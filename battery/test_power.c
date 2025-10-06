// SPDX-License-Identifier: GPL-2.0-only
/*
 * Power supply driver for testing.
 *
 * Copyright 2010  Anton Vorontsov <cbouatmailru@gmail.com>
 *
 * Dynamic module parameter code from the Virtual Battery Driver
 * Copyright (C) 2008 Pylone, Inc.
 * By: Masashi YOKOTA <yokota@pylone.jp>
 * Originally found here:
 * http://downloads.pylone.jp/src/virtual_battery/virtual_battery-0.0.1.tar.bz2
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <generated/utsrelease.h>

enum test_power_id {
	TEST_BATTERY,
	TEST_AC,
	TEST_POWER_NUM,
};

//
//
char *enumNames[] = { 
        "POWER_SUPPLY_PROP_STATUS",
	"POWER_SUPPLY_PROP_CHARGE_TYPE",
	"POWER_SUPPLY_PROP_HEALTH",
	"POWER_SUPPLY_PROP_PRESENT",
	"POWER_SUPPLY_PROP_ONLINE",
	"POWER_SUPPLY_PROP_AUTHENTIC",
	"POWER_SUPPLY_PROP_TECHNOLOGY",
	"POWER_SUPPLY_PROP_CYCLE_COUNT",
	"POWER_SUPPLY_PROP_VOLTAGE_MAX",
	"POWER_SUPPLY_PROP_VOLTAGE_MIN",
	"POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN",
	"POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN",
	"POWER_SUPPLY_PROP_VOLTAGE_NOW",
	"POWER_SUPPLY_PROP_VOLTAGE_AVG",
	"POWER_SUPPLY_PROP_VOLTAGE_OCV",
	"POWER_SUPPLY_PROP_VOLTAGE_BOOT",
	"POWER_SUPPLY_PROP_CURRENT_MAX",
	"POWER_SUPPLY_PROP_CURRENT_NOW",
	"POWER_SUPPLY_PROP_CURRENT_AVG",
	"POWER_SUPPLY_PROP_CURRENT_BOOT",
	"POWER_SUPPLY_PROP_POWER_NOW",
	"POWER_SUPPLY_PROP_POWER_AVG",
	"POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN",
	"POWER_SUPPLY_PROP_CHARGE_EMPTY_DESIGN",
	"POWER_SUPPLY_PROP_CHARGE_FULL",
	"POWER_SUPPLY_PROP_CHARGE_EMPTY",
	"POWER_SUPPLY_PROP_CHARGE_NOW",
	"POWER_SUPPLY_PROP_CHARGE_AVG",
	"POWER_SUPPLY_PROP_CHARGE_COUNTER",
	"POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT",
	"POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX",
	"POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE",
	"POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX",
	"POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT",
	"POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX",
	"POWER_SUPPLY_PROP_CHARGE_CONTROL_START_THRESHOLD", /* in percents! */
	"POWER_SUPPLY_PROP_CHARGE_CONTROL_END_THRESHOLD", /* in percents! */
	"POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR",
	"POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT",
	"POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT",
	"POWER_SUPPLY_PROP_INPUT_POWER_LIMIT",
	"POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN",
	"POWER_SUPPLY_PROP_ENERGY_EMPTY_DESIGN",
	"POWER_SUPPLY_PROP_ENERGY_FULL",
	"POWER_SUPPLY_PROP_ENERGY_EMPTY",
	"POWER_SUPPLY_PROP_ENERGY_NOW",
	"POWER_SUPPLY_PROP_ENERGY_AVG",
	"POWER_SUPPLY_PROP_CAPACITY", /* in percents! */
	"POWER_SUPPLY_PROP_CAPACITY_ALERT_MIN", /* in percents! */
	"POWER_SUPPLY_PROP_CAPACITY_ALERT_MAX", /* in percents! */
	"POWER_SUPPLY_PROP_CAPACITY_ERROR_MARGIN", /* in percents! */
	"POWER_SUPPLY_PROP_CAPACITY_LEVEL",
	"POWER_SUPPLY_PROP_TEMP",
	"POWER_SUPPLY_PROP_TEMP_MAX",
	"POWER_SUPPLY_PROP_TEMP_MIN",
	"POWER_SUPPLY_PROP_TEMP_ALERT_MIN",
	"POWER_SUPPLY_PROP_TEMP_ALERT_MAX",
	"POWER_SUPPLY_PROP_TEMP_AMBIENT",
	"POWER_SUPPLY_PROP_TEMP_AMBIENT_ALERT_MIN",
	"POWER_SUPPLY_PROP_TEMP_AMBIENT_ALERT_MAX",
	"POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW",
	"POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG",
	"POWER_SUPPLY_PROP_TIME_TO_FULL_NOW",
	"POWER_SUPPLY_PROP_TIME_TO_FULL_AVG",
	"POWER_SUPPLY_PROP_TYPE", /* use power_supply.type instead */
	"POWER_SUPPLY_PROP_USB_TYPE",
	"POWER_SUPPLY_PROP_SCOPE",
	"POWER_SUPPLY_PROP_PRECHARGE_CURRENT",
	"POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT",
	"POWER_SUPPLY_PROP_CALIBRATE",
	"POWER_SUPPLY_PROP_MANUFACTURE_YEAR",
	"POWER_SUPPLY_PROP_MANUFACTURE_MONTH",
	"POWER_SUPPLY_PROP_MANUFACTURE_DAY",
	"POWER_SUPPLY_PROP_MODEL_NAME",
	"POWER_SUPPLY_PROP_MANUFACTURER",
	"POWER_SUPPLY_PROP_SERIAL_NUMBER",
	};
//
//
#define BLKDRV_NAME "BAT0"
#define FMT_PREFIX  ": %s[%d] "
#define DEBUG_INFO( fmt, arg...)           printk( KERN_INFO BLKDRV_NAME FMT_PREFIX fmt, __func__, __LINE__, ##arg )

#define TOTAL_LIFE_SECONDS		(3 * 60 * 60)
#define TOTAL_CHARGE			(2000 * 1000) // uAH
#define TOTAL_CHARGE_FULL_SECONDS	(60 * 60)

static int ac_online			= 1;
static int battery_status	        = POWER_SUPPLY_STATUS_CHARGING;
static int battery_level		= POWER_SUPPLY_CAPACITY_LEVEL_HIGH;
static int battery_health		= POWER_SUPPLY_HEALTH_GOOD;
static int battery_present		= 1; /* true */
static int battery_technology		= POWER_SUPPLY_TECHNOLOGY_LION;
static int battery_capacity		= 80;
static int battery_timeleft	        = TOTAL_LIFE_SECONDS;
static int battery_temperature		= 30;
static int battery_voltage	        = (4200 * 1000); // uV
static bool module_initialized;

static int test_power_get_ac_property(struct power_supply *psy,
				      enum power_supply_property psp,
				      union power_supply_propval *val)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = ac_online;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int test_power_get_battery_property(struct power_supply *psy,
					   enum power_supply_property psp,
					   union power_supply_propval *val)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = "Test battery";
		break;
	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = "Linux";
		break;
	case POWER_SUPPLY_PROP_SERIAL_NUMBER:
		val->strval = UTS_RELEASE;
		break;
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = battery_status;
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		val->intval = POWER_SUPPLY_CHARGE_TYPE_FAST;
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = battery_health;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = battery_present;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = battery_technology;
		break;
	case POWER_SUPPLY_PROP_CAPACITY_LEVEL:
		val->intval = battery_level;//

		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = battery_capacity;
		break;
	case POWER_SUPPLY_PROP_CHARGE_EMPTY:
		val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_CHARGE_NOW:
		val->intval = battery_capacity * TOTAL_CHARGE / 100;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		val->intval = TOTAL_CHARGE;
		break;
	case POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG:
		val->intval = battery_timeleft;
		break;
	case POWER_SUPPLY_PROP_TIME_TO_FULL_NOW:
		val->intval = (100 - battery_capacity) * TOTAL_CHARGE_FULL_SECONDS / 100;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = battery_temperature;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = battery_voltage;
		break;
	default:
		pr_info("%s: some properties deliberately report errors.\n",
			__func__);
		return -EINVAL;
	}

	switch( psp ){
        case POWER_SUPPLY_PROP_MODEL_NAME:
	case POWER_SUPPLY_PROP_MANUFACTURER:
	case POWER_SUPPLY_PROP_SERIAL_NUMBER:
		DEBUG_INFO( "%s -> %s", enumNames[psp],val->strval );
		break;
	default:	
		DEBUG_INFO( "%s -> %d", enumNames[psp],val->intval );
		break;
	}

	return 0;
}

static enum power_supply_property test_power_ac_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

static enum power_supply_property test_power_battery_props[] = {
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

static char *test_power_ac_supplied_to[] = {
	"BAT0",
};

static struct power_supply *test_power_supplies[TEST_POWER_NUM];

static const struct power_supply_desc test_power_desc[] = {
	[TEST_BATTERY] = {
		.name = "BAT0",
		.type = POWER_SUPPLY_TYPE_BATTERY,
		.properties = test_power_battery_props,
		.num_properties = ARRAY_SIZE(test_power_battery_props),
		.get_property = test_power_get_battery_property,
	},
	[TEST_AC] = {
		.name = "AC0",
		.type = POWER_SUPPLY_TYPE_MAINS,
		.properties = test_power_ac_props,
		.num_properties = ARRAY_SIZE(test_power_ac_props),
		.get_property = test_power_get_ac_property,

	},
};

static const struct power_supply_config test_power_configs[] = {
        {   	/* test_battery */
	},
	{
		/* test_ac */
		.supplied_to = test_power_ac_supplied_to,
		.num_supplicants = ARRAY_SIZE(test_power_ac_supplied_to),
	}, 
};

static int __init test_power_init(void)
{
	int i;
	int ret;

	BUILD_BUG_ON(TEST_POWER_NUM != ARRAY_SIZE(test_power_supplies));
	BUILD_BUG_ON(TEST_POWER_NUM != ARRAY_SIZE(test_power_configs));

	for (i = 0; i < ARRAY_SIZE(test_power_supplies); i++) {
		test_power_supplies[i] = power_supply_register(NULL,
						&test_power_desc[i],
						&test_power_configs[i]);
		if (IS_ERR(test_power_supplies[i])) {
			pr_err("%s: failed to register %s\n", __func__,
				test_power_desc[i].name);
			ret = PTR_ERR(test_power_supplies[i]);
			goto failed;
		}
	}

	module_initialized = true;
	return 0;
failed:
	while (--i >= 0)
		power_supply_unregister(test_power_supplies[i]);
	return ret;
}
module_init(test_power_init);

static void __exit test_power_exit(void)
{
	int i;

	/* Let's see how we handle changes... */
	ac_online = 0;
	battery_status = POWER_SUPPLY_STATUS_DISCHARGING;
	for (i = 0; i < ARRAY_SIZE(test_power_supplies); i++)
		power_supply_changed(test_power_supplies[i]);
	pr_info("%s: 'changed' event sent, sleeping for 10 seconds...\n",
		__func__);
	ssleep(10);

	for (i = 0; i < ARRAY_SIZE(test_power_supplies); i++)
		power_supply_unregister(test_power_supplies[i]);

	module_initialized = false;
}
module_exit(test_power_exit);



#define MAX_KEYLENGTH 256
struct battery_property_map {
	int value;
	char const *key;
};

static struct battery_property_map map_ac_online[] = {
	{ 0,  "off"  },
	{ 1,  "on" },
	{ -1, NULL  },
};

static struct battery_property_map map_health[] = {
	{ POWER_SUPPLY_HEALTH_GOOD,           "good"        },
	{ POWER_SUPPLY_HEALTH_OVERHEAT,       "overheat"    },
	{ POWER_SUPPLY_HEALTH_DEAD,           "dead"        },
	{ POWER_SUPPLY_HEALTH_OVERVOLTAGE,    "overvoltage" },
	{ POWER_SUPPLY_HEALTH_UNSPEC_FAILURE, "failure"     },
	{ -1,                                 NULL          },
};

static struct battery_property_map map_present[] = {
	{ 0,  "false" },
	{ 1,  "true"  },
	{ -1, NULL    },
};

static int map_get_value(struct battery_property_map *map, const char *key,
				int def_val)
{
	char buf[MAX_KEYLENGTH];
	int cr;

	strscpy(buf, key, MAX_KEYLENGTH);

	cr = strnlen(buf, MAX_KEYLENGTH) - 1;
	if (cr < 0)
		return def_val;
	if (buf[cr] == '\n')
		buf[cr] = '\0';

	while (map->key) {
		if (strncasecmp(map->key, buf, MAX_KEYLENGTH) == 0)
			return map->value;
		map++;
	}

	return def_val;
}


static const char *map_get_key(struct battery_property_map *map, int value,
				const char *def_key)
{
	while (map->key) {
		if (map->value == value)
			return map->key;
		map++;
	}

	return def_key;
}

static inline void signal_power_supply_changed(struct power_supply *psy)
{
	if (module_initialized)
		power_supply_changed(psy);
}

static int param_set_ac_online(const char *key, const struct kernel_param *kp)
{
	ac_online = map_get_value(map_ac_online, key, ac_online);
	signal_power_supply_changed(test_power_supplies[TEST_AC]);
	return 0;
}

static int param_get_ac_online(char *buffer, const struct kernel_param *kp)
{
	return sprintf(buffer, "%s\n",
			map_get_key(map_ac_online, ac_online, "unknown"));
}

static int param_set_battery_health(const char *key,
					const struct kernel_param *kp)
{
	battery_health = map_get_value(map_health, key, battery_health);
	signal_power_supply_changed(test_power_supplies[TEST_BATTERY]);
	return 0;
}

static int param_get_battery_health(char *buffer, const struct kernel_param *kp)
{
	return sprintf(buffer, "%s\n",
			map_get_key(map_ac_online, battery_health, "unknown"));
}

static int param_set_battery_present(const char *key,
					const struct kernel_param *kp)
{
	battery_present = map_get_value(map_present, key, battery_present);
	signal_power_supply_changed(test_power_supplies[TEST_AC]);
	return 0;
}

static int param_get_battery_present(char *buffer,
					const struct kernel_param *kp)
{
	return sprintf(buffer, "%s\n",
			map_get_key(map_ac_online, battery_present, "unknown"));
}

static const struct kernel_param_ops param_ops_ac_online = {
	.set = param_set_ac_online,
	.get = param_get_ac_online,
};

static const struct kernel_param_ops param_ops_battery_present = {
	.set = param_set_battery_present,
	.get = param_get_battery_present,
};

static const struct kernel_param_ops param_ops_battery_health = {
	.set = param_set_battery_health,
	.get = param_get_battery_health,
};

#define param_check_ac_online(name, p) __param_check(name, p, void);
#define param_check_battery_present(name, p) __param_check(name, p, void);

module_param(ac_online, ac_online, 0644);
MODULE_PARM_DESC(ac_online, "AC charging state <on|off>");

module_param(battery_present, battery_present, 0644);
MODULE_PARM_DESC(battery_present,
	"battery presence state <good|overheat|dead|overvoltage|failure>");

MODULE_DESCRIPTION("Power supply driver for testing");
MODULE_AUTHOR("Anton Vorontsov <cbouatmailru@gmail.com>");
MODULE_LICENSE("GPL");

