/* Copyright (C) 2021 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Chaitnaya Gunta <chaitanya.gunta@netradyne.com>, Dec  2023
 */

#include "log.h"
#include "nd_time.h"
#include <nd_task.h>

#include <string>
#include <sstream>
#include <list>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <system_utils.h>
#include <future>
#include <config_parser.h>
#include "nd_factory.h"

#define BAGHEERACONFIG_INI "/home/ubuntu/.nddevice/latest/bagheera_config.ini"
using namespace std;

//NDService *nd_service_obj; //nd service object, to detect critical Errors which will be send to Health stats and cloud
ND_DeviceFactory *nd_device_obj = NULL;  // nd device object based on deviceType

#define TAG "REBOOT"
#define ROUTE_LOGS

#define REBOOT_RETRY_COUNT 2
#define SYNC_TASK_TIMEOUT 30 //30 seconds

static const string log_dir = "/home/ubuntu/.nddevice/log/nd_app_reboot";

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
        "systemctl stop nd_bt.service",
        "systemctl stop installer_app.service",
        "systemctl stop time_sync.service",
        "systemctl stop conn_mgr.service",
        "systemctl stop wifi_mgr.service",
        "systemctl stop apm.service",
        "systemctl stop power_monitor.service",
        "systemctl stop analyticsService.service",
        "systemctl stop outwardAnalyticsClient.service",
        "systemctl stop canAnalyticsClient.service",
        "systemctl stop dmsAnalyticsClient.service",
        "systemctl stop deviceHealthClient.service",
        "systemctl stop inwardAnalyticsClient.service",
        "systemctl stop unifiedAnalyticsClient.service",
        "systemctl stop audioPlayback.service",
        "systemctl stop HealthStatsManager.service",
        "systemctl stop svc.service",
        "systemctl stop service_mon.service",
        "systemctl stop nd_sam.service",
        "systemctl stop nd_dta.service",
        "systemctl stop gps.service",
        "systemctl stop cron.service",
        "systemctl stop haveged.service",
        "systemctl stop systemd-resolved.service",
        "systemctl stop nvargus-daemon.service",
        "rm -rf /run/crond.reboot",
        "rm -rf /dev/shm/nd_files_c",
        "rm -rf /dev/shm/MSGQ",
        "rm -f /dev/shm/*.bin",
};

int main(int argc, char *argv[]) {
    //nd_service_obj = NDService::get_service_obj(TAG);
    printf("initilizing logger\n");
    bool res;
    bool status_log = nd_log_init( log_dir.c_str() );
    int retry_count = 0;
    int64_t activating_curr_time = 0;
    int64_t activating_start_time = 0;
    int64_t epoch_before_reboot = 0;

    if(status_log == false) {
            printf("unable to init logger :: Exiting from main");
            return 1;
    }

    #ifdef ROUTE_LOGS
    route_logs( log_dir.c_str() );
    #endif

    LOG_I(TAG, "######Starting ND_REBOOT ENTRY######");

    nd_device_obj_init();

    //Setting WDOG to 128s and kicking before service tear down
    enable_pmic_wdt(TAG, LONG_TIME, false);

    for (int i = 0; i < (sizeof (standby_entry_cmds)/sizeof (string)); i++)
    {
        system_execute("ST_ENTRY_LOGS", standby_entry_cmds[i]);
    }
    Config_parser bag_conf(BAGHEERACONFIG_INI);
    bool get_override_val = true, val_overridden = false;

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
    }

    //sync file system
    sync_tt(SYNC_TASK_TIMEOUT);

    // KICK PMIC watchdog before issuing reboot
    kick_pmic_wdog(TAG);

    system_execute("REBOOT_ENTRY_LOGS", vendor_standby_entry_script);
    LOG_I(TAG, "######Ending REBOOT  ENTRY######");

    // KICK PMIC watchdog before issuing reboot
    kick_pmic_wdog(TAG);

    // Kicking Reboot
    epoch_before_reboot = get_system_time();
    activating_start_time = get_system_monotonic_time(true);
    LOG_I(TAG, "System Time at Reboot: %lld, monotonic_time: %lld", epoch_before_reboot, activating_start_time);
    do
    {
        string resp = "";
        if(system_execute_with_resp("REBOOT_ENTRY_LOGS", "shutdown -r now", resp) == false) {
            LOG_E (TAG, "reboot  command exec failed");
        }else {
            LOG_E(TAG, "reboot execution done");
        }
        retry_count++;
        epoch_before_reboot = get_system_time();
        activating_start_time = get_system_monotonic_time(true);
        LOG_E(TAG, "System Time after Reboot Attempt(%d): %lld, monotonic_time: %lld", retry_count, epoch_before_reboot, activating_start_time);
        sleep(2);
    } while (retry_count < REBOOT_RETRY_COUNT);

    LOG_I(TAG, "######Ending ND_REBOOT EXIT######");
    return 0;
}
