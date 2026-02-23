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
#include <linux/reboot.h>
#include <generated/utsrelease.h>


#define VERSION_MAJOR   1
#define VERSION_MINOR   0
#define VERSION_EDIT    2

enum test_power_id {
    ONEUP_BATTERY,
    ONEUP_AC,
    ONEUP_POWER_NUM,
};

//
// Useful definitions.  Note that the TOTAL_* definitions need to be worked out...
//
#define DRV_NAME                    "oneUpPower"
#define PR_INFO( fmt, arg...)       printk( KERN_INFO DRV_NAME ": " fmt, ##arg )
#define PR_ERR( fmt, arg... )       printk( KERN_ERR DRV_NAME ": " fmt, ##arg )
#define TOTAL_LIFE_SECONDS          (6 * 60 * 60)       // Time in seconds
#define TOTAL_CHARGE                (4800 * 1000)       // Power in micro Amp Hours, uAH
#define TOTAL_CHARGE_FULL_SECONDS   (((2*60)+30) * 60)  // Time to full charge in seconds


//
// I2C Addresses
//
#define I2C_BUS             0x01
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
// Needed data structures
//
struct PowerStatus {
    int status;         // Status of the power supply
    int capacity;       // Capacity in percentage
    int capacity_level; // What level are we at, CRITICAL,LOW,NORMAL,HIGH,FULL
    int health;         // State of the battery
    int present;        // Is the battery present (always YES)
    int technology;     // What technology is the battery (LION)
    int timeleft;       // How much time to we have left in seconds
    int temperature;    // What is the battery temperature
    int voltage;        // What is the current voltage of the battery

} battery = {
    .status             = POWER_SUPPLY_STATUS_DISCHARGING,
    .capacity           = 100,
    .capacity_level     = POWER_SUPPLY_CAPACITY_LEVEL_HIGH,
    .health             = POWER_SUPPLY_HEALTH_GOOD,
    .present            = 1,
    .technology         = POWER_SUPPLY_TECHNOLOGY_LION,
    .timeleft           = TOTAL_LIFE_SECONDS,
    .temperature        = 300,          // tenths of °C; 300 = 30.0°C
    .voltage            = (4200 * 1000), // uV
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

//
// Globals
//
static int soc_shutdown                 = 5;         // Default setting is 5% of power left for critical
static int ac_online                    = 1;         // Are we connected to an external power source?
static bool module_initialized          = false;     // Has the driver been initialized?
static struct task_struct *monitor_task = NULL;      // Place to store the monito task...

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
    {   /* battery */
    },
    {
        /* ac */
        .supplied_to = ac_power_supplied_to,
        .num_supplicants = ARRAY_SIZE(ac_power_supplied_to),
    },
};

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
    else if( capacity > 75 ){
        battery.capacity_level = POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;
    }
    else if( capacity > 40 ){
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
// Returns:
//     True   - If the system is plugged in
//     False  - If we are soley on battery power
//
static int check_ac_power( struct i2c_client *client )
{
    int current_high;
    int plugged_in;

    current_high = i2c_smbus_read_byte_data( client, CURRENT_HIGH_REG );

    //
    // Bit 7 of the high byte: 1 = discharging (no AC), 0 = charging (AC present)
    //
    if( (current_high & 0x80) == 0x80 ){
        plugged_in = 0;
    }
    else{
        plugged_in = 1;
    }

    if( ac_online != plugged_in ){
        ac_online = plugged_in;
        set_power_states();
        if( ac_online ){
            PR_INFO( "AC Power is connected.\n" );
        }
        else {
            PR_INFO( "AC Power is disconnected.\n" );
        }
        power_supply_changed( power_supplies[ONEUP_AC] );
    }

    return plugged_in;
}

//
// check_battery_state
//
// Determine that the current state of the battery is
//
// Parameters:
//     client  - I2C device used to get information
//
// Returns:
//     Battery State of Charge in Percentage
//
static int check_battery_state( struct i2c_client *client )
{
    int SOCPercent;

    SOCPercent = i2c_smbus_read_byte_data( client, SOC_HIGH_REG );
    if( SOCPercent > 100 ){
        SOCPercent = 100;
    }
    if( SOCPercent < 0 ){
        SOCPercent = 0;
    }

    if( battery.capacity != SOCPercent ){
        battery.capacity = SOCPercent;
        set_power_states();
        PR_INFO( "Battery State of charge is %d%%\n",SOCPercent );
        power_supply_changed( power_supplies[ONEUP_BATTERY] );
    }

    return SOCPercent;
}

//
// shutdown_helper
//
// Shutdown the system when we are critically low on power and not
// plugged in.
//
static void shutdown_helper( void ){
    orderly_poweroff( false );
}

//
// restart_battery_ic
//
// Cycle the CW2217 control register to bring it out of sleep or error state,
// then poll REG_ICSTATE until the IC reports it is ready (bits [3:2] non-zero).
//
// Parameters:
//     client - I2C client for the battery controller
//
// Returns:
//     0          - IC is active and ready
//     -EINTR     - kthread stop was requested during the wait
//     -ETIMEDOUT - IC did not become ready after retries
//
static int restart_battery_ic( struct i2c_client *client )
{
    int icstate;
    int attempt;
    int wait;

    for( attempt = 0; attempt < 3; attempt++ ) {
        if( kthread_should_stop() )
            return -EINTR;

        i2c_smbus_write_byte_data( client, REG_CONTROL, CTRL_RESTART );
        msleep( 500 );
        i2c_smbus_write_byte_data( client, REG_CONTROL, CTRL_ACTIVE );
        msleep( 500 );

        for( wait = 0; wait < 5; wait++ ) {
            if( kthread_should_stop() )
                return -EINTR;
            icstate = i2c_smbus_read_byte_data( client, REG_ICSTATE );
            if( icstate >= 0 && (icstate & 0x0C) != 0 ) {
                PR_INFO( "Battery IC activated.\n" );
                return 0;
            }
            msleep( 1000 );
        }
    }

    PR_ERR( "Battery IC did not become ready.\n" );
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
// Parameters:
//     client - I2C client for the battery controller
//
// Returns:
//     0  - Profile is valid (already matched or successfully updated)
//    <0  - I2C error, or IC failed to restart after programming
//
static int init_battery_profile( struct i2c_client *client )
{
    int  control;
    int  socalert;
    int  val;
    int  i;
    int  ret;
    bool profile_ok = false;

    PR_INFO( "Checking battery profile...\n" );

    //
    // IC is active when REG_CONTROL reads back 0
    //
    control = i2c_smbus_read_byte_data( client, REG_CONTROL );
    if( control == 0 ) {
        // IC is up; check if the profile-loaded flag is set
        socalert = i2c_smbus_read_byte_data( client, REG_SOCALERT );
        if( socalert >= 0 && (socalert & 0x80) != 0 ) {
            // Flag set; verify every profile byte
            profile_ok = true;
            for( i = 0; i < ARRAY_SIZE(battery_profile); i++ ) {
                val = i2c_smbus_read_byte_data( client, REG_PROFILE + i );
                if( val < 0 || (u8)val != battery_profile[i] ) {
                    PR_INFO( "Battery profile mismatch at byte %d.\n", i );
                    profile_ok = false;
                    break;
                }
            }
        }
    }

    if( profile_ok ) {
        PR_INFO( "Battery profile is valid.\n" );
        return 0;
    }

    PR_INFO( "Programming battery profile...\n" );

    // Restart then sleep the IC before writing
    ret = i2c_smbus_write_byte_data( client, REG_CONTROL, CTRL_RESTART );
    if( ret < 0 ) {
        PR_ERR( "Failed to restart IC before profile write: %d\n", ret );
        return ret;
    }
    msleep( 500 );

    ret = i2c_smbus_write_byte_data( client, REG_CONTROL, CTRL_SLEEP );
    if( ret < 0 ) {
        PR_ERR( "Failed to sleep IC before profile write: %d\n", ret );
        return ret;
    }
    msleep( 500 );

    // Write the 80-byte battery model profile
    for( i = 0; i < ARRAY_SIZE(battery_profile); i++ ) {
        ret = i2c_smbus_write_byte_data( client, REG_PROFILE + i, battery_profile[i] );
        if( ret < 0 ) {
            PR_ERR( "Failed to write profile byte %d: %d\n", i, ret );
            return ret;
        }
    }

    // Mark profile as loaded
    ret = i2c_smbus_write_byte_data( client, REG_SOCALERT, 0x80 );
    if( ret < 0 ) {
        PR_ERR( "Failed to set profile flag: %d\n", ret );
        return ret;
    }
    msleep( 500 );

    // Disable IC interrupts
    ret = i2c_smbus_write_byte_data( client, REG_GPIOCONFIG, 0x00 );
    if( ret < 0 ) {
        PR_ERR( "Failed to configure GPIO: %d\n", ret );
        return ret;
    }
    msleep( 500 );

    // Restart and wait for the IC to become ready
    ret = restart_battery_ic( client );
    if( ret != 0 ) {
        PR_ERR( "Battery IC failed to restart after profile update.\n" );
        return ret;
    }

    PR_INFO( "Battery profile updated successfully.\n" );
    return 0;
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
    int    soc;
    int    plugged_in;
    bool   profile_ready        = false;

    PR_INFO( "Starting system monitor...\n" );

    while( true ){
        //
        // Get an adapter so we can make an i2c client...
        //
        if( adapter == NULL ){
            //
            // get an adapter
            //
            set_current_state( TASK_INTERRUPTIBLE );
            adapter = i2c_get_adapter( I2C_BUS );
            PR_INFO( "Adapter = %p\n",adapter);
        }
        else if( client == NULL ){
            //
            // Get a i2c client
            //
            set_current_state( TASK_INTERRUPTIBLE );
            client = i2c_new_client_device( adapter, &board_info );
            PR_INFO( "Client = %p\n",client);
        }
        else if( !profile_ready ){
            //
            // One-time battery IC profile initialisation.  Must happen before
            // the first SOC read; without it the CW2217 may report inaccurate
            // percentages.  If init fails we warn and proceed: the IC may
            // already have a valid profile from a prior load.
            //
            set_current_state( TASK_INTERRUPTIBLE );
            if( init_battery_profile( client ) < 0 )
                PR_ERR( "Profile init failed; SOC readings may be inaccurate.\n" );
            profile_ready = true;
        }
        else{
            set_current_state( TASK_UNINTERRUPTIBLE );
            if( kthread_should_stop() ){
                break;
            }

            plugged_in = check_ac_power( client );
            soc        = check_battery_state( client );

            set_current_state( TASK_INTERRUPTIBLE );
            if( !plugged_in && (soc < soc_shutdown) ){
                // not pluggged in and below critical state, shutdown
                PR_INFO( "Performing system shutdown unplugged and power is at %d\n",soc);
                shutdown_helper();
            }
        }
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

    PR_INFO( "System monitor is stopping...\n" );
    return 0;
}

//
// get_ac_property
//
// When the value of a property is requested, this routine is called, and the
// property is looked up and its value reuturned.
//
// Parameters:
//     pst  - The power supply object
//     psp  - The property we are looking for (as an integer)
//     val  - A pointer to where the data should be stored.
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
//     pst  - The power supply object
//     psp  - The property we are looking for (as a integer)
//     val  - A pointer to where th data should be stored.
//
// Returns:
//     -EINVAL  - No such property, or not supported
//     0        - Succssfully located the data
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
            PR_INFO("%s: some properties deliberately report errors.\n",__func__);
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
//     pst  - The power supply object
//     psp  - The property we are looking for (as an integer)
//     val  - A pointer to where the data should be stored.
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

    PR_INFO( "Starting Power monitor version %d.%d.%d",VERSION_MAJOR,VERSION_MINOR,VERSION_EDIT );
    BUILD_BUG_ON(ONEUP_POWER_NUM != ARRAY_SIZE(power_supplies));
    BUILD_BUG_ON(ONEUP_POWER_NUM != ARRAY_SIZE(power_configs));

    for (i = 0; i < ARRAY_SIZE(power_supplies); i++) {
        power_supplies[i] = power_supply_register(NULL,
                                                  &power_descriptions[i],
                                                  &power_configs[i]);
        if (IS_ERR(power_supplies[i])) {
            PR_ERR("%s: failed to register %s\n", __func__, power_descriptions[i].name);
            ret = PTR_ERR(power_supplies[i]);
            goto failed;
        }
    }

    monitor_task = kthread_run( system_monitor, NULL, "argon40_monitor" );
    if( monitor_task == NULL ){
        PR_ERR( "Could not start system_monitor, terminating.\n" );
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

    while (--i >= 0){
        power_supply_unregister(power_supplies[i]);
    }

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

    for (i = 0; i < ARRAY_SIZE(power_supplies); i++){
        power_supply_changed(power_supplies[i]);
    }

    //PR_INFO("%s: 'changed' event sent, sleeping for 10 seconds...\n", __func__);
    //ssleep(10);

    for (i = 0; i < ARRAY_SIZE(power_supplies); i++){
        power_supply_unregister(power_supplies[i]);
    }

    module_initialized = false;
}
module_exit(oneup_power_exit);

static int param_set_soc_shutdown( const char *key, const struct kernel_param *kp )
{
    long soc;

    if( kstrtol( key, 10, &soc  ) == 0 ){
        if( soc == 0 ){
            PR_INFO( "Disabling automatic shutdown when battery is below threshold.\n");
            soc_shutdown = 0;
            return 0;
        }
        else if( (soc >= 1) && (soc <= 20)){
            PR_INFO( "Changing automatic shutdown when battery is below %ld%%\n",soc);
            soc_shutdown = soc;
            return 0;
        } else {
            PR_INFO( "Invalid value (%ld%%), please change to: 0 to disable, 1-20 to set shutdown threshold.\n",soc );
            return 0;
        }
    } else {
        PR_INFO( "Could not convert to integer\n" );
    }
    return -ENOENT;
}

static int param_get_soc_shutdown( char *buffer, const struct kernel_param *kp )
{
    return sprintf( buffer, "%d", soc_shutdown );
}

static const struct kernel_param_ops param_ops_soc_shutdown = {
    .set = param_set_soc_shutdown,
    .get = param_get_soc_shutdown,
};

#define param_check_soc_shutdown(name,p) __param_check(name,p,void);
module_param( soc_shutdown, soc_shutdown, 0644 );
MODULE_PARM_DESC(soc_shutdown, "Shutdown system when the battery state of charge is lower than this value.");

MODULE_DESCRIPTION("Power supply driver for Argon40 1UP");
MODULE_AUTHOR("Jeff Curless <jeff@thecurlesses.com>");
MODULE_LICENSE("GPL");

