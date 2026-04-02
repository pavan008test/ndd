
/* Copyright (C) 2018 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Hari Seenivasan <hari.seenivasan@netradyne.com>, March 2018
 * Written by SURESH KUMAR YERAKARAJU <SURESH.KUMAR@netradyne.com>, July 2019
 */

#include <system_utils.h>
#include <unistd.h>
#include <stdio.h>
#include <syslog.h>
#include <sys/time.h>

#include <config_parser.h>
#include "nd_shutdown.h"
#include "nd_time.h"
#include <nd_factory.h>
#ifdef __cplusplus 
extern "C" {
#endif

#include <gpio_api.h>

#ifdef __cplusplus 
}
#endif

#define ROUTE_LOGS
#define BAGHEERACONFIG_INI "/home/ubuntu/.nddevice/latest/bagheera_config.ini"
using namespace std;

ND_DeviceFactory *nd_device_obj = NULL;

static const char *TAG="ND_SHDN";
static const int rtc_timeout_seconds = 20;

void set_por_gpio_local(){
    nd_device_obj->gpio_por_assert();
}

int main(int argc, char *argv[]) {
    route_logs_filename("/dev","ttyS0");
    int status=-1;
    bool is_por_engaged;
    openlog(TAG, LOG_PID|LOG_CONS|LOG_NDELAY, LOG_USER);
    syslog(LOG_INFO, "#####STARTING ND_SHUTDOWN#####");
    LOG_I(TAG, "#####STARTING ND_SHUTDOWN#####");

    nd_device_obj_init();

    bool do_shutdown = (nd_device_obj->get_crank_level() != CRANK_HIGH);
    
    if(argc > 1 )
    {
	    bool shutdown_request = ( (strncmp(argv[1],"1",4) == 0) ? true : false );
	    LOG_I(TAG, "Parameter: %s, %d", argv[1], atoi(argv[1]));
	    //do_shutdown &= shutdown_request; 
	    do_shutdown = false;
	    syslog(LOG_INFO, "Shutdown Request is %x",  shutdown_request);
	    LOG_I(TAG, "Shutdown Request is  %x", shutdown_request);
    }

    syslog(LOG_INFO, "Shutdown Status is %x",  do_shutdown);
    LOG_I(TAG, "Shutdown Status is  %x", do_shutdown);

    
    //BAG3C Code for AON RTC Enable for Shutdowns. RTC Time is Set in power_monitor->initiate_shutdown()
    if( true == nd_device_obj->is_wake_on_motion_supported()) {

	    enable_pmic_wdt(TAG, SHORT_TIME, false);// as AON is handling the WAKE UP OF LANAI In BAGHEERA3, we should enable the PMIC for reboot

	    Config_parser bag_conf(BAGHEERACONFIG_INI);
	    bool get_override_val = true, val_overridden = false;
	    string wom_enable_str( bag_conf.getConfig("apm","apm_wom_enable", "true", get_override_val, val_overridden) );
	    string ign_enable_str( bag_conf.getConfig("apm","enable_ignition_based_wakeup", "true", get_override_val, val_overridden) );

	    bool ret_wom = false;
	    if( !strncmp(wom_enable_str.c_str(), "true", 4) || !strncmp(ign_enable_str.c_str(), "false",5) ) {
		    ret_wom = nd_device_obj->configure_imu_wom(true);
	    } else {
		    ret_wom = nd_device_obj->configure_imu_wom(false);
	    }

	    if( false == ret_wom ) {
		    LOG_E(TAG, " WOM Configuration Failed!!!");
	    }
	    // Just does a Enable of AON RTC, without modifying the rtc time which is set by power_monitor
	    if( false == nd_device_obj->configure_rtc_time( AON_RTC_ENABLE, AON_RTC_ENABLE ) )
	    {
		    LOG_E(TAG, " AON RTC Enable Failed!!!");
	    }
	    //LTC3350. Disable SuperCap Hysterisis, so that it won't draw current in shutdown state when connected to battery/power source.
	    // Re-Enable during StartUp
	    LOG_C(TAG,"Disabling Supercap Charging Cycle to reduce Current Draw during Shutdown");
	    system_execute(TAG, "i2cset -f -y 0 0x09 0x05 0x0000 w");
    } else {
	    enable_pmic_wdt(TAG, SHORT_TIME, (do_shutdown));
    }
    
// Dont wakeup for deliberate shut downs like low voltage/high temperature
#if 1
    is_por_engaged = system_chk_por_engaged(nd_device_obj->get_engage_shutdown_path());
    syslog(LOG_INFO, "Check engage por status %x", is_por_engaged);
    LOG_I(TAG, "Check engage por status %x", is_por_engaged);
    if (!is_por_engaged)
    {
        syslog(LOG_INFO, "#####Exiting ND_SHUTDOWN#####");
        LOG_C(TAG, "#####Exiting ND_SHUTDOWN#####");
        LOG_C(TAG, "No need to set wakeup alarm");
        return 0;
    }
#endif

    if ( do_shutdown ) {
        syslog(LOG_INFO, "crank level != CRANK_HIGH, not setting any alarm");
        LOG_I(TAG, "crank level != CRANK_HIGH, not setting any alarm");
        return 0;
    }
    // set set_RTC_time if get_crank_level() == CRANK_HIGH
    // pull GPIO to immediatley syart the device incase of crank high
#if 1
    syslog(LOG_ALERT, "Set gpio for por ");
    LOG_W(TAG, "Set gpio for por ");
    closelog();
    set_por_gpio_local();
    LOG_W(TAG, "SHOULD HAVE REBOOTED");
#endif

    return 0;
}
