// SPDX-License-Identifier: GPL-2.0-only
/*
 * Battery driver for the Argon ONE UP laptop (CW2217 fuel gauge, I2C).
 *
 * Author:  Jeff Curless
 *
 * Follows the Linux I2C driver model: per-device state allocated in probe(),
 * devm_* resource management, delayed_work instead of a raw kthread, and
 * module_i2c_driver() instead of manual module_init/exit.
 */

#include <linux/delay.h>
#include <linux/devm-helpers.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm.h>
#include <linux/power_supply.h>
#include <linux/reboot.h>
#include <linux/workqueue.h>
#include <generated/utsrelease.h>


#define VERSION_MAJOR   1
#define VERSION_MINOR   0
#define VERSION_EDIT    3

#define DRV_NAME                    "oneUpPower"
#define PR_INFO(fmt, arg...)        printk(KERN_INFO DRV_NAME ": " fmt, ##arg)
#define PR_ERR(fmt, arg...)         printk(KERN_ERR  DRV_NAME ": " fmt, ##arg)

#define TOTAL_LIFE_SECONDS          (6 * 60 * 60)
#define TOTAL_CHARGE                (4800 * 1000)
#define TOTAL_CHARGE_FULL_SECONDS   (((2*60)+30) * 60)

//
// I2C Addresses
//
#define BATTERY_ADDR        0x64
#define CURRENT_HIGH_REG    0x0E
#define CURRENT_LOW_REG     0x0F
#define SOC_HIGH_REG        0x04
#define SOC_LOW_REG         0x05

//
// Battery IC profile/control registers (CW2217)
//
#define REG_CONTROL         0x08
#define REG_GPIOCONFIG      0x0A
#define REG_SOCALERT        0x0B
#define REG_PROFILE         0x10
#define REG_ICSTATE         0xA7

#define CTRL_RESTART        0x30
#define CTRL_SLEEP          0xF0
#define CTRL_ACTIVE         0x00

//
// Per-device state, allocated by devm_kzalloc() in probe().
//
struct oneup_battery {
	struct i2c_client    *client;
	struct delayed_work   work;
	struct power_supply  *bat_psy;
	struct power_supply  *ac_psy;

	int soc;            /* state of charge, 0–100 % */
	int ac_online;      /* 1 = charger connected */
	int status;         /* POWER_SUPPLY_STATUS_* */
	int capacity_level; /* POWER_SUPPLY_CAPACITY_LEVEL_* */
	int soc_shutdown;   /* threshold copied from module param at probe */
};

//
// Module parameter — global default, copied into bat->soc_shutdown at probe.
//
static int soc_shutdown = 5;

//
// Battery model profile for the CW2217 fuel gauge (80 bytes starting at REG_PROFILE).
// This OCV curve was extracted from archive/kickstarter/argononeupd.py.
// Without a matching profile the IC's SOC algorithm uses incorrect chemistry
// data, causing inaccurate percentage readings especially near full and empty.
//
static const u8 battery_profile[] = {
	0x32, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0xA8, 0xAA, 0xBE, 0xC6, 0xB8, 0xAE, 0xC2, 0x98,
	0x82, 0xFF, 0xFF, 0xCA, 0x98, 0x75, 0x63, 0x55,
	0x4E, 0x4C, 0x49, 0x98, 0x88, 0xDC, 0x34, 0xDB,
	0xD3, 0xD4, 0xD3, 0xD0, 0xCE, 0xCB, 0xBB, 0xE7,
	0xA2, 0xC2, 0xC4, 0xAE, 0x96, 0x89, 0x80, 0x74,
	0x67, 0x63, 0x71, 0x8E, 0x9F, 0x85, 0x6F, 0x3B,
	0x20, 0x00, 0xAB, 0x10, 0xFF, 0xB0, 0x73, 0x00,
	0x00, 0x00, 0x64, 0x08, 0xD3, 0x77, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFA,
};

//
// Properties for AC
//
static enum power_supply_property power_ac_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

//
// Properties supported by the battery
//
static enum power_supply_property power_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CHARGE_EMPTY,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG,
	POWER_SUPPLY_PROP_TIME_TO_FULL_NOW,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_SERIAL_NUMBER,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
};

static char *ac_supplied_to[] = { "BAT0" };

// Forward declarations for descriptor references below.
static int oneup_bat_get_property(struct power_supply *psy,
				  enum power_supply_property psp,
				  union power_supply_propval *val);
static int oneup_ac_get_property(struct power_supply *psy,
				 enum power_supply_property psp,
				 union power_supply_propval *val);

static const struct power_supply_desc oneup_bat_desc = {
	.name           = "BAT0",
	.type           = POWER_SUPPLY_TYPE_BATTERY,
	.properties     = power_battery_props,
	.num_properties = ARRAY_SIZE(power_battery_props),
	.get_property   = oneup_bat_get_property,
};

static const struct power_supply_desc oneup_ac_desc = {
	.name           = "AC0",
	.type           = POWER_SUPPLY_TYPE_MAINS,
	.properties     = power_ac_props,
	.num_properties = ARRAY_SIZE(power_ac_props),
	.get_property   = oneup_ac_get_property,
};

//
// set_power_states
//
// Given the current SOC and AC state, update bat->status and bat->capacity_level.
//
static void set_power_states(struct oneup_battery *bat)
{
	int capacity = bat->soc;

	if (capacity > 95)
		bat->capacity_level = POWER_SUPPLY_CAPACITY_LEVEL_FULL;
	else if (capacity > 85)
		bat->capacity_level = POWER_SUPPLY_CAPACITY_LEVEL_HIGH;
	else if (capacity > 75)
		bat->capacity_level = POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;
	else if (capacity > 40)
		bat->capacity_level = POWER_SUPPLY_CAPACITY_LEVEL_LOW;
	else
		bat->capacity_level = POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL;

	if (bat->ac_online) {
		if (capacity > 95)
			bat->status = POWER_SUPPLY_STATUS_FULL;
		else
			bat->status = POWER_SUPPLY_STATUS_CHARGING;
	} else {
		bat->status = POWER_SUPPLY_STATUS_DISCHARGING;
	}
}

//
// check_ac_power
//
// Read the current direction register and update bat->ac_online.
//
// Bit 7 of CURRENT_HIGH_REG: 1 = discharging (no AC), 0 = charging (AC present).
//
static void check_ac_power(struct oneup_battery *bat)
{
	int current_high;
	int plugged_in;

	current_high = i2c_smbus_read_byte_data(bat->client, CURRENT_HIGH_REG);

	plugged_in = ((current_high & 0x80) == 0x80) ? 0 : 1;

	if (bat->ac_online != plugged_in) {
		bat->ac_online = plugged_in;
		if (bat->ac_online)
			PR_INFO("AC Power is connected.\n");
		else
			PR_INFO("AC Power is disconnected.\n");
	}
}

//
// check_battery_state
//
// Read the state-of-charge register and update bat->soc.
//
static void check_battery_state(struct oneup_battery *bat)
{
	int soc;

	soc = i2c_smbus_read_byte_data(bat->client, SOC_HIGH_REG);
	if (soc > 100)
		soc = 100;
	if (soc < 0)
		soc = 0;

	if (bat->soc != soc) {
		bat->soc = soc;
		PR_INFO("Battery State of charge is %d%%\n", soc);
	}
}

//
// shutdown_helper
//
// Trigger a graceful system shutdown when battery is critically low.
//
static void shutdown_helper(void)
{
	orderly_poweroff(false);
}

//
// restart_battery_ic
//
// Cycle the CW2217 control register to bring it out of sleep or error state,
// then poll REG_ICSTATE until the IC reports it is ready (bits [3:2] non-zero).
//
// Returns:
//     0          - IC is active and ready
//     -ETIMEDOUT - IC did not become ready after retries
//
static int restart_battery_ic(struct i2c_client *client)
{
	int icstate;
	int attempt;
	int wait;

	for (attempt = 0; attempt < 3; attempt++) {
		i2c_smbus_write_byte_data(client, REG_CONTROL, CTRL_RESTART);
		msleep(500);

		i2c_smbus_write_byte_data(client, REG_CONTROL, CTRL_ACTIVE);
		msleep(500);

		for (wait = 0; wait < 5; wait++) {
			icstate = i2c_smbus_read_byte_data(client, REG_ICSTATE);
			if (icstate >= 0 && (icstate & 0x0C) != 0) {
				PR_INFO("Battery IC activated.\n");
				return 0;
			}
			msleep(1000);
		}
	}

	PR_ERR("Battery IC did not become ready.\n");
	return -ETIMEDOUT;
}

//
// init_battery_profile
//
// Verify the 80-byte OCV curve stored in the CW2217 against the known-good
// profile for this battery pack.  If the IC is inactive, the profile flag is
// unset, or any byte mismatches, the IC is put to sleep, the full profile is
// written, and the IC is restarted.
//
// Ported from battery_checkupdateprofile() in archive/kickstarter/argononeupd.py.
//
// Returns:
//     0  - Profile is valid (already matched or successfully updated)
//    <0  - I2C error, or IC failed to restart after programming
//
static int init_battery_profile(struct i2c_client *client)
{
	int  control;
	int  socalert;
	int  val;
	int  i;
	int  ret;
	bool profile_ok = false;

	PR_INFO("Checking battery profile...\n");

	//
	// IC is active when REG_CONTROL reads back 0
	//
	control = i2c_smbus_read_byte_data(client, REG_CONTROL);
	if (control == 0) {
		//
		// IC is up; check if the profile-loaded flag is set
		//
		socalert = i2c_smbus_read_byte_data(client, REG_SOCALERT);
		if (socalert >= 0 && (socalert & 0x80) != 0) {
			//
			// Flag set; verify every profile byte
			//
			profile_ok = true;
			for (i = 0; i < ARRAY_SIZE(battery_profile); i++) {
				val = i2c_smbus_read_byte_data(client, REG_PROFILE + i);
				if (val < 0 || (u8)val != battery_profile[i]) {
					PR_INFO("Battery profile mismatch at byte %d.\n", i);
					profile_ok = false;
					break;
				}
			}
		}
	}

	if (profile_ok) {
		PR_INFO("Battery profile is valid.\n");
		return 0;
	}

	PR_INFO("Programming battery profile...\n");

	//
	// Restart then sleep the IC before writing
	//
	ret = i2c_smbus_write_byte_data(client, REG_CONTROL, CTRL_RESTART);
	if (ret < 0) {
		PR_ERR("Failed to restart IC before profile write: %d\n", ret);
		return ret;
	}
	msleep(500);

	ret = i2c_smbus_write_byte_data(client, REG_CONTROL, CTRL_SLEEP);
	if (ret < 0) {
		PR_ERR("Failed to sleep IC before profile write: %d\n", ret);
		return ret;
	}
	msleep(500);

	//
	// Write the 80-byte battery model profile
	//
	for (i = 0; i < ARRAY_SIZE(battery_profile); i++) {
		ret = i2c_smbus_write_byte_data(client, REG_PROFILE + i, battery_profile[i]);
		if (ret < 0) {
			PR_ERR("Failed to write profile byte %d: %d\n", i, ret);
			return ret;
		}
	}

	//
	// Mark profile as loaded
	//
	ret = i2c_smbus_write_byte_data(client, REG_SOCALERT, 0x80);
	if (ret < 0) {
		PR_ERR("Failed to set profile flag: %d\n", ret);
		return ret;
	}
	msleep(500);

	//
	// Disable IC interrupts
	//
	ret = i2c_smbus_write_byte_data(client, REG_GPIOCONFIG, 0x00);
	if (ret < 0) {
		PR_ERR("Failed to configure GPIO: %d\n", ret);
		return ret;
	}
	msleep(500);

	//
	// Restart and wait for the IC to become ready
	//
	ret = restart_battery_ic(client);
	if (ret != 0) {
		PR_ERR("Battery IC failed to restart after profile update.\n");
		return ret;
	}

	PR_INFO("Battery profile updated successfully.\n");
	return 0;
}

//
// oneup_battery_work
//
// Periodic work handler (replaces the old kthread).  Polls the hardware,
// updates the per-device state, and re-queues itself every second.
// devm_delayed_work_autocancel() ensures this is cancelled on device removal.
//
static void oneup_battery_work(struct work_struct *work)
{
	struct oneup_battery *bat =
		container_of(to_delayed_work(work), struct oneup_battery, work);

	check_ac_power(bat);
	check_battery_state(bat);
	set_power_states(bat);

	if (bat->ac_online == 0 && bat->soc < bat->soc_shutdown) {
		PR_INFO("Performing system shutdown: unplugged and power at %d%%\n",
			bat->soc);
		shutdown_helper();
	}

	power_supply_changed(bat->bat_psy);
	power_supply_changed(bat->ac_psy);

	schedule_delayed_work(&bat->work, HZ);
}

//
// oneup_ac_get_property
//
static int oneup_ac_get_property(struct power_supply *psy,
				 enum power_supply_property psp,
				 union power_supply_propval *val)
{
	struct oneup_battery *bat = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = bat->ac_online;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

//
// oneup_bat_get_property
//
static int oneup_bat_get_property(struct power_supply *psy,
				  enum power_supply_property psp,
				  union power_supply_propval *val)
{
	struct oneup_battery *bat = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = bat->status;
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		val->intval = POWER_SUPPLY_CHARGE_TYPE_FAST;
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = POWER_SUPPLY_HEALTH_GOOD;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = 1;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	case POWER_SUPPLY_PROP_CAPACITY_LEVEL:
		val->intval = bat->capacity_level;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = bat->soc;
		break;
	case POWER_SUPPLY_PROP_CHARGE_EMPTY:
		val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_CHARGE_NOW:
		val->intval = bat->soc * TOTAL_CHARGE / 100;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		val->intval = TOTAL_CHARGE;
		break;
	case POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG:
		val->intval = bat->soc * TOTAL_LIFE_SECONDS / 100;
		break;
	case POWER_SUPPLY_PROP_TIME_TO_FULL_NOW:
		val->intval = (100 - bat->soc) * TOTAL_CHARGE_FULL_SECONDS / 100;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = 300;		/* tenths of °C; 300 = 30.0°C */
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = (4200 * 1000);	/* uV */
		break;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = "oneUp Battery";
		break;
	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = "Argon40";
		break;
	case POWER_SUPPLY_PROP_SERIAL_NUMBER:
		val->strval = UTS_RELEASE;
		break;
	default:
		PR_INFO("%s: some properties deliberately report errors.\n", __func__);
		return -EINVAL;
	}
	return 0;
}

//
// PM suspend/resume — cancel the work on suspend, reschedule on resume.
//
static int __maybe_unused oneup_battery_suspend(struct device *dev)
{
	struct oneup_battery *bat = i2c_get_clientdata(to_i2c_client(dev));

	cancel_delayed_work_sync(&bat->work);
	return 0;
}

static int __maybe_unused oneup_battery_resume(struct device *dev)
{
	struct oneup_battery *bat = i2c_get_clientdata(to_i2c_client(dev));

	schedule_delayed_work(&bat->work, 0);
	return 0;
}

static SIMPLE_DEV_PM_OPS(oneup_battery_pm_ops,
			 oneup_battery_suspend, oneup_battery_resume);

//
// oneup_battery_probe
//
// Called by the I2C core when a device matching our id_table or of_match_table
// is discovered.  Allocates per-device state, initialises the battery IC,
// registers the power supplies, and kicks off the periodic work.
//
static int oneup_battery_probe(struct i2c_client *client)
{
	struct oneup_battery *bat;
	struct power_supply_config bat_cfg = {};
	struct power_supply_config ac_cfg = {};
	int ret;

	bat = devm_kzalloc(&client->dev, sizeof(*bat), GFP_KERNEL);
	if (!bat)
		return -ENOMEM;

	bat->client         = client;
	bat->soc            = 100;
	bat->ac_online      = 1;
	bat->status         = POWER_SUPPLY_STATUS_DISCHARGING;
	bat->capacity_level = POWER_SUPPLY_CAPACITY_LEVEL_HIGH;
	bat->soc_shutdown   = soc_shutdown;
	i2c_set_clientdata(client, bat);

	ret = init_battery_profile(client);
	if (ret)
		dev_warn(&client->dev,
			 "Profile init failed; SOC readings may be inaccurate\n");

	bat_cfg.drv_data = bat;
	bat_cfg.fwnode   = dev_fwnode(&client->dev);

	bat->bat_psy = devm_power_supply_register(&client->dev,
						  &oneup_bat_desc, &bat_cfg);
	if (IS_ERR(bat->bat_psy))
		return dev_err_probe(&client->dev, PTR_ERR(bat->bat_psy),
				     "Failed to register battery supply\n");

	ac_cfg.drv_data        = bat;
	ac_cfg.fwnode          = dev_fwnode(&client->dev);
	ac_cfg.supplied_to     = ac_supplied_to;
	ac_cfg.num_supplicants = ARRAY_SIZE(ac_supplied_to);

	bat->ac_psy = devm_power_supply_register(&client->dev,
						 &oneup_ac_desc, &ac_cfg);
	if (IS_ERR(bat->ac_psy))
		return dev_err_probe(&client->dev, PTR_ERR(bat->ac_psy),
				     "Failed to register AC supply\n");

	ret = devm_delayed_work_autocancel(&client->dev, &bat->work,
					   oneup_battery_work);
	if (ret)
		return ret;

	schedule_delayed_work(&bat->work, 0);

	PR_INFO("Probe successful (v%d.%d.%d)\n",
		VERSION_MAJOR, VERSION_MINOR, VERSION_EDIT);
	return 0;
}

static const struct of_device_id oneup_battery_of_match[] = {
	{ .compatible = "argon40,oneup-battery" },
	{ }
};
MODULE_DEVICE_TABLE(of, oneup_battery_of_match);

static const struct i2c_device_id oneup_battery_i2c_id[] = {
	{ "oneup-battery" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, oneup_battery_i2c_id);

static struct i2c_driver oneup_battery_driver = {
	.driver = {
		.name           = "oneup-battery",
		.of_match_table = oneup_battery_of_match,
		.pm             = &oneup_battery_pm_ops,
	},
	.probe    = oneup_battery_probe,
	.id_table = oneup_battery_i2c_id,
};
module_i2c_driver(oneup_battery_driver);

static int param_set_soc_shutdown(const char *key, const struct kernel_param *kp)
{
	long soc;

	if (kstrtol(key, 10, &soc) == 0) {
		if (soc == 0) {
			PR_INFO("Disabling automatic shutdown when battery is below threshold.\n");
			soc_shutdown = 0;
			return 0;
		} else if ((soc >= 1) && (soc <= 20)) {
			PR_INFO("Changing automatic shutdown when battery is below %ld%%\n", soc);
			soc_shutdown = soc;
			return 0;
		} else {
			PR_INFO("Invalid value (%ld%%), please change to: 0 to disable, 1-20 to set shutdown threshold.\n", soc);
			return 0;
		}
	} else {
		PR_INFO("Could not convert to integer\n");
	}
	return -ENOENT;
}

static int param_get_soc_shutdown(char *buffer, const struct kernel_param *kp)
{
	return sprintf(buffer, "%d", soc_shutdown);
}

static const struct kernel_param_ops param_ops_soc_shutdown = {
	.set = param_set_soc_shutdown,
	.get = param_get_soc_shutdown,
};

#define param_check_soc_shutdown(name, p) __param_check(name, p, void)
module_param(soc_shutdown, soc_shutdown, 0644);
MODULE_PARM_DESC(soc_shutdown, "Shutdown system when the battery state of charge is lower than this value.");

MODULE_DESCRIPTION("Power supply driver for Argon40 1UP");
MODULE_AUTHOR("Jeff Curless <jeff@thecurlesses.com>");
MODULE_LICENSE("GPL");
