// SPDX-License-Identifier: GPL-2.0-only
/*
 * This driver was writtent to support the Argon40 1UP laptop.  I wanted to make sure that we could properly use the battery plugin.
 *
 * Author:  Jeff Curless
 *
 */

/*
 * Based heavily on:
 * https://git.kernel.org/cgit/linux/kernel/git/stable/linux-stable.git/tree/drivers/power/test_power.c?id=refs/tags/v4.2.6/
 */

#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <generated/utsrelease.h>

enum test_power_id {
	ONEUP_BATTERY,
	ONEUP_AC,
	ONEUP_POWER_NUM,
};

//
// Useful definitions.  Note that the TOTAL_* definitions need to be worked out...
//
#define BLKDRV_NAME 			"oneUpPower"
#define TOTAL_LIFE_SECONDS		(3 * 60 * 60)		// Time in seconds
#define TOTAL_CHARGE			(4800 * 1000) 		// Power in micro Amp Hours, uAH
#define TOTAL_CHARGE_FULL_SECONDS	(60 * 60)		// Time to full charge in seconds

//
// I2C Addresses
//
#define I2C_BUS			0x01
#define BATTERY_ADDR	        0x64
#define CURRENT_HIGH_REG	0x0E
#define CURRENT_LOW_REG		0x0F
#define SOC_HIGH_REG		0x04
#define SOC_LOW_REG		0x05

//
// Needed data structures
//
struct PowerStatus {
    int status;			// Status of the power supply
    int capacity;		// Capacity in percentage
    int capacity_level;		// What level are we at, CRITICAL,LOW,NORMAL,HIGH,FULL
    int health;			// State of the battery
    int present;		// Is the battery present (always YES)
    int technology;		// What technology is the battery (LION)
    int timeleft;		// How much time to we have left in seconds
    int temperature;		// What is the battery temperature
    int voltage;		// What is the current voltage of the battery
 
} battery = {
	.status   	= POWER_SUPPLY_STATUS_DISCHARGING,
	.capacity 	= 90,
        .capacity_level = POWER_SUPPLY_CAPACITY_LEVEL_HIGH,
	.health         = POWER_SUPPLY_HEALTH_GOOD,
	.present        = 1,
	.technology     = POWER_SUPPLY_TECHNOLOGY_LION,
	.timeleft       = TOTAL_LIFE_SECONDS,
	.temperature    = 30,
	.voltage        = (4200 * 1000), // uV
};

//
// Forward declairations
//
static int get_battery_property(struct power_supply *psy,
		                enum power_supply_property psp,
				union power_supply_propval *val);
static int get_ac_property(struct power_supply *psy,
		           enum power_supply_property psp,
			   union power_supply_propval *val );

static int ac_online			= 1;	     // Are we connected to an external power source?
static bool module_initialized 		= false;		// Has the driver been initialized?
static struct task_struct *monitor_task = NULL;

//
// Properties for AC
//
static enum power_supply_property power_ac_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

//
// Properties supported to the Battery
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

//
// What Battery does the the AC object supply power to...
//
static char *ac_power_supplied_to[] = {
	"BAT0",
};

//
// All of the power supplies we are registering
//
static struct power_supply *power_supplies[ONEUP_POWER_NUM];

//
// The power descriptions for the supplies
//
static const struct power_supply_desc power_descriptions[] = {
	[ONEUP_BATTERY] = {
		.name = "BAT0",
		.type = POWER_SUPPLY_TYPE_BATTERY,
		.properties = power_battery_props,
		.num_properties = ARRAY_SIZE(power_battery_props),
		.get_property = get_battery_property,
	},
	[ONEUP_AC] = {
		.name = "AC0",
		.type = POWER_SUPPLY_TYPE_MAINS,
		.properties = power_ac_props,
		.num_properties = ARRAY_SIZE(power_ac_props),
		.get_property = get_ac_property,

	},
};

//
// Configurations
//
static const struct power_supply_config power_configs[] = {
        {   	/* battery */
	},
	{
		/* ac */
		.supplied_to = ac_power_supplied_to,
		.num_supplicants = ARRAY_SIZE(ac_power_supplied_to),
	}, 
};

//
// Potentially a method to shutdown the system when the battery is really low...
//
// static const char * const shutdown_argv[] = 
//    { "/sbin/shutdown", "-h", "-P", "now", NULL };
// call_usermodehelper(shutdown_argv[0], shutdown_argv, NULL, UMH_NO_WAIT);
//

//
// set_power_states 
//
// Given the current state of the capacity and status of the AC plug, 
// make sure we normalize the data associated with those levels.
//
static void set_power_states( void )
{
    int capacity = battery.capacity;
    
    if( capacity > 95 ){
        battery.capacity_level = POWER_SUPPLY_CAPACITY_LEVEL_FULL;
    }
    else if( capacity > 85 ){
	battery.capacity_level = POWER_SUPPLY_CAPACITY_LEVEL_HIGH;
    }
    else if( capacity > 40 ){
	battery.capacity_level = POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;
    }
    else if( capacity > 30 ){
	battery.capacity_level = POWER_SUPPLY_CAPACITY_LEVEL_LOW;
    }
    else {
	battery.capacity_level = POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL;
    }

    if( ac_online ){
        if( capacity > 95 ){
	    battery.status = POWER_SUPPLY_STATUS_FULL;
	}
	else {
	    battery.status = POWER_SUPPLY_STATUS_CHARGING;
	}
    }
    else {
        battery.status = POWER_SUPPLY_STATUS_DISCHARGING;
    }
}

//
// check_ac_power
//
// Check to see if the AC plug is connected or not.
//
// Parameters:
//     client - A i2c object that is used to get data from the I2C bus.
//
static void check_ac_power( struct i2c_client *client )
{
    int current_high;
    int plugged_in;

    current_high = i2c_smbus_read_byte_data( client, CURRENT_HIGH_REG );
    if( (current_high & 0x80) > 0 ){
        plugged_in = 0;
    }
    else{
	plugged_in = 1;
    }
    
    if( ac_online != plugged_in ){
        ac_online = plugged_in;
	set_power_states();
	//power_supply_changed( power_supplies[ONEUP_AC] );
    }
}

//
// check_battery_state
//
// Determine that the current state of the battery is
//
// Parameters:
//     client	- I2C device used to get information
//
static void check_battery_state( struct i2c_client *client )
{
    int SOCPercent;

    SOCPercent = i2c_smbus_read_byte_data( client, SOC_HIGH_REG );
    if( SOCPercent > 100 )
        SOCPercent = 100;
    if( SOCPercent < 0 )
	SOCPercent = 0;

    if( battery.capacity != SOCPercent ){
        battery.capacity = SOCPercent;
	set_power_states();
	power_supply_changed( power_supplies[ONEUP_BATTERY] );
    }
}

//
// system_monitor
//
// Monitor the power system associated with the laptop.  Need to monitor the AC line (is it plugged in or not),
// and the current capacity of the battery.
//
// This code is called via a kernel thread, and executes approximatly once a second.  This timing can be modified,
// however it should probably not be faster.
//
// Note:  The python code has some additional code that inspects the I2C device and profile.  This code will 
// pobably need to be added here.  The issue is it appears to be quite timing sensitive.
//
// Parameters:
//     args - Not used.
//
// Returns:
//     -1   - if there was an error
//     0    - no errors.
//
static int system_monitor( void *args )
{
    struct i2c_client  *client  = NULL;
    struct i2c_adapter *adapter = NULL;
    struct i2c_board_info board_info = {I2C_BOARD_INFO("argon40_battery", BATTERY_ADDR )};

    pr_info( "Starting system monitor...\n" );
 
    //
    // Get an adapter so we can make an i2c client...
    //
    adapter = i2c_get_adapter( I2C_BUS );
    if( adapter == NULL ){
        pr_err( "Unable to get i2c adapter!\n" );
	return -1;
    }
    pr_info( "Created an I2C adapter...\n" );

    //
    // Build the i2c client...
    //
    client = i2c_new_client_device( adapter, &board_info );
    if( client == NULL ){
        pr_err( "Unable to create i2c client!\n" );
	return -1;
    }

    pr_info( "Created an I2C client device...\n" );

    //
    // Monitor until we are done...
    //
    while( true ){
        set_current_state( TASK_UNINTERRUPTIBLE );
	if( kthread_should_stop() ) 
	    break;

        check_ac_power( client );
	check_battery_state( client );

	set_current_state( TASK_INTERRUPTIBLE );
	schedule_timeout( HZ );
    }

    //
    // Cleanup
    //
    if( client )
    {
        i2c_unregister_device( client );
        client = NULL;
    }

    if( adapter )
    {
	i2c_put_adapter( adapter );
	adapter = NULL;
    }

    pr_info( "System monitor is stopping...\n" );
    return 0;
}

//
// get_ac_property
//
// When the value of a property is requested, this routine is called, and the
// property is looked up and its value reuturned.
//
// Parameters:
//     pst	- The power supply object
//     psp	- The property we are looking for (as an integer)
//     val	- A pointer to where the data should be stored.
//
// Returns:
//     -EINVAL  - No such property, or not supported
//     0        - Successfuly located the data
//
static int get_ac_property(struct power_supply *psy,
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

//
// get_battery_int_property
//
// When the value of a property is requested, this routine is called, and
// the property is looked up and its value returned.
//
// This particular function simple returns the integer properties.
//
// Parmters:
//     pst	- The power supply object
//     psp	- The property we are looking for (as a integer)
//     val	- A pointer to where th data should be stored.
//
// Returns:
//     -EINVAL	- No such property, or not supported
//     0	- Succssfully located the data
//
static int get_battery_int_property( struct power_supply *psy,
		                     enum power_supply_property psp,
				     union power_supply_propval *val )
{
    switch( psp ) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = battery.status;
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		val->intval = POWER_SUPPLY_CHARGE_TYPE_FAST;
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = battery.health;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = battery.present;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = battery.technology;
		break;
	case POWER_SUPPLY_PROP_CAPACITY_LEVEL:
		val->intval = battery.capacity_level;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = battery.capacity;
		break;
	case POWER_SUPPLY_PROP_CHARGE_EMPTY:
		val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_CHARGE_NOW:
		val->intval = battery.capacity * TOTAL_CHARGE / 100;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		val->intval = TOTAL_CHARGE;
		break;
	case POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG:
		val->intval = battery.timeleft;
		break;
	case POWER_SUPPLY_PROP_TIME_TO_FULL_NOW:
		val->intval = (100 - battery.capacity) * TOTAL_CHARGE_FULL_SECONDS / 100;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = battery.temperature;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = battery.voltage;
		break;
	default:
		pr_info("%s: some properties deliberately report errors.\n",
			__func__);
		return -EINVAL;
	}

	return 0;
}

//
// get_battery_property
//
// When the value of a property is requested, this routine is called, and the
// property is looked up and its value reuturned.
//
// Parameters:
//     pst	- The power supply object
//     psp	- The property we are looking for (as an integer)
//     val	- A pointer to where the data should be stored.
//
// Returns:
//     -EINVAL  - No such property, or not supported
//     0        - Successfuly located the data
//
static int get_battery_property(struct power_supply *psy,
			        enum power_supply_property psp,
				union power_supply_propval *val)
{
    switch (psp) {
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
	    return get_battery_int_property( psy, psp, val );
    }

    return 0;
}

//
// oneup_power_init
//
// Initialization code for the driver
//
// Returns:
//     0 - On success
//    -1 - Failure
//
static int __init oneup_power_init(void)
{
	int i;
	int ret;

	pr_info( "Starting Power monitor..." );
	BUILD_BUG_ON(ONEUP_POWER_NUM != ARRAY_SIZE(power_supplies));
	BUILD_BUG_ON(ONEUP_POWER_NUM != ARRAY_SIZE(power_configs));

	for (i = 0; i < ARRAY_SIZE(power_supplies); i++) {
		power_supplies[i] = power_supply_register(NULL,
						&power_descriptions[i],
						&power_configs[i]);
		if (IS_ERR(power_supplies[i])) {
			pr_err("%s: failed to register %s\n", __func__,
				power_descriptions[i].name);
			ret = PTR_ERR(power_supplies[i]);
			goto failed;
		}
	}

	monitor_task = kthread_run( system_monitor, NULL, "argon40_monitor" );
	if( monitor_task == NULL ){
	    pr_err( "Could not start system_monitor, terminating.\n" );
            ret = -EINVAL;
	    goto failed;
	}

	set_power_states();
	module_initialized = true;
	return 0;
failed:
	if( monitor_task ){
            kthread_stop( monitor_task );
	    monitor_task = NULL;
	}

	while (--i >= 0)
	    power_supply_unregister(power_supplies[i]);
   
	return ret;
}
module_init(oneup_power_init);

//
// oneup_power_exit
//
// Called when the driver exists
//
static void __exit oneup_power_exit(void)
{
	int i;

	//
	// First up, stop the monitor task as its using resources
	//
	if( monitor_task ){
	    kthread_stop( monitor_task );
	    monitor_task = NULL;
	}

	/* Let's see how we handle changes... */
	ac_online = 0;
	battery.status = POWER_SUPPLY_STATUS_DISCHARGING;

	for (i = 0; i < ARRAY_SIZE(power_supplies); i++)
	    power_supply_changed(power_supplies[i]);
	
	pr_info("%s: 'changed' event sent, sleeping for 10 seconds...\n", __func__);
	ssleep(10);

	for (i = 0; i < ARRAY_SIZE(power_supplies); i++)
	    power_supply_unregister(power_supplies[i]);

	module_initialized = false;
}
module_exit(oneup_power_exit);

MODULE_DESCRIPTION("Power supply driver for Argon40 1UP");
MODULE_AUTHOR("Jeff Curless <jeff@thecurlesses.com>");
MODULE_LICENSE("GPL");

