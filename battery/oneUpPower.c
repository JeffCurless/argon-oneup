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

#define pr_fmt(fmt) "oneup-battery: " fmt

#include <linux/delay.h>
#include <linux/devm-helpers.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm.h>
#include <linux/power_supply.h>
#include <linux/reboot.h>
#include <linux/spinlock.h>
#include <linux/sysfs.h>
#include <linux/workqueue.h>

#define VERSION_MAJOR   1
#define VERSION_MINOR   0
#define VERSION_EDIT    8

#define CELLS_IN_SERIES             3
#define TOTAL_LIFE_SECONDS          (6 * 60 * 60)
#define TOTAL_CHARGE                (4800 * 1000)
#define TOTAL_CHARGE_FULL_SECONDS   (((2*60)+30) * 60)

#define CW2217_RSENSE_MOHM          10

/* I2C register addresses */
#define REG_VCELL_HIGH              0x02
#define REG_VCELL_LOW               0x03
#define REG_SOC_HIGH                0x04
#define REG_SOC_LOW                 0x05
#define REG_TEMP                    0x06
#define REG_CONTROL                 0x08
#define REG_GPIOCONFIG              0x0A
#define REG_SOCALERT                0x0B
#define REG_CURRENT_HIGH            0x0E
#define REG_CURRENT_LOW             0x0F
#define REG_PROFILE                 0x10
#define REG_CYCLE_HIGH              0xA4
#define REG_CYCLE_LOW               0xA5
#define REG_SOH                     0xA6
#define REG_ICSTATE                 0xA7
#define REG_STB_CUR_HIGH            0xA8
#define REG_STB_CUR_LOW             0xA9

/* Backward compatibility aliases */
#define CURRENT_HIGH_REG            REG_CURRENT_HIGH
#define SOC_HIGH_REG                REG_SOC_HIGH

#define CTRL_RESTART        0x30
#define CTRL_SLEEP          0xF0
#define CTRL_ACTIVE         0x00

/* Bit fields */
#define CURRENT_HIGH_DISCHARGE_BIT  0x80  /* set = discharging, clear = charging */
#define SOCALERT_PROFILE_FLAG       0x80  /* set = OCV profile loaded */
#define ICSTATE_ACTIVE_MASK         0x0C  /* non-zero = IC ready */

/* SOC capacity-level thresholds (percent) */
#define SOC_THRESH_FULL     95
#define SOC_THRESH_HIGH     75
#define SOC_THRESH_NORMAL   25
#define SOC_THRESH_LOW      10

/* CW2217 timing and retry constants */
#define CW2217_RESTART_DELAY_MS     100
#define CW2217_ICSTATE_POLL_MS      100
#define CW2217_RESTART_ATTEMPTS     3
#define CW2217_ICSTATE_POLL_RETRIES 5

/*
 * Per-device state, allocated by devm_kzalloc() in probe().
 */
struct oneup_battery {
	struct i2c_client    *client;
	struct delayed_work   work;
	struct power_supply  *bat_psy;
	struct power_supply  *ac_psy;

	/*
	 * Protects soc, ac_online, status, capacity_level, voltage_uv,
	 * current_ua, and temp_dc against concurrent reads from sysfs getters
	 * while the work handler writes them.
	 */
	spinlock_t lock;

	int soc;            /* state of charge, 0-100 % */
	int soc_fractional; /* 16-bit fractional SOC: (soc_h << 8) | soc_l */
	int cycle_count;    /* full charge cycles */
	int soh;            /* state of health, 0-100 % */
	int standby_current_ua; /* standby / quiescent current in µA */
	int ac_online;      /* 1 = charger connected */
	int ac_candidate;   /* most recent raw read awaiting confirmation */
	int ac_debounce;    /* consecutive polls confirming ac_candidate */
	int status;         /* POWER_SUPPLY_STATUS_* */
	int capacity_level; /* POWER_SUPPLY_CAPACITY_LEVEL_* */
	int voltage_uv;     /* cell voltage in µV */
	int current_ua;     /* current in µA (+ charging, - discharging) */
	int temp_dc;        /* temperature in tenths of °C */
	bool shutdown_triggered; /* true once orderly_poweroff has been called */
};

/* Module parameters - read at runtime via READ_ONCE() from the work handler. */
static int soc_shutdown = 5;
static int ac_debounce_polls = 3;

/*
 * Battery model profile for the CW2217 (80 bytes starting at REG_PROFILE).
 * OCV curve extracted from archive/kickstarter/argononeupd.py. Without a
 * matching profile the IC's SOC algorithm produces inaccurate readings
 * especially near full and empty.
 */
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

static enum power_supply_property power_ac_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

static enum power_supply_property power_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_POWER_NOW,
	POWER_SUPPLY_PROP_TEMP,
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
};

static char *ac_supplied_to[] = { "BAT0" };

/* Forward declarations for descriptor references below. */
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

/*
 * Read cell voltage from registers 0x02 (high) and 0x03 (low).
 * Bits 13:0, scaled by 312.5 µV (625 / 2).
 * Stores voltage in microvolts (µV) into *volt. Returns 0 on success or -errno.
 */
static int read_voltage_uv(struct i2c_client *client, int *volt)
{
	int v_hi, v_lo;
	u16 raw_v;

	v_hi = i2c_smbus_read_byte_data(client, REG_VCELL_HIGH);
	if (v_hi < 0)
		return v_hi;

	v_lo = i2c_smbus_read_byte_data(client, REG_VCELL_LOW);
	if (v_lo < 0)
		return v_lo;

	raw_v = ((v_hi & 0x3F) << 8) | (v_lo & 0xFF);
	*volt = (int)((u64)raw_v * 625 / 2);
	return 0;
}

/*
 * Read battery current from registers 0x0E (high) and 0x0F (low).
 * Signed 16-bit word, converted to µA using 10 mΩ sense resistor.
 * Stores current in microamperes (µA, + charging, - discharging) into *curr.
 * Returns 0 on success or -errno.
 */
static int read_current_ua(struct i2c_client *client, int *curr)
{
	int c_hi, c_lo;
	s16 raw_curr;
	s64 num, denom;

	c_hi = i2c_smbus_read_byte_data(client, REG_CURRENT_HIGH);
	if (c_hi < 0)
		return c_hi;

	c_lo = i2c_smbus_read_byte_data(client, REG_CURRENT_LOW);
	if (c_lo < 0)
		return c_lo;

	raw_curr = (s16)(((u16)(c_hi & 0xFF) << 8) | ((u16)(c_lo & 0xFF)));
	num = (s64)raw_curr * 52400000LL;
	denom = (s64)32768 * CW2217_RSENSE_MOHM;
	num += (num >= 0) ? (denom / 2) : -(denom / 2);

	*curr = (int)div_s64(num, denom);
	return 0;
}

/*
 * Read battery temperature from register 0x06.
 * Linear formula: -40.0 °C + 0.5 °C * raw_byte -> tenths of °C.
 * Stores temperature in tenths of a degree Celsius (0.1 °C) into *temp.
 * Returns 0 on success or -errno.
 */
static int read_temperature_dc(struct i2c_client *client, int *temp)
{
	int temp_raw;

	temp_raw = i2c_smbus_read_byte_data(client, REG_TEMP);
	if (temp_raw < 0)
		return temp_raw;

	*temp = -400 + 5 * temp_raw;
	return 0;
}

/*
 * Read cycle count from registers 0xA4 (high) and 0xA5 (low).
 * Unsigned 16-bit word, LSB is 1/16th of a cycle.
 */
static int read_cycle_count(struct i2c_client *client, int *cycles)
{
	int c_hi, c_lo;
	u16 raw_cycles;

	c_hi = i2c_smbus_read_byte_data(client, REG_CYCLE_HIGH);
	if (c_hi < 0)
		return c_hi;

	c_lo = i2c_smbus_read_byte_data(client, REG_CYCLE_LOW);
	if (c_lo < 0)
		return c_lo;

	raw_cycles = ((u16)(c_hi & 0xFF) << 8) | ((u16)(c_lo & 0xFF));
	*cycles = (int)(raw_cycles / 16);
	return 0;
}

/*
 * Read State of Health (SOH) from register 0xA6.
 * Integer percentage from 0 to 100.
 */
static int read_soh(struct i2c_client *client, int *soh)
{
	int val;

	val = i2c_smbus_read_byte_data(client, REG_SOH);
	if (val < 0)
		return val;

	if (val > 100)
		val = 100;
	else if (val <= 0)
		val = 100;

	*soh = val;
	return 0;
}

/*
 * Read standby current from registers 0xA8 (high) and 0xA9 (low).
 * Signed 16-bit word, converted to µA using 10 mΩ sense resistor.
 */
static int read_standby_current_ua(struct i2c_client *client, int *curr)
{
	int c_hi, c_lo;
	s16 raw_curr;
	s64 num, denom;

	c_hi = i2c_smbus_read_byte_data(client, REG_STB_CUR_HIGH);
	if (c_hi < 0)
		return c_hi;

	c_lo = i2c_smbus_read_byte_data(client, REG_STB_CUR_LOW);
	if (c_lo < 0)
		return c_lo;

	raw_curr = (s16)(((u16)(c_hi & 0xFF) << 8) | ((u16)(c_lo & 0xFF)));
	num = (s64)raw_curr * 52400000LL;
	denom = (s64)32768 * CW2217_RSENSE_MOHM;
	num += (num >= 0) ? (denom / 2) : -(denom / 2);

	*curr = (int)div_s64(num, denom);
	return 0;
}

static ssize_t current_standby_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct power_supply *psy = to_power_supply(dev);
	struct oneup_battery *bat = power_supply_get_drvdata(psy);
	int val;

	spin_lock(&bat->lock);
	val = bat->standby_current_ua;
	spin_unlock(&bat->lock);

	return sysfs_emit(buf, "%d\n", val);
}
static DEVICE_ATTR_RO(current_standby);

static struct attribute *oneup_battery_attrs[] = {
	&dev_attr_current_standby.attr,
	NULL,
};
ATTRIBUTE_GROUPS(oneup_battery);

/*
 * Update bat->soc, bat->status, and bat->capacity_level from the given SOC and
 * the current AC state.  All fields are evaluated and written under one lock
 * hold so sysfs getters always see a consistent snapshot.
 */
static void set_power_states(struct oneup_battery *bat, int soc)
{
	int new_level, new_status;

	if (soc > SOC_THRESH_FULL)
		new_level = POWER_SUPPLY_CAPACITY_LEVEL_FULL;
	else if (soc > SOC_THRESH_HIGH)
		new_level = POWER_SUPPLY_CAPACITY_LEVEL_HIGH;
	else if (soc > SOC_THRESH_NORMAL)
		new_level = POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;
	else if (soc > SOC_THRESH_LOW)
		new_level = POWER_SUPPLY_CAPACITY_LEVEL_LOW;
	else
		new_level = POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL;

	spin_lock(&bat->lock);
	if (bat->ac_online)
		new_status = (soc > SOC_THRESH_FULL) ? POWER_SUPPLY_STATUS_FULL
						     : POWER_SUPPLY_STATUS_CHARGING;
	else
		new_status = POWER_SUPPLY_STATUS_DISCHARGING;

	bat->soc            = soc;
	bat->capacity_level = new_level;
	bat->status         = new_status;
	spin_unlock(&bat->lock);
}

/*
 * Read the current direction register and update bat->ac_online.
 * Bit 7 of REG_CURRENT_HIGH: set = discharging (no AC), clear = charging (AC present).
 */
static void check_ac_power(struct oneup_battery *bat)
{
	int current_high;
	int plugged_in;
	int threshold;
	int current_ac_online;

	current_high = i2c_smbus_read_byte_data(bat->client, REG_CURRENT_HIGH);
	if (current_high < 0) {
		dev_err(&bat->client->dev, "Failed to read current register: %d\n", current_high);
		return;
	}

	plugged_in = (current_high & CURRENT_HIGH_DISCHARGE_BIT) ? 0 : 1;

	spin_lock(&bat->lock);
	current_ac_online = bat->ac_online;
	spin_unlock(&bat->lock);

	/* Raw read matches current state - no transition in progress. */
	if (plugged_in == current_ac_online) {
		bat->ac_candidate = plugged_in;
		bat->ac_debounce  = 0;
		return;
	}

	/* Accumulate consecutive confirmations of the candidate value. */
	if (plugged_in == bat->ac_candidate) {
		bat->ac_debounce++;
	} else {
		bat->ac_candidate = plugged_in;
		bat->ac_debounce  = 1;
	}

	/* Clamp the tunable to a sane range at point of use. */
	threshold = clamp_val(ac_debounce_polls, 1, 10);

	if (bat->ac_debounce >= threshold) {
		spin_lock(&bat->lock);
		bat->ac_online = plugged_in;
		spin_unlock(&bat->lock);
		bat->ac_debounce = 0;
		if (plugged_in)
			dev_info(&bat->client->dev, "AC Power is connected.\n");
		else
			dev_info(&bat->client->dev, "AC Power is disconnected.\n");
	}
}

/*
 * Read the state-of-charge register.  Returns the new SOC, or the previous
 * value if the read fails.  bat->soc itself is updated by set_power_states()
 * so it stays consistent with status and capacity_level.
 */
static int check_battery_state(struct oneup_battery *bat, int *frac_soc)
{
	int soc_h, soc_l;

	soc_h = i2c_smbus_read_byte_data(bat->client, SOC_HIGH_REG);
	if (soc_h < 0) {
		dev_err(&bat->client->dev, "Failed to read SOC register: %d\n", soc_h);
		return bat->soc;
	}
	soc_l = i2c_smbus_read_byte_data(bat->client, REG_SOC_LOW);
	if (soc_l < 0)
		soc_l = 0;

	if (soc_h > 100)
		soc_h = 100;

	if (frac_soc)
		*frac_soc = (soc_h << 8) | (u8)soc_l;

	if (bat->soc != soc_h)
		dev_info(&bat->client->dev, "Battery State of charge is %d%%\n", soc_h);

	return soc_h;
}

/* Trigger a graceful system shutdown when battery is critically low. */
static void shutdown_helper(void)
{
	orderly_poweroff(false);
}

/*
 * Cycle the CW2217 control register to bring it out of sleep or error state,
 * then poll REG_ICSTATE until the IC reports it is ready (bits [3:2] non-zero).
 * Returns 0 if the IC is active and ready, or -ETIMEDOUT if it did not become
 * ready after retries.
 */
static int restart_battery_ic(struct i2c_client *client)
{
	int icstate;
	int attempt;
	int wait;
	int ret;

	for (attempt = 0; attempt < CW2217_RESTART_ATTEMPTS; attempt++) {
		ret = i2c_smbus_write_byte_data(client, REG_CONTROL, CTRL_RESTART);
		if (ret < 0) {
			dev_err(&client->dev, "Failed to write CTRL_RESTART: %d\n", ret);
			return ret;
		}
		msleep(CW2217_RESTART_DELAY_MS);

		ret = i2c_smbus_write_byte_data(client, REG_CONTROL, CTRL_ACTIVE);
		if (ret < 0) {
			dev_err(&client->dev, "Failed to write CTRL_ACTIVE: %d\n", ret);
			return ret;
		}
		msleep(CW2217_RESTART_DELAY_MS);

		for (wait = 0; wait < CW2217_ICSTATE_POLL_RETRIES; wait++) {
			icstate = i2c_smbus_read_byte_data(client, REG_ICSTATE);
			if (icstate >= 0 && (icstate & ICSTATE_ACTIVE_MASK) != 0) {
				dev_info(&client->dev, "Battery IC activated.\n");
				return 0;
			}
			msleep(CW2217_ICSTATE_POLL_MS);
		}
	}

	dev_err(&client->dev, "Battery IC did not become ready.\n");
	return -ETIMEDOUT;
}

/*
 * Verify the 80-byte OCV curve stored in the CW2217 against the known-good
 * profile for this battery pack.  If the IC is inactive, the profile flag is
 * unset, or any byte mismatches, the IC is put to sleep, the full profile is
 * written, and the IC is restarted.
 *
 * Ported from battery_checkupdateprofile() in archive/kickstarter/argononeupd.py.
 *
 * Returns 0 if the profile is valid or was successfully updated, or a negative
 * errno on I2C error or if the IC failed to restart after programming.
 */
static int init_battery_profile(struct i2c_client *client)
{
	int control;
	int socalert;
	int val;
	int i;
	int ret;
	bool profile_ok = false;

	dev_info(&client->dev, "Checking battery profile...\n");

	/* IC is active when REG_CONTROL reads back 0 */
	control = i2c_smbus_read_byte_data(client, REG_CONTROL);
	if (control < 0) {
		dev_err(&client->dev, "Failed to read control register: %d\n", control);
		return control;
	}
	if (control == 0) {
		/* IC is up; check if the profile-loaded flag is set */
		socalert = i2c_smbus_read_byte_data(client, REG_SOCALERT);
		if (socalert >= 0 && (socalert & SOCALERT_PROFILE_FLAG) != 0) {
			/* Flag set; verify every profile byte */
			profile_ok = true;
			for (i = 0; i < ARRAY_SIZE(battery_profile); i++) {
				val = i2c_smbus_read_byte_data(client, REG_PROFILE + i);
				if (val < 0 || (u8)val != battery_profile[i]) {
					dev_info(&client->dev,
						 "Battery profile mismatch at byte %d.\n", i);
					profile_ok = false;
					break;
				}
			}
		}
	}

	if (profile_ok) {
		dev_info(&client->dev, "Battery profile is valid.\n");
		return 0;
	}

	dev_info(&client->dev, "Programming battery profile...\n");

	/* Restart then sleep the IC before writing */
	ret = i2c_smbus_write_byte_data(client, REG_CONTROL, CTRL_RESTART);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to restart IC before profile write: %d\n", ret);
		return ret;
	}
	msleep(CW2217_RESTART_DELAY_MS);

	ret = i2c_smbus_write_byte_data(client, REG_CONTROL, CTRL_SLEEP);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to sleep IC before profile write: %d\n", ret);
		return ret;
	}
	msleep(CW2217_RESTART_DELAY_MS);

	/* Write the 80-byte battery model profile */
	for (i = 0; i < ARRAY_SIZE(battery_profile); i++) {
		ret = i2c_smbus_write_byte_data(client, REG_PROFILE + i, battery_profile[i]);
		if (ret < 0) {
			dev_err(&client->dev, "Failed to write profile byte %d: %d\n", i, ret);
			return ret;
		}
	}

	/* Mark profile as loaded - RMW to preserve lower SOC alert threshold bits */
	socalert = i2c_smbus_read_byte_data(client, REG_SOCALERT);
	if (socalert < 0)
		socalert = 0;
	ret = i2c_smbus_write_byte_data(client, REG_SOCALERT,
					(u8)socalert | SOCALERT_PROFILE_FLAG);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to set profile flag: %d\n", ret);
		return ret;
	}
	msleep(CW2217_RESTART_DELAY_MS);

	/* Disable IC interrupts */
	ret = i2c_smbus_write_byte_data(client, REG_GPIOCONFIG, 0x00);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to configure GPIO: %d\n", ret);
		return ret;
	}
	msleep(CW2217_RESTART_DELAY_MS);

	/* Restart and wait for the IC to become ready */
	ret = restart_battery_ic(client);
	if (ret != 0) {
		dev_err(&client->dev, "Battery IC failed to restart after profile update.\n");
		return ret;
	}

	dev_info(&client->dev, "Battery profile updated successfully.\n");
	return 0;
}

/*
 * Periodic work handler: polls the hardware, updates per-device state, and
 * re-queues itself every second. devm_delayed_work_autocancel() ensures this
 * is cancelled on device removal.
 */
static void oneup_battery_work(struct work_struct *work)
{
	struct oneup_battery *bat =
		container_of(to_delayed_work(work), struct oneup_battery, work);
	int prev_soc            = bat->soc;
	int prev_ac_online      = bat->ac_online;
	int prev_status         = bat->status;
	int prev_capacity_level = bat->capacity_level;
	int v = 0, c = 0, t = 0;
	int frac_soc = 0, cycles = 0, soh = 100, stb_c = 0;
	bool v_ok, c_ok, t_ok, cycles_ok, soh_ok, stb_c_ok;

	check_ac_power(bat);
	set_power_states(bat, check_battery_state(bat, &frac_soc));

	v_ok      = (read_voltage_uv(bat->client, &v) == 0);
	c_ok      = (read_current_ua(bat->client, &c) == 0);
	t_ok      = (read_temperature_dc(bat->client, &t) == 0);
	cycles_ok = (read_cycle_count(bat->client, &cycles) == 0);
	soh_ok    = (read_soh(bat->client, &soh) == 0);
	stb_c_ok  = (read_standby_current_ua(bat->client, &stb_c) == 0);

	spin_lock(&bat->lock);
	bat->soc_fractional = frac_soc;
	if (v_ok)
		bat->voltage_uv = v;
	if (c_ok)
		bat->current_ua = c;
	if (t_ok)
		bat->temp_dc = t;
	if (cycles_ok)
		bat->cycle_count = cycles;
	if (soh_ok)
		bat->soh = soh;
	if (stb_c_ok)
		bat->standby_current_ua = stb_c;
	spin_unlock(&bat->lock);

	if (bat->ac_online == 0 && bat->soc < READ_ONCE(soc_shutdown) &&
	    !bat->shutdown_triggered) {
		dev_info(&bat->client->dev,
			 "Performing system shutdown: unplugged and power at %d%%\n",
			 bat->soc);
		bat->shutdown_triggered = true;
		shutdown_helper();
	}

	if (bat->soc != prev_soc || bat->status != prev_status ||
	    bat->capacity_level != prev_capacity_level)
		power_supply_changed(bat->bat_psy);

	if (bat->ac_online != prev_ac_online)
		power_supply_changed(bat->ac_psy);

	schedule_delayed_work(&bat->work, HZ);
}

static int oneup_ac_get_property(struct power_supply *psy,
				 enum power_supply_property psp,
				 union power_supply_propval *val)
{
	struct oneup_battery *bat = power_supply_get_drvdata(psy);
	int ac_online;

	spin_lock(&bat->lock);
	ac_online = bat->ac_online;
	spin_unlock(&bat->lock);

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = ac_online;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int oneup_bat_get_property(struct power_supply *psy,
				  enum power_supply_property psp,
				  union power_supply_propval *val)
{
	struct oneup_battery *bat = power_supply_get_drvdata(psy);
	int soc, soc_frac, status, capacity_level;
	int voltage_uv, current_ua, temp_dc;
	int cycle_count, soh;
	s64 full_uAh;

	spin_lock(&bat->lock);
	soc            = bat->soc;
	soc_frac       = bat->soc_fractional;
	status         = bat->status;
	capacity_level = bat->capacity_level;
	voltage_uv     = bat->voltage_uv;
	current_ua     = bat->current_ua;
	temp_dc        = bat->temp_dc;
	cycle_count    = bat->cycle_count;
	soh            = bat->soh;
	spin_unlock(&bat->lock);

	full_uAh = (s64)TOTAL_CHARGE * soh / 100LL;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = status;
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		val->intval = (status == POWER_SUPPLY_STATUS_CHARGING)
			? POWER_SUPPLY_CHARGE_TYPE_FAST
			: POWER_SUPPLY_CHARGE_TYPE_NONE;
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		if (temp_dc > 550)
			val->intval = POWER_SUPPLY_HEALTH_OVERHEAT;
		else if (temp_dc < 0)
			val->intval = POWER_SUPPLY_HEALTH_COLD;
		else if (soh < 40)
			val->intval = POWER_SUPPLY_HEALTH_DEAD;
		else
			val->intval = POWER_SUPPLY_HEALTH_GOOD;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = 1;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	case POWER_SUPPLY_PROP_CYCLE_COUNT:
		val->intval = cycle_count;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = voltage_uv * CELLS_IN_SERIES;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
		val->intval = 9900000;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		val->intval = 12600000;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = current_ua;
		break;
	case POWER_SUPPLY_PROP_POWER_NOW:
		val->intval = (int)div_s64((s64)(voltage_uv * CELLS_IN_SERIES) * abs(current_ua), 1000000LL);
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = temp_dc;
		break;
	case POWER_SUPPLY_PROP_CAPACITY_LEVEL:
		val->intval = capacity_level;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = soc;
		break;
	case POWER_SUPPLY_PROP_CHARGE_EMPTY:
		val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_CHARGE_NOW:
		val->intval = (int)div_s64((s64)soc_frac * full_uAh, 25600LL);
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		val->intval = TOTAL_CHARGE;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		val->intval = (int)full_uAh;
		break;
	case POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG:
		if (status == POWER_SUPPLY_STATUS_DISCHARGING) {
			int abs_curr = abs(current_ua);
			if (abs_curr > 50000) {
				s64 charge_uAh = (s64)soc * full_uAh / 100LL;
				val->intval = (int)div_s64(charge_uAh * 3600LL, abs_curr);
			} else {
				val->intval = soc * TOTAL_LIFE_SECONDS / 100;
			}
		} else {
			val->intval = 0;
		}
		break;
	case POWER_SUPPLY_PROP_TIME_TO_FULL_NOW:
		if (status == POWER_SUPPLY_STATUS_CHARGING) {
			if (current_ua > 50000) {
				s64 rem_uAh = (s64)(100 - soc) * full_uAh / 100LL;
				val->intval = (int)div_s64(rem_uAh * 3600LL, current_ua);
			} else {
				val->intval = (100 - soc) * TOTAL_CHARGE_FULL_SECONDS / 100;
			}
		} else {
			val->intval = 0;
		}
		break;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = "oneUp Battery";
		break;
	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = "Argon40";
		break;
	default:
		dev_dbg(&bat->client->dev, "%s: unsupported property\n", __func__);
		return -EINVAL;
	}
	return 0;
}

/* PM: cancel the work on suspend, reschedule on resume. */
static int oneup_battery_suspend(struct device *dev)
{
	struct oneup_battery *bat = i2c_get_clientdata(to_i2c_client(dev));

	cancel_delayed_work_sync(&bat->work);
	return 0;
}

static int oneup_battery_resume(struct device *dev)
{
	struct oneup_battery *bat = i2c_get_clientdata(to_i2c_client(dev));

	schedule_delayed_work(&bat->work, 0);
	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(oneup_battery_pm_ops,
				oneup_battery_suspend, oneup_battery_resume);

/*
 * Called by the I2C core when a device matching our id_table or of_match_table
 * is discovered.  Allocates per-device state, initialises the battery IC,
 * registers the power supplies, and kicks off the periodic work.
 */
static int oneup_battery_probe(struct i2c_client *client)
{
	struct oneup_battery *bat;
	struct power_supply_config bat_cfg = {};
	struct power_supply_config ac_cfg = {};
	int ret;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -EOPNOTSUPP;

	bat = devm_kzalloc(&client->dev, sizeof(*bat), GFP_KERNEL);
	if (!bat)
		return -ENOMEM;

	bat->client         = client;
	bat->soc            = 100;
	bat->soc            = 100;
	bat->soc_fractional = 100 << 8;
	bat->cycle_count    = 0;
	bat->soh            = 100;
	bat->standby_current_ua = 0;
	bat->ac_online      = 1;
	bat->ac_candidate   = 1;
	bat->status         = POWER_SUPPLY_STATUS_DISCHARGING;
	bat->capacity_level = POWER_SUPPLY_CAPACITY_LEVEL_HIGH;
	bat->voltage_uv     = 3800000;
	bat->current_ua     = 0;
	bat->temp_dc        = 250;
	spin_lock_init(&bat->lock);
	i2c_set_clientdata(client, bat);

	ret = init_battery_profile(client);
	if (ret)
		dev_warn(&client->dev,
			 "Profile init failed; SOC readings may be inaccurate\n");

	/*
	 * Read the real AC state before registering the power supplies so the
	 * first get_property call is correct.  Without this, the hardcoded
	 * ac_online=1 above causes the debounce logic to report AC connected
	 * for several poll cycles when the system boots on battery.
	 */
	ret = i2c_smbus_read_byte_data(client, REG_CURRENT_HIGH);
	if (ret >= 0) {
		bat->ac_online    = (ret & CURRENT_HIGH_DISCHARGE_BIT) ? 0 : 1;
		bat->ac_candidate = bat->ac_online;
		dev_info(&client->dev, "AC Power is %s at boot.\n",
			 bat->ac_online ? "connected" : "not connected");
	}
	read_voltage_uv(client, &bat->voltage_uv);
	read_current_ua(client, &bat->current_ua);
	read_temperature_dc(client, &bat->temp_dc);
	read_cycle_count(client, &bat->cycle_count);
	read_soh(client, &bat->soh);
	read_standby_current_ua(client, &bat->standby_current_ua);
	set_power_states(bat, check_battery_state(bat, &bat->soc_fractional));

	bat_cfg.drv_data = bat;
	bat_cfg.fwnode   = dev_fwnode(&client->dev);
	bat_cfg.attr_grp = oneup_battery_groups;

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

	dev_info(&client->dev, "Probe successful (v%d.%d.%d)\n",
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

static int param_set_soc_shutdown(const char *key, const struct kernel_param *kp)
{
	long soc;
	int ret;

	ret = kstrtol(key, 10, &soc);
	if (ret) {
		pr_warn("Could not convert to integer\n");
		return ret;
	}

	if (soc == 0) {
		pr_info("Disabling automatic shutdown when battery is below threshold.\n");
		soc_shutdown = 0;
		return 0;
	}

	if (soc >= 1 && soc <= 20) {
		pr_info("Changing automatic shutdown when battery is below %ld%%\n", soc);
		soc_shutdown = soc;
		return 0;
	}

	pr_warn("Invalid value (%ld%%), please change to: 0 to disable, 1-20 to set shutdown threshold.\n",
		soc);
	return -EINVAL;
}

static int param_get_soc_shutdown(char *buffer, const struct kernel_param *kp)
{
	return sysfs_emit(buffer, "%d\n", soc_shutdown);
}

static const struct kernel_param_ops param_ops_soc_shutdown = {
	.set = param_set_soc_shutdown,
	.get = param_get_soc_shutdown,
};

module_param_cb(soc_shutdown, &param_ops_soc_shutdown, &soc_shutdown, 0644);
MODULE_PARM_DESC(soc_shutdown, "Shutdown system when the battery state of charge is lower than this value.");

module_param(ac_debounce_polls, int, 0644);
MODULE_PARM_DESC(ac_debounce_polls, "Number of consecutive polls (1-10) to confirm AC state change (default 3).");

static struct i2c_driver oneup_battery_driver = {
	.driver = {
		.name           = "oneup-battery",
		.of_match_table = oneup_battery_of_match,
		.pm             = pm_sleep_ptr(&oneup_battery_pm_ops),
	},
	.probe    = oneup_battery_probe,
	.id_table = oneup_battery_i2c_id,
};
module_i2c_driver(oneup_battery_driver);

MODULE_DESCRIPTION("Power supply driver for Argon40 1UP");
MODULE_AUTHOR("Jeff Curless <jeff@thecurlesses.com>");
MODULE_LICENSE("GPL");
