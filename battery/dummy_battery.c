// SPDX-License-Identifier: GPL-2.0
// Simple dummy battery driver (out-of-tree module)
// Registers a power_supply "dummy-battery" with a few common properties
// and a small work loop to simulate charge/discharge.
//
// Build (out-of-tree):
//   make -C /lib/modules/$(uname -r)/build M=$PWD modules
// Use:
//   sudo insmod dummy_battery.ko start_capacity=82 discharge_rate=1
// See:
//   ls -l /sys/class/power_supply/dummy-battery
//
// References:
//  - Linux power_supply class docs
//  - In-tree drivers/power/supply/test_power.c (example test driver)

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/power_supply.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/jiffies.h>

#define DRV_NAME "dummy_battery"
#define DFLT_CAPACITY        75     // percent
#define DFLT_VOLTAGE_UV  4000000    // 4.0V
#define DFLT_CURRENT_UA    50000    // 50 mA (sign: + = charging, - = discharging)
#define DFLT_TEMP_DECIC    300      // 30.0 C
#define TICK_MS           1000      // 1s tick

struct dummy_batt {
	struct power_supply *psy;
	struct power_supply_desc desc;

	struct delayed_work sim_work;
	struct mutex lock;

	/* Simulated state */
	int capacity;          // 0..100 (%)
	int voltage_uV;        // microvolts
	int current_uA;        // microamps (signed)
	int temp_deciC;        // 0.1 C units
	bool online_ac;        // AC adapter present?
	int status;
};

static int start_capacity = DFLT_CAPACITY;    // initial SoC (%)
module_param(start_capacity, int, 0644);
MODULE_PARM_DESC(start_capacity, "Initial battery capacity in percent (0-100)");

static int discharge_rate = 1; // % per tick when discharging
module_param(discharge_rate, int, 0644);
MODULE_PARM_DESC(discharge_rate, "Capacity percent drop per second while discharging");

static int charge_rate = 2; // % per tick when charging
module_param(charge_rate, int, 0644);
MODULE_PARM_DESC(charge_rate, "Capacity percent rise per second while charging");

static bool start_charging; // start in charging state?
module_param(start_charging, bool, 0644);
MODULE_PARM_DESC(start_charging, "Start as charging (true) or discharging (false)");

static bool start_online_ac; // start with AC online?
module_param(start_online_ac, bool, 0644);
MODULE_PARM_DESC(start_online_ac, "Start with AC (mains) online (true/false)");

/* --- Property list --- */
static enum power_supply_property dummy_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,          // AC online
	POWER_SUPPLY_PROP_CAPACITY,        // %
	POWER_SUPPLY_PROP_VOLTAGE_NOW,     // uV
	POWER_SUPPLY_PROP_CURRENT_NOW,     // uA
	POWER_SUPPLY_PROP_TEMP,            // 0.1C
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_SCOPE,
};

/* --- Get properties --- */
static int dummy_get_property(struct power_supply *psy,
			      enum power_supply_property psp,
			      union power_supply_propval *val)
{
	struct dummy_batt *db = power_supply_get_drvdata(psy);

	mutex_lock(&db->lock);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = db->status;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = 1; // battery is always present
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		// represents AC adapter presence for this battery device
		val->intval = db->online_ac ? 1 : 0;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = db->capacity;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = db->voltage_uV;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = db->current_uA;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = db->temp_deciC;
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = POWER_SUPPLY_HEALTH_GOOD;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	case POWER_SUPPLY_PROP_SCOPE:
		val->intval = POWER_SUPPLY_SCOPE_SYSTEM;
		break;
	default:
		mutex_unlock(&db->lock);
		return -EINVAL;
	}

	mutex_unlock(&db->lock);
	return 0;
}

/* Optional: allow some properties to be set via sysfs writes for fun */
static int dummy_set_property(struct power_supply *psy,
			      enum power_supply_property psp,
			      const union power_supply_propval *val)
{
	struct dummy_batt *db = power_supply_get_drvdata(psy);

	mutex_lock(&db->lock);
	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		db->online_ac = val->intval ? true : false;
		db->status = db->online_ac ? POWER_SUPPLY_STATUS_CHARGING
					   : POWER_SUPPLY_STATUS_DISCHARGING;
		break;
	case POWER_SUPPLY_PROP_STATUS:
		/* Allow forcing status. Note: AC ONLINE won't auto-toggle */
		switch (val->intval) {
		case POWER_SUPPLY_STATUS_CHARGING:
		case POWER_SUPPLY_STATUS_DISCHARGING:
		case POWER_SUPPLY_STATUS_NOT_CHARGING:
		case POWER_SUPPLY_STATUS_FULL:
			db->status = val->intval;
			break;
		default:
			mutex_unlock(&db->lock);
			return -EINVAL;
		}
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		if (val->intval < 0 || val->intval > 100) {
			mutex_unlock(&db->lock);
			return -ERANGE;
		}
		db->capacity = val->intval;
		break;
	default:
		mutex_unlock(&db->lock);
		return -EINVAL;
	}
	mutex_unlock(&db->lock);

	// Tell userspace things changed
	power_supply_changed(psy);
	return 0;
}

static int dummy_property_is_writeable(struct power_supply *psy,
				       enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
	case POWER_SUPPLY_PROP_STATUS:
	case POWER_SUPPLY_PROP_CAPACITY:
		return 1;
	default:
		return 0;
	}
}

/* --- Simulation loop --- */
static void dummy_sim_work(struct work_struct *work)
{
	struct dummy_batt *db = container_of(to_delayed_work(work),
					     struct dummy_batt, sim_work);
	bool changed = false;

	mutex_lock(&db->lock);

	if (db->status == POWER_SUPPLY_STATUS_CHARGING && db->capacity < 100) {
		db->capacity += charge_rate;
		if (db->capacity >= 100) {
			db->capacity = 100;
			db->status = POWER_SUPPLY_STATUS_FULL;
		}
		changed = true;
	} else if (db->status == POWER_SUPPLY_STATUS_DISCHARGING && db->capacity > 0) {
		db->capacity -= discharge_rate;
		if (db->capacity <= 0) {
			db->capacity = 0;
			db->status = POWER_SUPPLY_STATUS_NOT_CHARGING;
		}
		changed = true;
	}

	/* Simple linear voltage model: 3.5V @0% .. 4.2V @100% */
	db->voltage_uV = 3500000 + (db->capacity * 7000); // 3.5V + 0.007V * %
	/* Current sign tracks status */
	if (db->status == POWER_SUPPLY_STATUS_CHARGING)
		db->current_uA = 80000;   // +80 mA
	else if (db->status == POWER_SUPPLY_STATUS_DISCHARGING)
		db->current_uA = -50000;  // -50 mA
	else
		db->current_uA = 0;

	mutex_unlock(&db->lock);

	if (changed)
		power_supply_changed(db->psy);

	/* Reschedule */
	schedule_delayed_work(&db->sim_work, msecs_to_jiffies(TICK_MS));
}

/* --- Platform plumbing (we create our own pdev) --- */
static struct platform_device *dummy_pdev;

static int dummy_probe(struct platform_device *pdev)
{
	struct power_supply_config cfg = {};
	struct dummy_batt *db;
	int ret;

	db = devm_kzalloc(&pdev->dev, sizeof(*db), GFP_KERNEL);
	if (!db)
		return -ENOMEM;

	mutex_init(&db->lock);

	db->desc.name = "dummy-battery";
	db->desc.type = POWER_SUPPLY_TYPE_BATTERY;
	db->desc.properties = dummy_props;
	db->desc.num_properties = ARRAY_SIZE(dummy_props);
	db->desc.get_property = dummy_get_property;
	db->desc.set_property = dummy_set_property;
	db->desc.property_is_writeable = dummy_property_is_writeable;

	/* Initial state */
	db->capacity   = clamp(start_capacity, 0, 100);
	db->voltage_uV = DFLT_VOLTAGE_UV;
	db->current_uA = DFLT_CURRENT_UA;
	db->temp_deciC = DFLT_TEMP_DECIC;
	db->online_ac  = start_online_ac;
	db->status     = start_charging ? POWER_SUPPLY_STATUS_CHARGING
					: POWER_SUPPLY_STATUS_DISCHARGING;

	cfg.drv_data = db;

	db->psy = devm_power_supply_register(&pdev->dev, &db->desc, &cfg);
	if (IS_ERR(db->psy)) {
		ret = PTR_ERR(db->psy);
		dev_err(&pdev->dev, "power_supply_register failed: %d\n", ret);
		return ret;
	}

	INIT_DELAYED_WORK(&db->sim_work, dummy_sim_work);
	schedule_delayed_work(&db->sim_work, msecs_to_jiffies(TICK_MS));

	platform_set_drvdata(pdev, db);
	dev_info(&pdev->dev, "dummy-battery registered, start_capacity=%d%%\n", db->capacity);
	return 0;
}

static void  dummy_remove(struct platform_device *pdev)
{
	struct dummy_batt *db = platform_get_drvdata(pdev);
	cancel_delayed_work_sync(&db->sim_work);
}

static struct platform_driver dummy_driver = {
	.probe  = dummy_probe,
	.remove = dummy_remove,
	.driver = {
		.name = DRV_NAME,
	},
};

static int __init dummy_init(void)
{
	int ret;

	dummy_pdev = platform_device_register_simple(DRV_NAME, -1, NULL, 0);
	if (IS_ERR(dummy_pdev))
		return PTR_ERR(dummy_pdev);

	ret = platform_driver_register(&dummy_driver);
	if (ret) {
		platform_device_unregister(dummy_pdev);
		return ret;
	}
	pr_info(DRV_NAME ": loaded\n");
	return 0;
}
module_init(dummy_init);

static void __exit dummy_exit(void)
{
	platform_driver_unregister(&dummy_driver);
	platform_device_unregister(dummy_pdev);
	pr_info(DRV_NAME ": unloaded\n");
}
module_exit(dummy_exit);

MODULE_AUTHOR("You");
MODULE_DESCRIPTION("Dummy battery power_supply driver");
MODULE_LICENSE("GPL");

