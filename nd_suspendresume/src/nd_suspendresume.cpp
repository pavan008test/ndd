/* Copyright (C) 2021 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Shravan Kumar M <shravan.kumar@netradyne.com>, July 2021
 */

#include "log.h"
#include "nd_time.h"
#include <nd_task.h>
#include "nd_factory.h"
#include <string>
#include <sstream>
#include <list>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <system_utils.h>
#include "service_utils.h"
#include <future>
#include <led_utils.h>
#include <nd_gpio.h>
#include <config_parser.h>
#include "nd_file_utils.h"

#define BAGHEERACONFIG_INI "/home/ubuntu/.nddevice/latest/bagheera_config.ini"

//NDService *nd_service_obj; //nd service object, to detect critical Errors which will be send to Health stats and cloud
ND_DeviceFactory *nd_device_obj = NULL;
using namespace std;

#define TAG "STNDBY"
static const int SUSPEND_RETRY_COUNT = 3;
static const int64_t SUSPEND_ACTIVATING_TIMEOUT_MS = 60*1000; // Timeout for activating detection
static const string log_dir = "/home/ubuntu/.nddevice/log/nd_suspendresume";

static const string vendor_standby_entry_script = "/etc/init.d/sc7_entry.sh";
static const string vendor_standby_exit_script = "/etc/init.d/sc7_exit.sh";

static string standby_entry_cmds [] = {
        "systemctl stop cam_rec.service",
        "systemctl stop bagheera.service",
        "systemctl stop awsiot.service",
        "systemctl stop scheduler_manager.service",
        "systemctl stop circular_buffer.service",
        "systemctl stop diagnostic.service",
        "systemctl stop uploader.service",
        "systemctl stop speed.service",
        "systemctl stop obd.service",
        "systemctl stop ext_cam.service",
        "systemctl stop installer_app.service",
        "systemctl stop nd_bt.service",
        "systemctl stop time_sync.service",
        "systemctl stop conn_mgr.service",
        "systemctl stop wifi_mgr.service",
        "systemctl stop power_monitor.service",
        "systemctl stop analyticsService.service",
        "systemctl stop outwardAnalyticsClient.service",
        "systemctl stop canAnalyticsClient.service",
        "systemctl stop deviceHealthClient.service",
        "systemctl stop inwardAnalyticsClient.service",
        "systemctl stop unifiedAnalyticsClient.service",
        "systemctl stop audioPlayback.service",
        "systemctl stop HealthStatsManager.service",
        "systemctl stop svc.service",
        "systemctl stop service_mon.service",
        "systemctl stop cron.service",
        "systemctl stop haveged.service",
        "systemctl stop systemd-resolved.service",
        "systemctl stop apm.service",
        "systemctl stop gps.service",
        "systemctl stop nd_dta.service",
        "systemctl stop nd_sam.service",
        "sleep 2",
        "systemctl stop nvargus-daemon.service",
        "rm -rf /run/crond.reboot",
        "rm -rf /dev/shm/nd_files_c",
        "rm -rf /dev/shm/MSGQ",
        "rm -f /dev/shm/*.bin",
        "sync"
};

static string standby_exit_cmds [] = {
//        "mkdir -p /dev/shm/nd_files_c/",
//        "touch /dev/shm/nd_files_c/keepaliveresponse.txt",
//        "chmod 666 /dev/shm/nd_files_c/keepaliveresponse.txt",
        "sleep 2",
        "systemctl start haveged.service",
        "systemctl start systemd-resolved.service",
        "systemctl start cron.service",
        "systemctl start time_sync.service",
        "systemctl start power_monitor.service",
        "systemctl start svc.service",
        "systemctl start service_mon.service",
        "systemctl start nvargus-daemon.service",
        "systemctl start awsiot.service",
        "systemctl start circular_buffer.service",
        "systemctl start uploader.service",
        "systemctl start diagnostic.service",
        "systemctl start speed.service",
        "systemctl start obd.service",
        "systemctl start wifi_mgr.service",
        "systemctl start conn_mgr.service",
        "systemctl start installer_app.service",
        "systemctl start nd_bt.service",
        "systemctl start ext_cam.service",
        "systemctl start analyticsService.service",
        "systemctl start outwardAnalyticsClient.service",
        "systemctl start canAnalyticsClient.service",
        "systemctl start deviceHealthClient.service",
        "systemctl start inwardAnalyticsClient.service",
        "systemctl start unifiedAnalyticsClient.service",
        "systemctl start audioPlayback.service",
        "systemctl start HealthStatsManager.service",
        "systemctl start scheduler_manager.service",
        "systemctl start apm.service",
        "systemctl start gps.service",
        "systemctl start nd_dta.service",
        "systemctl start nd_sam.service",
        "sleep 2",
        "systemctl start cam_rec.service"
        "systemctl start bagheera.service"
};

#define ROUTE_LOGS

static void update_monotonic_uptime_file () {

    int64_t uptime = get_system_monotonic_time(true);
    stringstream uptime_ss;
    uptime_ss << "echo " << uptime << " > /dev/shm/nd_files_nc/standby_uptime";

    system_execute("SET_UPTIME", uptime_ss.str());
}

static void update_epoch_boottime_file () {

    int64_t boottime = get_system_time() / 1000; // Needed in seconds
    stringstream boottime_ss;
    boottime_ss << "echo " << boottime << " > /dev/shm/nd_files_nc/standby_boottime";

    system_execute("SET_BOOTTIME", boottime_ss.str());
}

enum service_state {none, inactive, activating, failed, other};

void clear_led() {

    nd_device_obj->nd_clear_led( ND_GPIO_PWR_LED_G );
    nd_device_obj->nd_clear_led( ND_GPIO_PWR_LED_R );
    nd_device_obj->nd_clear_led( ND_GPIO_PRIV_LED_G );
    nd_device_obj->nd_clear_led( ND_GPIO_PRIV_LED_R );
}

void set_led_bootup() {

    nd_device_obj->nd_clear_led( ND_GPIO_PWR_LED_G );
    nd_device_obj->nd_clear_led( ND_GPIO_PWR_LED_R );
    nd_device_obj->nd_clear_led( ND_GPIO_PRIV_LED_G );
    nd_device_obj->nd_clear_led( ND_GPIO_PRIV_LED_R );
    nd_device_obj->nd_blink_led( ND_GPIO_PWR_LED_R , 50 );

}

void set_led_bootup_complete() {
    nd_device_obj->nd_clear_led( ND_GPIO_PWR_LED_R );
    nd_device_obj->nd_set_led_on( GREEN, POWER_LED, true);
}

void write_suspend_state_to_reset_reason_file(string suspend_state_str, bool truncate = false){

    string reset_reason_file = "/home/ubuntu/.nddevice/reset_reason.txt";
    if(true == truncate && true == file_truncate(reset_reason_file)){
        LOG_I(TAG,"Reset reason file truncated");
    }
    std::ofstream outfile(reset_reason_file, std::ios::app);
    if (!outfile.is_open()) {
        LOG_E(TAG, "Failed to open /home/ubuntu/.nddevice/reset_reason.txt for writing");
    } else {
        outfile << suspend_state_str << std::endl;
        if (outfile.fail()) {
            LOG_E(TAG, "Failed to write reset reason to file");
        }
        outfile.close();
        if (outfile.fail()) {
            LOG_E(TAG, "Failed to properly close reset reason file after writing");
        }
    }
}

int main(int argc, char *argv[]) {
    //nd_service_obj = NDService::get_service_obj(TAG);
    printf("initilizing logger\n");
    bool res;
    bool status_log = nd_log_init( log_dir.c_str() );
    int retry_count = 0;
    int64_t activating_curr_time = 0;
    int64_t activating_start_time = 0;
    int64_t epoch_before_suspend = 0, epoch_after_suspend = 0;
    
    if(status_log == false) {
            printf("unable to init logger :: Exiting from main");
            return 1;
    }

    string suspend_state_str = "";
    #ifdef ROUTE_LOGS
    route_logs( log_dir.c_str() );
    #endif

    LOG_I(TAG, "######Starting STANDBY ENTRY######");

    nd_device_obj_init();

    for (int i = 0; i < (sizeof (standby_entry_cmds)/sizeof (string)); i++)
    {
        system_execute("ST_ENTRY_LOGS", standby_entry_cmds[i]);
    }

    Config_parser bag_conf(BAGHEERACONFIG_INI);
    bool get_override_val = true, val_overridden = false;
    //BGR2-926. Reboot on SC7 Exit
    string reboot_on_sc7exit( bag_conf.getConfig("power","reboot_on_suspend_exit","true",get_override_val, val_overridden));
    bool is_reboot_required = (strncmp(reboot_on_sc7exit.c_str(), "true", 4) == 0 ? true : false);
    LOG_I(TAG, " reboot_on_suspend_exit(%d)", is_reboot_required);

    //BAG3C
    if( true == nd_device_obj->is_wake_on_motion_supported()) {
	    string wom_enable_str( bag_conf.getConfig("apm","apm_wom_enable", "true", get_override_val, val_overridden) );
	    string ign_enable_str( bag_conf.getConfig("apm","enable_ignition_based_wakeup", "true", get_override_val, val_overridden) );

	    bool ret_wom =  false;
	    if( !strncmp(wom_enable_str.c_str(), "true", 4) || !strncmp(ign_enable_str.c_str(), "false",5) ) {
		    ret_wom = nd_device_obj->configure_imu_wom(true);
	    } else {
		    ret_wom = nd_device_obj->configure_imu_wom(false);
	    }

	    if( false == ret_wom ) {
		    LOG_E(TAG, " WOM Configuration Failed!!!");
	    }

	    //LTC3350. Disable SuperCap Hysterisis, so that it won't draw current in suspend state when connected to battery/power source.
	    // Re-Enable during StartUp. Handled via ltc3350.ko ltc3350_pm_suspend and ltc3350_pm_resume call
    }

    // Disable PMIC watchdog
    disable_pmic_wdt(TAG);

    sleep(2);
    system_execute("ST_ENTRY_LOGS", vendor_standby_entry_script);
    LOG_I(TAG, "######Ending STANDBY ENTRY######");


    service_state suspend_state = none ;
    clear_led();
    // Entering into standby
    epoch_before_suspend = get_system_time();
    activating_start_time = get_system_monotonic_time(true);
    suspend_state_str= "SUSPEND_STATE: SC7_E:"+to_string(convert_epoch_format(get_system_time(), DigitsOfEpoch::eDigits_Seconds));
    write_suspend_state_to_reset_reason_file(suspend_state_str, true);
    do
    {
        sleep(2);
        string resp = "";
        if ( suspend_state != activating ) {
            if(system_execute_with_resp("ST_ENTRY_LOGS", "systemctl suspend", resp) == false) {
                LOG_E (TAG, "suspend command exec failed : resp : %s", resp.c_str());
                retry_count++;
                continue;
            }
            else {
                LOG_I(TAG, "suspend execution done");
                sleep(2);
            }
        }

        if(system_execute_with_resp("ST_ENTRY_LOGS", "systemctl is-failed systemd-suspend", resp) == true) {
            LOG_E (TAG, "suspend check command exec failed : resp : %s", resp.c_str());
            retry_count++;
            continue;
        }

        LOG_I(TAG, "Response: \n %s", resp.c_str());
        if(resp.find("activating") != string::npos) {
            LOG_I(TAG, "Activating");
            suspend_state = activating;
            activating_curr_time = get_system_monotonic_time(true);
            if ((activating_start_time != 0) && (activating_curr_time - activating_start_time > SUSPEND_ACTIVATING_TIMEOUT_MS  )){
                suspend_state == failed;
                break;
            }
        } else if(resp.find("failed") != string::npos) {
            LOG_E(TAG, "failed to move to suspend state. resp = %s", resp.c_str());
            suspend_state = failed;
            retry_count++;
        }else {

            suspend_state = other;
            break;
        }

    } while (retry_count < SUSPEND_RETRY_COUNT);


    if(suspend_state == failed) {
        LOG_E(TAG, "suspend failed, so rebooting");
        suspend_state_str= "SUSPEND_STATE: SC7_F:"+to_string(convert_epoch_format(get_system_time(), DigitsOfEpoch::eDigits_Seconds));
        write_suspend_state_to_reset_reason_file(suspend_state_str);
        system_reboot();
    }

    epoch_after_suspend = get_system_time();
    LOG_I(TAG, "Systemtime before suspend: %lld, Systemtime after suspend: %lld", epoch_before_suspend, epoch_after_suspend);
    LOG_I(TAG, "Time spent in suspend (ms): %lld", epoch_after_suspend - epoch_before_suspend);
    {
        suspend_state_str = "SUSPEND_STATE: SC7_X:"+to_string(convert_epoch_format(get_system_time(), DigitsOfEpoch::eDigits_Seconds));
        write_suspend_state_to_reset_reason_file(suspend_state_str);
        string reason_str = "";
        int power_on_off_reason = 0;
        if (nd_device_obj->get_lanai_pmic_reset_reason(power_on_off_reason, reason_str) == false) {
            LOG_E(TAG, "nd_suspendresume get_reset_wake_reason() failed");
        }
        if(power_on_off_reason != 0 && reason_str != ""){
            LOG_I(TAG,"Power on/off value %d, reset reason string %s",power_on_off_reason,reason_str.c_str());
            // Write Reason String
            size_t pos = reason_str.find(" -- ");
            if(pos != std::string::npos) {
                reason_str.erase(pos, 4); // Remove the " -- "
            }
            pos = reason_str.find("RESET_REASON");
            if(pos != std::string::npos) {
                reason_str.insert(pos, "\n");
            }
            write_suspend_state_to_reset_reason_file(reason_str);
            // Write power on/off value
            write_suspend_state_to_reset_reason_file("VALUE: " + to_string(power_on_off_reason));
        }else{
            LOG_E(TAG,"Power on/off value is 0 and reset reason is empty");
        }

    }

    LOG_I(TAG, "######Starting STANDBY EXIT######");
    write_suspend_state_to_reset_reason_file("SUSPEND_STATE: REBOOT:" + to_string(convert_epoch_format(get_system_time(), DigitsOfEpoch::eDigits_Seconds)));
    LOG_I(TAG, "rebooting the device");
    system_reboot();

    set_led_bootup();    
    update_monotonic_uptime_file();
    update_epoch_boottime_file();

    // Enable PMIC watchdog
    enable_pmic_wdt(TAG, LONG_TIME, false);

    system_execute("STANDBY_EXIT_LOGS", vendor_standby_exit_script);

    set_led_bootup_complete();
    for (int i = 0; i < (sizeof (standby_exit_cmds)/sizeof (string)); i++)
    {
        system_execute("STANDBY_EXIT_LOGS", standby_exit_cmds[i]);
    }
    LOG_I(TAG, "######Ending STANDBY EXIT######");
    return 0;
}
