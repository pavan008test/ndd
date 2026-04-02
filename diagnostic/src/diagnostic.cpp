/* Copyright (C) 2017 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Mukesh <mukeshkumar.singh@netradyne.com>, May 2021
 */

#include <log.h>
#include "nd_msgq.h"
#include "nd_msg_types.h"
#include "nd_msg_utils.h"

#include <string>
#include <sstream>
#include <list>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <system_utils.h>
#include <service_utils.h>
#include "sdcard_utils.h"
#include "storage_utils.h"
#include "nd_task.h"
#include "svc.h"
#include "config_parser.h"
#include "sdcard.h"
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <thread>
#include <nd_prop_utils.h>
#include <nd_factory.h>
#include <jansson/jansson.h>
#include "nd_messenger.h"
#include "nd_file_utils.h"
#include "fsck_recovery.h"
#include "nd_app_timer.h"
#include "process_info.h"
#include "cpugpu_info.h"
#include "waf_utils.h"
#include "nd_ext_cam_utils.h"
#include <sys/statvfs.h>
#include "nd_config_read_utils.h"

ND_DeviceFactory *nd_device_obj = NULL;  // nd device object based on deviceType
using namespace std;
std::condition_variable sd_cond;
std::mutex sd_mutex;
static int64_t count_sdcard = 0;
#define ROUTE_LOGS
#define TAG "DIAG"
#define UNMOUNT_SDCARD_WAIT_TIME 60
#define MOUNT_SDCARD_WAIT_TIME 60
#define WAF_MONITORING_INTERVAL_TIME 30  //in mins

// Macro to check if health analytics is disabled for metric messages
#define SKIP_IF_HEALTH_ANALYTICS_DISABLED() \
    if (health_analytics == false) { \
        LOG_D(TAG, "Health analytics is disabled, skipping metric message type: %d", g_msg->type); \
        break; \
    }

static const string diagnostic_q_name = "DIAGNOSTIC";
static const string time_sync_token_file = "/dev/shm/time_sync_token_file.bin";

SdCard *sdcard_obj_ptr = NULL;
string get_msgq_name();
bool sdcard_readonly_fs();

NDService *nd_service_obj; //nd service object, to detect critical Errors which will be send to Health stats and cloud
static const string BAGHEERACONFIG_INI = "/home/ubuntu/.nddevice/latest/bagheera_config.ini";

#ifdef UNIT_TEST
void unit_test();
#endif

static nd_msgq_t *server_q=NULL;
static int msg_idx = 0;
static const int UNIT_TIME = 1;
static int handles = 0;
static const string log_dir = "/home/ubuntu/.nddevice/log/diagnostic";
static const string modem_devnode_path = "/dev/qcqmi0";
static bool is_automation_enabled = false;
bool health_analytics = true;

enum SIM_STATUS_CHECK {
    modem_not_up = 0,
    modem_up = 1,
    sim_status_check_done = 2
};

static int64_t service_start_time;
static int igni_off_min = 0;
static bool dhub_msg_received = false;
bool is_ext_cam_enabled = false;
static const string GPU_POLL_DEFAULT_STRING = "1"; //gpu thread polling interval default in seconds

// eMMC state tracking for overlay scenarios
static bool emmc_was_down_this_boot = false;
static bool emmc_state_initialized = false;
bool check_overlay_services_status( bool expected_overlay_state);
bool configure_overlay_filesystem_services(bool expected_overlay_state);
void run_overlay_management_wrapper(bool mounts_enabled);
void periodic_overlay_management_thread(bool mounts_enabled);

power_metrics_msg_t power_health_info;
req_dhub_health_msg_t dhub_health_info;
req_storage_health_msg_t storage_health_info;
NDMessenger::ServerBuilder health_analytics_publisher;

struct diagnostic_generic_msg_t {
    msg_type_t type;
    int len;
};

bool reset_master_data = false;



bool init_msgq() {

    //Create message queue
    server_q = nd_msgq_t::get_msgq( get_msgq_name(), nd_msgq_t::ND_MSGQ_SERVER );

    if( server_q == NULL ) {
	LOG_E(TAG, "Cannot create message queue");
	return false;
    }
    
    LOG_I(TAG, "Message queue created");

    return true;
}


int64_t get_service_uptime()
{
    int64_t service_uptime = get_system_monotonic_time() - service_start_time ;
    return service_uptime;
}

void set_igni_off_mins(int cnt)
{
    if(cnt == 1)
    {
	igni_off_min = 0;
    }
    else if( cnt == 0)
    {
        igni_off_min += 1;
    }
    else
    {
	igni_off_min = -1;
    }
}

int get_igni_off_mins()
{
    return igni_off_min;
}

bool get_internal_emmc_mount_status() 
{
    try {
        // Get the mount point path from your device object
        string internal_mount_point = nd_device_obj->get_internal_mount_point();
        
        struct statvfs stat;
        
        LOG_D(TAG, "Checking mount status for: %s", internal_mount_point.c_str());
        
        // If statvfs succeeds, the filesystem is mounted and accessible
        if (statvfs(internal_mount_point.c_str(), &stat) == 0) {
            // Additional check: ensure it's not just an empty directory
            if (stat.f_blocks > 0) {
                LOG_I(TAG,"Internal Emmc is mounted");
                LOG_I(TAG, "Internal eMMC is MOUNTED at: %s", internal_mount_point.c_str());
                return true;
            } else {
                // f_blocks = 0 means NO FILESYSTEM is mounted
                LOG_E(TAG,"Failed to execute mount check command : %s", internal_mount_point.c_str());
                LOG_W(TAG, "Mount point EXISTS but NO FILESYSTEM mounted at: %s", 
                      internal_mount_point.c_str());
                return false;
            }
        } 
    } catch (const std::exception& e) {
        LOG_E(TAG, "EXCEPTION in get_internal_emmc_mount_status: %s", e.what());
        return false;
    }
} //updated code using statvfs to check if the internal emmc is mounted.
bool get_internal_external_memory_info(int *internal_total, int *internal_available, int *external_total, int *external_available) {
    try {
        string internal_dev_node = nd_device_obj->get_internal_mount_point();
        string external_dev_node = nd_device_obj->get_external_mount_point();
        struct statvfs stat;

        if (statvfs(internal_dev_node.c_str(), &stat) != 0) {
            LOG_E(TAG, "Failed to statvfs internal device node: %s", internal_dev_node.c_str());
            return false;
        }
        *internal_total = (stat.f_blocks * stat.f_frsize) / (1024 * 1024);  // Convert to MB
        *internal_available = (stat.f_bavail * stat.f_frsize) / (1024 * 1024);  // Convert to MB

        // External
        if (statvfs(external_dev_node.c_str(), &stat) != 0) {
            LOG_E(TAG, "Failed to statvfs external device node: %s", external_dev_node.c_str());
            return false;
        }
        *external_total = (stat.f_blocks * stat.f_frsize) / (1024 * 1024); // Convert to MB
        *external_available = (stat.f_bavail * stat.f_frsize) / (1024 * 1024); // Convert to MB

        LOG_I(TAG, "internal_total %d", *internal_total);
        LOG_I(TAG, "internal_available %d", *internal_available);
        LOG_I(TAG, "external_total %d", *external_total);
        LOG_I(TAG, "external_available %d", *external_available);

        return true;
    } catch (const std::exception& e) {
        LOG_E(TAG, "Exception in get_internal_external_memory_info: %s", e.what());
        return false;
    }
}

bool get_write_access(int& emmc_status, int& sdcard_status)
{
    string internal_filepath = nd_device_obj->smart_health_report_params_for_memory(INTERNAL_WRITE_ACCESS_FILE);
    string external_filepath = nd_device_obj->smart_health_report_params_for_memory(EXTERNAL_WRITE_ACCESS_FILE);

    // Writing to a file
    ofstream internal_file(internal_filepath);
    if (internal_file.is_open()) {
        internal_file << "This is a test line";
        internal_file.close();
        emmc_status = 1;
    } else {
        LOG_E(TAG,"Unable to open file for writing");
        emmc_status = 0;
    }

    ofstream external_file(external_filepath);
    if (external_file.is_open()) {
        external_file << "This is a test line";
        external_file.close();
        sdcard_status = 1;
    } else {
        LOG_E(TAG,"Unable to open file for writing");
        sdcard_status = 0;
    }
    file_delete(external_filepath);
    file_delete(internal_filepath);
    return true;
}
//using ifstream to read the manufacturer info from the sysfs files.
bool get_manufacturer_info(string &int_manfid, string &int_name, string &int_oemid, string &int_serial, 
                          string &ext_manfid, string &ext_name, string &ext_oemid, string &ext_serial) {
    try {
        // Helper lambda to read files directly
        auto read_sys_file = [](const string& filepath, const string& description) -> string {
            try {
                ifstream file(filepath);
                if (!file.is_open()) {
                    LOG_W(TAG, "Failed to get %s", description.c_str());
                    return "";
                }
                
                string content;
                if (!getline(file, content)) {
                    LOG_E(TAG, "Failed to get %s", description.c_str());
                    return "";
                }
                
                // Remove trailing whitespace/newlines (equivalent to tr -d '\n')
                content.erase(content.find_last_not_of(" \t\r\n") + 1);
                
                LOG_D(TAG, "Read %s: '%s' from %s", description.c_str(), content.c_str(), filepath.c_str());
                return content;
                
            } catch (const std::exception& e) {
                LOG_E(TAG, "Exception reading %s: %s", description.c_str(), e.what());
                return "";
            }
        };

        LOG_I(TAG, "Collecting eMMC manufacturer information...");

        // Read internal eMMC info (no shell commands!)
        int_manfid = read_sys_file(
            nd_device_obj->smart_health_report_params_for_memory(INTERNAL_MANFID), 
            "internal manufacturer ID"
        );
        int_name = read_sys_file(
            nd_device_obj->smart_health_report_params_for_memory(INTERNAL_eMMC_NAME), 
            "internal eMMC name"
        );
        int_oemid = read_sys_file(
            nd_device_obj->smart_health_report_params_for_memory(INTERNAL_OEMID), 
            "internal OEM ID"
        );
        int_serial = read_sys_file(
            nd_device_obj->smart_health_report_params_for_memory(INTERNAL_SERIAL), 
            "internal serial number"
        );

        // Read external eMMC info (no shell commands))
        ext_manfid = read_sys_file(
            nd_device_obj->smart_health_report_params_for_memory(EXTERNAL_MANFID), 
            "external manufacturer ID"
        );
        ext_name = read_sys_file(
            nd_device_obj->smart_health_report_params_for_memory(EXTERNAL_eMMC_NAME), 
            "external eMMC name"
        );
        ext_oemid = read_sys_file(
            nd_device_obj->smart_health_report_params_for_memory(EXTERNAL_OEMID), 
            "external OEM ID"
        );
        ext_serial = read_sys_file(
            nd_device_obj->smart_health_report_params_for_memory(EXTERNAL_SERIAL), 
            "external serial number"
        );
        LOG_I(TAG, "Internal manfid: %s", int_manfid.empty() ? "UNAVAILABLE" : int_manfid.c_str());
        LOG_I(TAG, "External manfid: %s", ext_manfid.empty() ? "UNAVAILABLE" : ext_manfid.c_str());
        LOG_I(TAG, "Internal name: %s", int_name.empty() ? "UNAVAILABLE" : int_name.c_str());
        LOG_I(TAG, "External name: %s", ext_name.empty() ? "UNAVAILABLE" : ext_name.c_str());
        LOG_I(TAG, "Internal OEMID: %s", int_oemid.empty() ? "UNAVAILABLE" : int_oemid.c_str());
        LOG_I(TAG, "External OEMID: %s", ext_oemid.empty() ? "UNAVAILABLE" : ext_oemid.c_str());
        LOG_I(TAG, "Internal serial: %s", int_serial.empty() ? "UNAVAILABLE" : int_serial.c_str());
        LOG_I(TAG, "External serial: %s", ext_serial.empty() ? "UNAVAILABLE" : ext_serial.c_str());
        return true;
        
    } catch (const std::exception& e) {
        LOG_E(TAG, "Exception in get_manufacturer_info: %s", e.what());
        return false;
    }
} //updated code to read manufacturer info using ifstream.

bool publish_dhub_metrics(res_dhub_health_msg_t* dhub_metrics, string messenger_addr_analytics, string messenger_topic_health_analytics)
{

    health_analytics_publisher.setMessage(string(dhub_metrics->res));
    if(is_automation_enabled)
    {
        ofstream dhub_file("/dev/shm/dhub_health.txt",ios::out | ios::app);
        dhub_file << " , " << get_system_time();
        dhub_file << "\n";
        dhub_file.close();
    }

    bool status = health_analytics_publisher.publish();
    return status;

}

bool publish_storage_metrics(res_storage_health_msg_t* storage_metrics, power_metrics_msg_t* power_metrics, res_dhub_health_msg_t* dhub_metrics, string messenger_addr_analytics, string messenger_topic_health_analytics)
{
	int64_t milli_secs_in_min = int64_t(MILLISECONDS_IN_A_MINUTE);
	int system_uptime = int(get_system_monotonic_time() / milli_secs_in_min);
	int cb_uptime = int(storage_metrics->service_uptime / milli_secs_in_min);
	int ds_uptime = int(get_service_uptime() / milli_secs_in_min);

	mount_status_t mount_status = get_mount_status(nd_device_obj->get_external_eMMC_phy_mount_path(), nd_device_obj->get_external_eMMC_dev_node());
	string details_of_obs = to_string(storage_metrics->no_of_obs_files) + "/" + to_string(storage_metrics->size_of_obs_files);
    string details_of_partial_0 = to_string(storage_metrics->no_of_partial_0) + "/" + to_string(storage_metrics->size_of_partial_0);
	string details_of_partial_1 = to_string(storage_metrics->no_of_partial_1) + "/" + to_string(storage_metrics->size_of_partial_1);
	string details_of_partial_2 = to_string(storage_metrics->no_of_partial_2) + "/" + to_string(storage_metrics->size_of_partial_2);
	string details_of_partial_3 = to_string(storage_metrics->no_of_partial_3) + "/" + to_string(storage_metrics->size_of_partial_3);
	string details_of_partial_N;

    if(dhub_msg_received)
    {
        details_of_partial_N = to_string(dhub_metrics->pf) + "/" + to_string(dhub_metrics->pf_size);
        dhub_msg_received = false;
    }
    else
    {
        details_of_partial_N = "0/0";
    }

	string details_of_hq_0 = to_string(storage_metrics->no_of_hq_0) + "/" + to_string(storage_metrics->size_of_hq_0);
	string details_of_hq_1 = to_string(storage_metrics->no_of_hq_1) + "/" + to_string(storage_metrics->size_of_hq_1);
	string details_of_hq_2 = to_string(storage_metrics->no_of_hq_2) + "/" + to_string(storage_metrics->size_of_hq_2);
	string details_of_hq_3 = to_string(storage_metrics->no_of_hq_3) + "/" + to_string(storage_metrics->size_of_hq_3);
	string details_of_hq_N = to_string(storage_metrics->no_of_hq_N) + "/" + to_string(storage_metrics->size_of_hq_N);
	string details_of_lq_0 = to_string(storage_metrics->no_of_lq_0) + "/" + to_string(storage_metrics->size_of_lq_0);
	string details_of_lq_1 = to_string(storage_metrics->no_of_lq_1) + "/" + to_string(storage_metrics->size_of_lq_1);
	string details_of_lq_N = to_string(storage_metrics->no_of_lq_N) + "/" + to_string(storage_metrics->size_of_lq_N);
    string details_of_audio_files = to_string(storage_metrics->no_of_audio_files) + "/" + to_string(storage_metrics->size_of_audio_files);

	int internal_total = 0, internal_available = 0, external_total = 0, external_available = 0;
	get_internal_external_memory_info(&internal_total, &internal_available, &external_total, &external_available);
	float internal_mem = float(internal_available)/internal_total;
	float external_mem = float(external_available)/external_total;
	int emmc_status = -1;
	int sd_card_status = -1;

	memory_info mem_info;
	sdcard_obj_ptr->get_remaining_life_bad_block_spare_block(mem_info);
	get_write_access(emmc_status, sd_card_status);
    string sd_card_rw = to_string(sdcard_obj_ptr->get_average_copy_time()) + "/" + to_string(sdcard_obj_ptr->get_average_delete_time());

	string int_manfid = "", int_name = "", int_oemid = "", int_serial = "";
    string ext_manfid = "", ext_name = "", ext_oemid = "", ext_serial = "";
	get_manufacturer_info(int_manfid, int_name, int_oemid, int_serial, ext_manfid, ext_name, ext_oemid, ext_serial);
	string int_man_info = "MANID:" + int_manfid + "-NAME:" + int_name + "-OEMID:" + int_oemid + "-SN:" + int_serial;
	string ext_man_info;
    if((nd_device_obj->getDeviceType() == eBagheera_2) || (nd_device_obj->getDeviceType() == eBagheera_3)) {
        ext_man_info = "MANID:" + ext_manfid + "-NAME:" + ext_name + "-OEMID:" + ext_oemid + "-SN:" + ext_serial;
    }


	LOG_D(TAG, "cb is %d ds is %d",cb_uptime,ds_uptime);
	LOG_D(TAG, "iom is %d lpw_cnt is %d", get_igni_off_mins(), power_metrics->lpw_cnt);
    LOG_D(TAG, "*************** no_of_obs_files : %ld ", storage_metrics->no_of_obs_files);
    LOG_D(TAG, "*************** no_of_obs_files_size : %f ", storage_metrics->size_of_obs_files);
    LOG_D(TAG, "*************** no_of_obs_files_details : %s ", details_of_obs.c_str());
	LOG_D(TAG, "internal mem is %f external_mem is %f", internal_mem, external_mem);


	json_t *root = json_object();
	json_t *storage_info;  
	storage_info = json_object();  

	json_object_set_new(storage_info, "timestamp", json_integer(storage_metrics->time));
	json_object_set_new(storage_info, "sys_uptime", json_integer( system_uptime ));
	json_t *service_uptime = json_object();
	json_object_set_new(service_uptime, "cb", json_integer( cb_uptime ));
	json_object_set_new(service_uptime, "ds", json_integer( ds_uptime ));
	json_object_set_new(storage_info, "service_uptime", service_uptime);
	
    json_object_set_new(storage_info, "iom", json_integer(get_igni_off_mins()));

	json_object_set_new(storage_info, "lpc", json_integer(power_metrics->lpw_cnt));

	json_t *memory_mounted = json_object();
	json_object_set_new(memory_mounted, "emmc", json_integer(get_internal_emmc_mount_status()));
	json_object_set_new(memory_mounted, "ext_emmc", json_integer(mount_status));
	json_object_set_new(storage_info, "memory_mounted", memory_mounted);

	json_object_set_new(storage_info, "avs", json_integer( storage_metrics->avail_video_storage_min));

	json_t *ovd = json_object();	
	json_object_set_new(ovd, "ts", json_integer(storage_metrics->time));
	json_object_set_new(ovd, "lat", json_real(storage_metrics->ovd_lat));
	json_object_set_new(ovd, "long", json_real(storage_metrics->ovd_lon));
	json_object_set_new(ovd, "udid", json_string(storage_metrics->ovd_udid));
	json_object_set_new(storage_info, "ovd", ovd);

    
	json_object_set_new(storage_info, "no_of_obs", json_string(details_of_obs.c_str()));

	json_t *no_of_partial = json_object();
		json_object_set_new(no_of_partial, "0", json_string(details_of_partial_0.c_str()));
	json_object_set_new(no_of_partial, "1", json_string(details_of_partial_1.c_str()));
	json_object_set_new(no_of_partial, "2", json_string(details_of_partial_2.c_str()));
	json_object_set_new(no_of_partial, "3", json_string(details_of_partial_3.c_str()));
	json_object_set_new(no_of_partial, "N", json_string(details_of_partial_N.c_str()));
	json_object_set_new(storage_info, "no_of_partial", no_of_partial);

	json_t *no_of_hq = json_object();
	json_object_set_new(no_of_hq, "0", json_string(details_of_hq_0.c_str()));
	json_object_set_new(no_of_hq, "1", json_string(details_of_hq_1.c_str()));
	json_object_set_new(no_of_hq, "2", json_string(details_of_hq_2.c_str()));
	json_object_set_new(no_of_hq, "3", json_string(details_of_hq_3.c_str()));
	json_object_set_new(no_of_hq, "N", json_string(details_of_hq_N.c_str()));
	json_object_set_new(storage_info, "no_of_hq", no_of_hq);

	// Add the no_of_lq array
	json_t *no_of_lq = json_object();
	json_object_set_new(no_of_lq, "0", json_string(details_of_lq_0.c_str()));
	json_object_set_new(no_of_lq, "1", json_string(details_of_lq_1.c_str()));
	json_object_set_new(no_of_lq, "N", json_string(details_of_lq_N.c_str()));
	json_object_set_new(storage_info, "no_of_lq", no_of_lq);


	// Add the no_of_audio array
	json_object_set_new(storage_info, "no_of_audio", json_string(details_of_audio_files.c_str()));

	// add the no_of_files_added field
	json_object_set_new(storage_info, "no_of_files_added", json_integer(storage_metrics->no_of_files_added));

	// add the no_of_files_deleted field
	json_object_set_new(storage_info, "no_of_files_deleted", json_integer(storage_metrics->no_of_files_deleted));

	json_t *memory_size = json_object();
	

	json_object_set_new(memory_size, "emmc", json_real(internal_mem));
	json_object_set_new(memory_size, "ext_emmc", json_real(external_mem));
	json_object_set_new(storage_info, "memory_size", memory_size);

	// Add the bad_sectors object
	json_t *bad_sectors = json_object();
    if((nd_device_obj->getDeviceType() == eBagheera_2) || (nd_device_obj->getDeviceType() == eBagheera_3)) {
	    json_object_set_new(bad_sectors, "emmc", json_real( mem_info.bad_block_emmc));
    }
	json_object_set_new(bad_sectors, "ext_emmc", json_real( mem_info.bad_block_sd));
	json_object_set_new(storage_info, "bad_sectors", bad_sectors);

	// Add the spare_block_used object
	json_t *spare_block_used = json_object();
    if((nd_device_obj->getDeviceType() == eBagheera_2) || (nd_device_obj->getDeviceType() == eBagheera_3)) {
	    json_object_set_new(spare_block_used, "emmc", json_real(mem_info.spare_block_emmc));
    }

    json_object_set_new(spare_block_used, "ext_emmc", json_real(mem_info.spare_block_sd));
	json_object_set_new(storage_info, "spare_block_used", spare_block_used);

	// Add the write_access object  
	json_t *write_access = json_object();
	json_object_set_new(write_access, "emmc", json_integer(emmc_status));
	json_object_set_new(write_access, "ext_emmc", json_integer(sd_card_status));
	json_object_set_new(storage_info, "write_access", write_access);

	// Add the r/w_status object  
	json_t *r_w_status = json_object();
	json_object_set_new(r_w_status, "ext_emmc", json_string(sd_card_rw.c_str()));
	json_object_set_new(storage_info, "r/w_status", r_w_status);

	// Add the rem_life object  
	json_t *rem_life = json_object();
    if((nd_device_obj->getDeviceType() == eBagheera_2) || (nd_device_obj->getDeviceType() == eBagheera_3)) {
	    json_object_set_new(rem_life, "emmc", json_real( mem_info.rem_life_emmc));
    }
	json_object_set_new(rem_life, "ext_emmc", json_real(mem_info.rem_life_sd));
	json_object_set_new(storage_info, "rem_life", rem_life);

	// Create waf object  
	json_t *waf = json_object();
	json_object_set_new(waf, "emmc", json_string("NA"));
	json_object_set_new(waf, "ext_emmc", json_string("NA"));

//	json_object_set_new(storage_info, "waf", waf);

	// Create mi object
	json_t *mi = json_object();
    	json_object_set_new(mi, "emmc", json_string( int_man_info.c_str() ));
    if((nd_device_obj->getDeviceType() == eBagheera_2) || (nd_device_obj->getDeviceType() == eBagheera_3)) {
	    json_object_set_new(mi, "ext_emmc", json_string( ext_man_info.c_str() ));
    }

	// Add mi object to storage_info  
	json_object_set_new(storage_info, "mi", mi);

	// Add hmuf and lap to storage_info
//	json_object_set_new(storage_info, "hmuf", json_string("Sub Folder Name/ Size Of Sub Folder "));
	json_object_set_new(storage_info, "lap", json_string((sdcard_obj_ptr->check_last_recovery_method()).c_str()));
	json_object_set_new(root, "storage_info", storage_info);


    // storage metrics for healthstats
    json_t* root_healthstats = json_object();
    json_t* storage_info_healthstats = json_object();    
	json_object_set_new(storage_info_healthstats, "ts", json_integer(storage_metrics->time));
	json_object_set_new(storage_info_healthstats, "ut", json_integer( system_uptime ));

	json_t *service_uptime_healthstats = json_object();
	json_object_set_new(service_uptime_healthstats, "cb", json_integer( cb_uptime ));
	json_object_set_new(service_uptime_healthstats, "ds", json_integer( ds_uptime ));
	json_object_set_new(storage_info_healthstats, "sut", service_uptime_healthstats);
	
    json_object_set_new(storage_info_healthstats, "iom", json_integer(get_igni_off_mins()));
	json_object_set_new(storage_info_healthstats, "lpc", json_integer(power_metrics->lpw_cnt));

	json_t *memory_mounted_healthstats = json_object();
	json_object_set_new(memory_mounted_healthstats, "emmc", json_integer(get_internal_emmc_mount_status()));
	json_object_set_new(memory_mounted_healthstats, "ext_emmc", json_integer(mount_status));
	json_object_set_new(storage_info_healthstats, "mmnt", memory_mounted_healthstats);

	json_object_set_new(storage_info_healthstats, "avs", json_integer( storage_metrics->avail_video_storage_min));

	json_t *ovd_healthstats = json_object();	
	json_object_set_new(ovd_healthstats, "ts", json_integer(storage_metrics->time));
	json_object_set_new(ovd_healthstats, "lat", json_real(storage_metrics->ovd_lat));
	json_object_set_new(ovd_healthstats, "long", json_real(storage_metrics->ovd_lon));
	json_object_set_new(ovd_healthstats, "udid", json_string(storage_metrics->ovd_udid));
	json_object_set_new(storage_info_healthstats, "ovd", ovd_healthstats);

	json_object_set_new(storage_info_healthstats, "obscnt", json_string(details_of_obs.c_str()));

	json_t *no_of_partial_healthstats = json_object();
	json_object_set_new(no_of_partial_healthstats, "0", json_string(details_of_partial_0.c_str()));
	json_object_set_new(no_of_partial_healthstats, "1", json_string(details_of_partial_1.c_str()));
	json_object_set_new(no_of_partial_healthstats, "2", json_string(details_of_partial_2.c_str()));
	json_object_set_new(no_of_partial_healthstats, "3", json_string(details_of_partial_3.c_str()));
	json_object_set_new(no_of_partial_healthstats, "N", json_string(details_of_partial_N.c_str()));
	json_object_set_new(storage_info_healthstats, "parcnt", no_of_partial_healthstats);

	json_t *no_of_hq_healthstats = json_object();
	json_object_set_new(no_of_hq_healthstats, "0", json_string(details_of_hq_0.c_str()));
	json_object_set_new(no_of_hq_healthstats, "1", json_string(details_of_hq_1.c_str()));
	json_object_set_new(no_of_hq_healthstats, "2", json_string(details_of_hq_2.c_str()));
	json_object_set_new(no_of_hq_healthstats, "3", json_string(details_of_hq_3.c_str()));
	json_object_set_new(no_of_hq_healthstats, "N", json_string(details_of_hq_N.c_str()));
	json_object_set_new(storage_info_healthstats, "hqcnt", no_of_hq_healthstats);

	// Add the no_of_lq array
	json_t *no_of_lq_healthstats = json_object();
	json_object_set_new(no_of_lq_healthstats, "0", json_string(details_of_lq_0.c_str()));
	json_object_set_new(no_of_lq_healthstats, "1", json_string(details_of_lq_1.c_str()));
	json_object_set_new(no_of_lq_healthstats, "N", json_string(details_of_lq_N.c_str()));
	json_object_set_new(storage_info_healthstats, "lqcnt", no_of_lq_healthstats);


	// Add the no_of_audio array
	json_object_set_new(storage_info_healthstats, "audcnt", json_string(details_of_audio_files.c_str()));

	// add the no_of_files_added field
	json_object_set_new(storage_info_healthstats, "facnt", json_integer(storage_metrics->no_of_files_added));

	// add the no_of_files_deleted field
	json_object_set_new(storage_info_healthstats, "fdcnt", json_integer(storage_metrics->no_of_files_deleted));

	json_t *memory_size_healthstats = json_object();
	json_object_set_new(memory_size_healthstats, "emmc", json_real(internal_mem));
	json_object_set_new(memory_size_healthstats, "ext_emmc", json_real(external_mem));
	json_object_set_new(storage_info_healthstats, "msize", memory_size_healthstats);

	// Add the bad_sectors object
	json_t *bad_sectors_healthstats = json_object();
    if((nd_device_obj->getDeviceType() == eBagheera_2) || (nd_device_obj->getDeviceType() == eBagheera_3)) {
	    json_object_set_new(bad_sectors_healthstats, "emmc", json_real( mem_info.bad_block_emmc));
    }
	json_object_set_new(bad_sectors_healthstats, "ext_emmc", json_real( mem_info.bad_block_sd));
	json_object_set_new(storage_info_healthstats, "badsec", bad_sectors_healthstats);

	// Add the spare_block_used object
	json_t *spare_block_used_healthstats = json_object();
    if((nd_device_obj->getDeviceType() == eBagheera_2) || (nd_device_obj->getDeviceType() == eBagheera_3)) {
	    json_object_set_new(spare_block_used_healthstats, "emmc", json_real(mem_info.spare_block_emmc));
    }
        json_object_set_new(spare_block_used_healthstats, "ext_emmc", json_real(mem_info.spare_block_sd));
	json_object_set_new(storage_info_healthstats, "sbused", spare_block_used_healthstats);

	// Add the write_access object  
	json_t *write_access_healthstats = json_object();
	json_object_set_new(write_access_healthstats, "emmc", json_integer(emmc_status));
	json_object_set_new(write_access_healthstats, "ext_emmc", json_integer(sd_card_status));
	json_object_set_new(storage_info_healthstats, "memwa", write_access_healthstats);

	// Add the r/w_status object  
	json_t *r_w_status_healthstats = json_object();
	json_object_set_new(r_w_status_healthstats, "ext_emmc", json_string(sd_card_rw.c_str()));
	json_object_set_new(storage_info_healthstats, "rws", r_w_status_healthstats);

	// Add the rem_life object  
	json_t *rem_life_healthstats = json_object();
    if((nd_device_obj->getDeviceType() == eBagheera_2) || (nd_device_obj->getDeviceType() == eBagheera_3)) {
	    json_object_set_new(rem_life_healthstats, "emmc", json_real( mem_info.rem_life_emmc));
    }
	json_object_set_new(rem_life_healthstats, "ext_emmc", json_real(mem_info.rem_life_sd));
	json_object_set_new(storage_info_healthstats, "remlfe", rem_life_healthstats);

	// Create waf object  
	json_t *waf_healthstats = json_object();
	json_object_set_new(waf_healthstats, "emmc", json_string("NA"));
	json_object_set_new(waf_healthstats, "ext_emmc", json_string("NA"));

//	json_object_set_new(storage_info, "waf", waf);

	// Create mi object
	json_t *mi_healthstats = json_object();
	json_object_set_new(mi_healthstats, "emmc", json_string( int_man_info.c_str() ));
    if((nd_device_obj->getDeviceType() == eBagheera_2) || (nd_device_obj->getDeviceType() == eBagheera_3)) {
        json_object_set_new(mi_healthstats, "ext_emmc", json_string( ext_man_info.c_str() ));
    }

	// Add mi object to storage_info  
	json_object_set_new(storage_info_healthstats, "mi", mi_healthstats);

	// Add hmuf and lap to storage_info
//	json_object_set_new(storage_info, "hmuf", json_string("Sub Folder Name/ Size Of Sub Folder "));
	json_object_set_new(storage_info_healthstats, "lap", json_string((sdcard_obj_ptr->check_last_recovery_method()).c_str()));
	json_object_set_new( root_healthstats, "isArray" , json_string("true"));
	json_object_set_new(root_healthstats, "health_info:storage_stats", storage_info_healthstats);

    char* storage_health_stats;
    if(root_healthstats != NULL)
    {
        storage_health_stats = json_dumps(root_healthstats, JSON_REAL_PRECISION(9));
        int length = strlen(storage_health_stats);
        LOG_D(TAG,"Sending Storage Health Metrics To HealthStats");
        nd_service_obj->send_msg_healthstats(storage_health_stats, length);
    }
    else
    {
        LOG_E(TAG, "Storage Data Is NULL, Unable To Send Storage Health Metrics To HealthStats");
    }

    // Clean up storage_health_stats after sending to healthstats
    if (storage_health_stats != NULL){
        free(storage_health_stats);
    }
    json_decref(root_healthstats);

    if (health_analytics == false) 
    {
        LOG_D(TAG, "Health analytics is disabled");
        // Clean up root since it won't be used
        json_decref(root);
    }
    else
    {
        char* sdcard_health = NULL;
        sdcard_health = json_dumps(root,JSON_REAL_PRECISION(9));


        health_analytics_publisher.setMessage(string(sdcard_health));
        if(is_automation_enabled)
        {
            ofstream sdcard_file("/dev/shm/sd_card.txt");
            sdcard_file << string(sdcard_health);
            sdcard_file << " , " << get_system_time();
            sdcard_file << "\n";
            sdcard_file.flush();
        }
        bool status = health_analytics_publisher.publish();
        if(sdcard_health != NULL)
            free(sdcard_health);
        json_decref(root);
    }    
}

bool send_power_healthstats(power_metrics_msg_t power_metrics)
{
    json_t *root = json_object();
    json_t* power_info = json_object();
    json_t* igni = json_object();
    json_t* lpw_pwr = json_object();
    json_t* batt_volt = json_object();
    json_t* shutdown = json_object();
    char* power_health_stats = NULL;

    json_object_set_new(power_info, "ts", json_integer(power_metrics.ts));
    json_object_set_new(power_info, "sut", json_integer(power_metrics.uptime));
    json_object_set_new(igni, "sta", json_integer(power_metrics.igni_stat));
    json_object_set_new(igni, "srcs", json_integer(power_metrics.ign_src_stat));

    json_object_set_new(igni, "piot", json_integer(power_metrics.igni_post_off_time));
    json_object_set_new(lpw_pwr, "lpc", json_integer(power_metrics.lpw_cnt));
    json_object_set_new(lpw_pwr, "lpcd", json_integer(power_metrics.lpw_cyc_dur));
    json_object_set_new(lpw_pwr, "lpwd", json_integer(power_metrics.lpw_wakeup_dur));
    json_object_set_new(batt_volt, "minvth", json_integer(power_metrics.batt_min_volt));
    json_object_set_new(batt_volt, "maxvth", json_integer(power_metrics.batt_max_volt));
    json_object_set_new(shutdown, "rsn", json_integer(power_metrics.shutdown_reason));
    json_object_set_new(shutdown, "prvspd", json_integer(power_metrics.prev_spd));
    json_object_set_new(shutdown, "prvvolt", json_integer(power_metrics.prev_volt));
    json_object_set_new(power_info, "waker", json_integer(power_metrics.wakeup_reason));

    json_object_set_new(power_info, "igni", igni);
    json_object_set_new(power_info, "lpw", lpw_pwr);
    json_object_set_new(batt_volt, "ibv", json_integer(-1));
    json_object_set_new(batt_volt, "cv", json_integer(power_metrics.prev_volt));
    json_object_set_new(power_info, "volt", batt_volt);
    json_object_set_new(power_info, "temp", json_real(power_metrics.temp));
    json_object_set_new(power_info, "sccnt", json_integer(power_metrics.supercap_toggle_cnt));
    json_object_set_new(power_info, "sdn", shutdown);
    json_object_set_new(power_info, "fanspd", json_integer(power_health_info.fan_spd));
    json_object_set_new(power_info, "fic", json_integer(-1));
    json_object_set_new( root, "isArray" , json_string("true"));
    json_object_set_new(root, "health_info:power_info", power_info);

    if(root != NULL)
    {
        power_health_stats = json_dumps(root, JSON_REAL_PRECISION(9));
        int length = strlen(power_health_stats);
        LOG_I(TAG,"Sending Power Health Metrics To HealthStats");
        bool ret = nd_service_obj->send_msg_healthstats(power_health_stats, length);
        if(power_health_stats != NULL)
        {
            free(power_health_stats);
        }
        json_decref(root);
        return ret;
    }
    else
    {
        LOG_E(TAG, "Power Data Is NULL, Unable To Send Power Health Metrics To HealthStats");
        return false;
    }

}

bool publish_power_metrics(power_metrics_msg_t* power_metrics, string messenger_addr_analytics, string messenger_topic_health_analytics)
{
	msg_idx++;
	LOG_D(TAG, "Timestamp is %ld", power_metrics->ts);
	LOG_D(TAG, "Uptime is %d", power_metrics->uptime);
	LOG_D(TAG, "Ignition Status is %d", power_metrics->igni_stat);
    LOG_D(TAG, "Ignition Source Status is %d", power_metrics->ign_src_stat);
	LOG_D(TAG, "igni_post_off_time is %d", power_metrics->igni_post_off_time);
	LOG_D(TAG, "lpw_cnt is %d", power_metrics->lpw_cnt);
	LOG_D(TAG, "lpw_cyc_dur is %d", power_metrics->lpw_cyc_dur);
	LOG_D(TAG, "lpw_wakeup_dur is %d", power_metrics->lpw_wakeup_dur);
	LOG_D(TAG, "wakeup_reason is %d", power_metrics->wakeup_reason);
	LOG_D(TAG, "batt_curr_volt is %d", power_metrics->batt_curr_volt);
	LOG_D(TAG, "batt_min_volt is %d", power_metrics->batt_min_volt);
	LOG_D(TAG, "batt_max_volt is %d", power_metrics->batt_max_volt);
	LOG_D(TAG, "Temparature is %f", power_metrics->temp);
	LOG_D(TAG, "curr_spd is %d", power_metrics->curr_spd);
	LOG_D(TAG, "prev_spd is %d", power_metrics->prev_spd);
	LOG_D(TAG, "prev_voltage is %d", power_metrics->prev_volt);
	LOG_D(TAG, "curr_lat is %f", power_metrics->curr_lat);
	LOG_D(TAG, "curr_long is %f", power_metrics->curr_long);
	LOG_D(TAG, "shutdown_reason is %d", power_metrics->shutdown_reason);
	LOG_D(TAG, "fan speed is %d", power_health_info.fan_spd);

	set_igni_off_mins(power_metrics->igni_stat);
	dhub_health_info.wakeup_reason = power_metrics->wakeup_reason;

	json_t *root = json_object();
	json_t* power_info = json_object();
	json_t* igni = json_object();
	json_t* lpw_pwr = json_object();
	json_t* batt_volt = json_object();
	json_t* shutdown = json_object();

	json_object_set_new(power_info, "ts", json_integer(power_metrics->ts));
	json_object_set_new(power_info, "uptime", json_integer(power_metrics->uptime));
	json_object_set_new(igni, "status", json_integer(power_metrics->igni_stat));
    json_object_set_new(igni, "src_status", json_integer(power_metrics->ign_src_stat));

	if(msg_idx > 1)
	{
		json_object_set_new(igni, "post_ign_off_time", json_integer(-1));
		json_object_set_new(lpw_pwr, "low_pwr_cnt", json_integer(-1));
		json_object_set_new(lpw_pwr, "low_pwr_cyc_dur", json_integer(-1));
		json_object_set_new(lpw_pwr, "low_pwr_wakeup_dur", json_integer(-1));
		json_object_set_new(batt_volt, "min_volt_thres", json_integer(-1));
		json_object_set_new(batt_volt, "max_volt_thres", json_integer(-1));
		json_object_set_new(shutdown, "reason", json_integer(-1));
		json_object_set_new(shutdown, "prev_speed", json_integer(-1));
		json_object_set_new(shutdown, "prev_volt", json_integer(-1));
		json_object_set_new(power_info, "device_wakeup_reason", json_integer(-1));

	}
	else
	{
		json_object_set_new(igni, "post_ign_off_time", json_integer(power_metrics->igni_post_off_time));
		json_object_set_new(lpw_pwr, "low_pwr_cnt", json_integer(power_metrics->lpw_cnt));
		json_object_set_new(lpw_pwr, "low_pwr_cyc_dur", json_integer(power_metrics->lpw_cyc_dur));
		json_object_set_new(lpw_pwr, "low_pwr_wakeup_dur", json_integer(power_metrics->lpw_wakeup_dur));
		json_object_set_new(batt_volt, "min_volt_thres", json_integer(power_metrics->batt_min_volt));
		json_object_set_new(batt_volt, "max_volt_thres", json_integer(power_metrics->batt_max_volt));
		json_object_set_new(shutdown, "reason", json_integer(power_metrics->shutdown_reason));
		json_object_set_new(shutdown, "prev_speed", json_integer(power_metrics->prev_spd));
		json_object_set_new(shutdown, "prev_volt", json_integer(power_metrics->prev_volt));
		json_object_set_new(power_info, "device_wakeup_reason", json_integer(power_metrics->wakeup_reason));

	}
	json_object_set_new(power_info, "igni", igni);
	json_object_set_new(power_info, "lpw_pwr", lpw_pwr);
	json_object_set_new(batt_volt, "inst_batt_volt", json_integer(-1));
	json_object_set_new(batt_volt, "curr_volt", json_integer(power_metrics->prev_volt));
	json_object_set_new(power_info, "batt_volt", batt_volt);
	json_object_set_new(power_info, "temp", json_real(power_metrics->temp));
	json_object_set_new(power_info, "curr_lat", json_real(power_metrics->curr_lat));
	json_object_set_new(power_info, "curr_long", json_real(power_metrics->curr_long));
	json_object_set_new(power_info, "curr_speed", json_integer(power_metrics->curr_spd));
	json_object_set_new(power_info, "supercap_toggle_cnt", json_integer(0));
	json_object_set_new(power_info, "shutdown", shutdown);
	json_object_set_new(power_info, "fan_speed", json_integer(power_health_info.fan_spd));
	json_object_set_new(power_info, "fault_in_chip", json_integer(-1));
	json_object_set_new(root, "power_info", power_info);

    if (send_power_healthstats(*power_metrics))
    {
        LOG_I(TAG,"Successfully Sent Power Health Metrics To HealthStats");
    }
    else
    {
        LOG_E(TAG,"Failed to Send Power Health Metrics To HealthStats");
    }

    if (health_analytics == false) {
        json_decref(root);
        LOG_D(TAG, "Health analytics is disabled");
    }
    else
    {
        char* power_health;
        power_health = json_dumps(root,JSON_REAL_PRECISION(9));

        if(is_automation_enabled)
        {
            ofstream power_file("/dev/shm/power.txt");
            power_file << string(power_health);
            power_file << " , " << get_system_time();
            power_file << "\n";
            power_file.flush();
        }

        health_analytics_publisher.setMessage(string(power_health));
        bool status = health_analytics_publisher.publish();

        free(power_health);
        json_decref(root);
    }
	return true;

}

bool send_apm_healthstats(apm_metrics_msg_t apm_metrics)
{   
    json_t * root = json_object();
    json_t* apm_info = json_object();
    char* apm_health_stats = NULL;

    json_object_set_new(apm_info, "ts", json_integer(apm_metrics.ts));
    json_object_set_new(apm_info, "upt", json_integer(apm_metrics.uptime));
    json_object_set_new(apm_info, "uid" , json_integer(apm_metrics.udid));
    json_object_set_new(apm_info, "sid", json_integer(apm_metrics.sid));
    json_object_set_new(apm_info, "src", json_integer(apm_metrics.src_type));
    std::string is_event = apm_metrics.is_event ? "true" : "false";
    json_object_set_new(apm_info, "isEvent" , json_string(is_event.c_str()));
    json_object_set_new(apm_info, "data", json_string(apm_metrics.data_str));
    json_object_set_new(apm_info, "state" , json_integer(apm_metrics.state));
    json_object_set_new(apm_info, "idx" , json_integer(apm_metrics.idx));
    json_object_set_new(apm_info, "didx" , json_integer(apm_metrics.decision_idx));
    json_object_set_new(apm_info, "ofC" , json_integer(apm_metrics.off_cnt));
    json_object_set_new(apm_info, "onC" , json_integer(apm_metrics.on_cnt));
    json_object_set_new(apm_info, "idC", json_integer(apm_metrics.idl_dbn_cnt));
    json_object_set_new(apm_info, "pofC" , json_integer(apm_metrics.prev_off_cnt));
    json_object_set_new(apm_info, "poC" , json_integer(apm_metrics.prev_on_cnt));
    json_object_set_new(apm_info, "pidC", json_integer(apm_metrics.prev_idl_dbn_cnt));
    json_object_set_new(apm_info, "rC" , json_integer(apm_metrics.reader_cnt));
    json_object_set_new(apm_info, "gC" , json_integer(apm_metrics.generator_cnt));
    json_object_set_new(apm_info, "oC" , json_integer(apm_metrics.outage_cnt));
    json_object_set_new(root, "isArray" , json_string("true"));//to avoid overwrite of dictionary and persist as list of dicts
    json_object_set_new(root, "health_info:apm_info", apm_info);

    if(root != NULL)
    {
        apm_health_stats = json_dumps(root, JSON_REAL_PRECISION(9));
        int length = strlen(apm_health_stats);
        LOG_I(TAG,"Sending APM Health Metrics To HealthStats");
        bool ret = nd_service_obj->send_msg_healthstats(apm_health_stats, length);
        if(apm_health_stats != NULL)
        {
            free(apm_health_stats);
        }
        json_decref(root);
        return ret;
    }
    else
    {
        LOG_E(TAG, "APM Data Is NULL, Unable To Send APM Health Metrics To HealthStats");
        return false;
    }

}

bool publish_apm_metrics(apm_metrics_msg_t* apm_metrics) {
    LOG_D(TAG, "Publishing APM Metrics");
    LOG_D(TAG, "ts : %ld", apm_metrics->ts);
    LOG_D(TAG, "upt : %ld", apm_metrics->uptime);
    LOG_D(TAG, "udid : %d", apm_metrics->udid);
    LOG_D(TAG, "sid : %d", apm_metrics->sid);
    LOG_D(TAG, "src_type : %d", apm_metrics->src_type);
    LOG_D(TAG, "is_event : %d", apm_metrics->is_event);
    LOG_D(TAG, "data_str : %s", apm_metrics->data_str);
    LOG_D(TAG, "state : %d", apm_metrics->state);
    LOG_D(TAG, "idx : %d", apm_metrics->idx);
    LOG_D(TAG, "decision_idx : %d", apm_metrics->decision_idx);
    LOG_D(TAG, "off_cnt : %d", apm_metrics->off_cnt);
    LOG_D(TAG, "on_cnt : %d", apm_metrics->on_cnt);
    LOG_D(TAG, "idl_dbn_cnt : %d", apm_metrics->idl_dbn_cnt);
    LOG_D(TAG, "prev_off_cnt : %d", apm_metrics->prev_off_cnt);
    LOG_D(TAG, "prev_on_cnt : %d", apm_metrics->prev_on_cnt);
    LOG_D(TAG, "prev_idl_dbn_cnt : %d", apm_metrics->prev_idl_dbn_cnt);
    LOG_D(TAG, "reader_cnt : %d", apm_metrics->reader_cnt);
    LOG_D(TAG, "generator_cnt : %d", apm_metrics->generator_cnt);
    LOG_D(TAG, "outage_cnt : %d", apm_metrics->outage_cnt);

    if (send_apm_healthstats(*apm_metrics))
    {
        LOG_I(TAG,"Successfully Sent APM Health Metrics To HealthStats");
    }
    else
    {
        LOG_E(TAG,"Failed to Send APM Health Metrics To HealthStats");
    }
    //TODO : send to analytics
	return true;
}

bool send_apm_thres_healthstats(const apm_thresholds_msg_t& apm_thresholds) {
    json_t * root = json_object();
    json_t* apm_thres_info = json_object();
    char* apm_thres_health_stats = NULL;

    json_object_set_new(apm_thres_info, "ts", json_integer(apm_thresholds.ts));
    json_object_set_new(apm_thres_info, "upt", json_integer(apm_thresholds.uptime));
    json_object_set_new(apm_thres_info, "uid" , json_integer(apm_thresholds.udid));
    json_object_set_new(apm_thres_info, "sid", json_integer(apm_thresholds.sid));
    json_object_set_new(apm_thres_info, "src", json_integer(apm_thresholds.src_type));
    json_object_set_new(apm_thres_info, "on_wd", json_integer(apm_thresholds.on_wd));
    json_object_set_new(apm_thres_info, "off_wd", json_integer(apm_thresholds.off_wd));
    json_object_set_new(apm_thres_info, "outage_wd", json_integer(apm_thresholds.outage_wd));
    json_object_set_new(apm_thres_info, "valid_wd", json_integer(apm_thresholds.valid_dbn_wd));
    json_object_set_new(apm_thres_info, "sleep_wd   ", json_integer(apm_thresholds.sleep_wd));
    json_object_set_new(apm_thres_info, "hyst_wd", json_integer(apm_thresholds.hysterisis_wd));
    json_object_set_new(apm_thres_info, "cont_wd", json_integer(apm_thresholds.continuous_wd));
    json_object_set_new(apm_thres_info, "cache_wd", json_integer(apm_thresholds.data_cache_wd));
    json_object_set_new(apm_thres_info, "thres_str", json_string(apm_thresholds.thres_str));
    json_object_set_new(root, "isArray" , json_string("true"));// to avoid overwrite of dictionary and persist as list of dicts
    json_object_set_new(root, "health_info:apm_thres_info", apm_thres_info);

    if(root != NULL)
    {
        apm_thres_health_stats = json_dumps(root, JSON_REAL_PRECISION(9));
        int length = strlen(apm_thres_health_stats);
        LOG_I(TAG,"Sending APM Thresholds To HealthStats");
        bool ret = nd_service_obj->send_msg_healthstats(apm_thres_health_stats, length);
        if(apm_thres_health_stats != NULL)
        {
            free(apm_thres_health_stats);
        }
        json_decref(root);
        return ret;
    }
    else
    {
        LOG_E(TAG, "APM Thresholds Data Is NULL, Unable To Send APM Thresholds To HealthStats");
        return false;
    }

}

bool publish_apm_thresholds(apm_thresholds_msg_t* apm_thresholds) {

    LOG_D(TAG, "Publishing APM Thresholds");
    LOG_D(TAG, "ts : %ld", apm_thresholds->ts);
    LOG_D(TAG, "uptime : %ld", apm_thresholds->uptime);
    LOG_D(TAG, "udid : %ld", apm_thresholds->udid);
    LOG_D(TAG, "sid : %ld", apm_thresholds->sid);
    LOG_D(TAG, "src_type : %d", apm_thresholds->src_type);
    LOG_D(TAG, "on_wd : %d", apm_thresholds->on_wd);
    LOG_D(TAG, "off_wd : %d", apm_thresholds->off_wd);
    LOG_D(TAG, "outage_wd : %d", apm_thresholds->outage_wd);
    LOG_D(TAG, "valid_dbn_wd : %d", apm_thresholds->valid_dbn_wd);
    LOG_D(TAG, "sleep_wd : %d", apm_thresholds->sleep_wd);
    LOG_D(TAG, "hysterisis_wd : %d", apm_thresholds->hysterisis_wd);
    LOG_D(TAG, "continuous_wd : %d", apm_thresholds->continuous_wd);
    LOG_D(TAG, "data_cache_wd : %d", apm_thresholds->data_cache_wd);
    LOG_D(TAG, "thres_str : %s", apm_thresholds->thres_str);
    if (send_apm_thres_healthstats(*apm_thresholds))
    {
        LOG_I(TAG,"Successfully Sent APM Thresholds To HealthStats");
    }
    else
    {
        LOG_E(TAG,"Failed to Send APM Thresholds To HealthStats");
    }
    //TODO : send to analytics
    return true;
}

bool send_wom_healthstats(wom_metrics_msg_t wom_metrics) {
    json_t *root = json_object();
    json_t* wom_info = json_object();
    char* wom_health_stats = NULL;

    json_object_set_new(wom_info, "ts", json_integer(wom_metrics.ts));
    json_object_set_new(wom_info, "upt", json_integer(wom_metrics.uptime));
    json_object_set_new(wom_info, "uid" , json_integer(wom_metrics.udid));
    json_object_set_new(wom_info, "sid", json_integer(wom_metrics.sid));
    json_object_set_new(wom_info, "vclass", json_integer(wom_metrics.veh_class));
    json_object_set_new(wom_info, "event" , json_integer(wom_metrics.event));
    json_object_set_new(wom_info, "xs", json_integer(wom_metrics.x_stat));
    json_object_set_new(wom_info, "ys" , json_integer(wom_metrics.y_stat));
    json_object_set_new(wom_info, "zs" , json_integer(wom_metrics.z_stat));
    json_object_set_new(wom_info, "xtb" , json_integer(wom_metrics.x_thr_boot));
    json_object_set_new(wom_info, "ytb" , json_integer(wom_metrics.y_thr_boot));
    json_object_set_new(wom_info, "ztb" , json_integer(wom_metrics.z_thr_boot));
    json_object_set_new(wom_info, "xts", json_integer(wom_metrics.x_thr_set));
    json_object_set_new(wom_info, "yts" , json_integer(wom_metrics.y_thr_set));
    json_object_set_new(wom_info, "zts" , json_integer(wom_metrics.z_thr_set));
    json_object_set_new(root, "isArray" , json_string("true"));//to avoid overwrite of dictionary and persist as list of dicts
    json_object_set_new(root, "health_info:wom_info", wom_info);

    if(root != NULL)
    {
        wom_health_stats = json_dumps(root, JSON_REAL_PRECISION(9));
        int length = strlen(wom_health_stats);
        LOG_I(TAG,"Sending WOM Health Metrics To HealthStats");
        bool ret = nd_service_obj->send_msg_healthstats(wom_health_stats, length);
        if(wom_health_stats != NULL)
        {
            free(wom_health_stats);
        }
        json_decref(root);
        return ret;
    }
    else
    {
        LOG_E(TAG, "WOM Data Is NULL, Unable To Send WOM Health Metrics To HealthStats");
        return false;
    }

}

bool publish_wom_metrics(wom_metrics_msg_t* wom_metrics) {

    LOG_D(TAG, "Publishing WOM Metrics");
    LOG_D(TAG, "ts : %ld", wom_metrics->ts);
    LOG_D(TAG, "upt : %ld", wom_metrics->uptime);
    LOG_D(TAG, "udid : %d", wom_metrics->udid);
    LOG_D(TAG, "sid : %d", wom_metrics->sid);
    LOG_D(TAG, "veh_class : %d", wom_metrics->veh_class);
    LOG_D(TAG, "event : %d", wom_metrics->event);
    LOG_D(TAG, "x_stat : %d", wom_metrics->x_stat);
    LOG_D(TAG, "y_stat : %d", wom_metrics->y_stat);
    LOG_D(TAG, "z_stat : %d", wom_metrics->z_stat);
    LOG_D(TAG, "x_thr_boot : %d", wom_metrics->x_thr_boot);
    LOG_D(TAG, "y_thr_boot : %d", wom_metrics->y_thr_boot);
    LOG_D(TAG, "z_thr_boot : %d", wom_metrics->z_thr_boot);
    LOG_D(TAG, "x_thr_set : %d", wom_metrics->x_thr_set);
    LOG_D(TAG, "y_thr_set : %d", wom_metrics->y_thr_set);
    LOG_D(TAG, "z_thr_set : %d", wom_metrics->z_thr_set);

    if (send_wom_healthstats(*wom_metrics))
    {
        LOG_I(TAG,"Successfully Sent WOM Health Metrics To HealthStats");
    }
    else
    {
        LOG_E(TAG,"Failed to Send WOM Health Metrics To HealthStats");
    }
    //TODO : send to analytics
    return true;
}

static int64_t root_count = 0;
static const int64_t ROOT_FS_CHECK_MSG_COUNT = 3600; //1 hour

// Safe sync function that preserves folder structure (rm -rf protection)
bool safe_sync_directories(const std::string& source_dir, const std::string& dest_dir, const std::string& description) {
	LOG_I(TAG, "Starting safe sync: %s", description.c_str());
	LOG_I(TAG, "Source: %s -> Destination: %s", source_dir.c_str(), dest_dir.c_str());

	// First ensure destination directory exists
	std::string create_dest_cmd = "mkdir -p \"" + dest_dir + "\"";
	int create_result = system(create_dest_cmd.c_str());
	if (create_result != 0) {
		LOG_E(TAG, "Failed to create destination directory: %s", dest_dir.c_str());
		return false;
	}
	// Step 1: Delete only FILES in destination (preserve folders)
	// Use find to delete files but not directories - FOLDER STRUCTURE PROTECTION
	std::string safe_cleanup_cmd = "find \"" + dest_dir + "\" -type f -delete";
	LOG_I(TAG, "Safe cleanup (files only): %s", safe_cleanup_cmd.c_str());
	int cleanup_result = system(safe_cleanup_cmd.c_str());
	if (cleanup_result != 0) {
		LOG_W(TAG, "Safe cleanup had some issues but continuing...");
	}

	// Step 2: Use rsync to sync content while preserving folder structure
	// Sync from source to destination without deleting destination-only files
	std::string rsync_cmd = "rsync -av \"" + source_dir + "/\" \"" + dest_dir + "/\"";
	LOG_I(TAG, "Rsync command: %s", rsync_cmd.c_str());
	int sync_result = system(rsync_cmd.c_str());

	if (sync_result == 0) {
		LOG_I(TAG, "Safe sync completed successfully: %s", description.c_str());
		return true;
	} else {
		LOG_E(TAG, "Safe sync failed: %s (exit code: %d)", description.c_str(), sync_result);
		return false;
	}
}

// Force disable all overlay services for emergency scenarios
bool force_disable_overlay_services() {
	LOG_I(TAG, "Force disabling all overlay services");
	std::vector<std::string> overlay_services = {
		"var-log.mount",
		"home-ubuntu-.nddevice-log.mount",
		"var-backups.mount",
		"var-tmp.mount",
		"sync-early@var-log.service",
		"sync-early@nddevice-log.service",
		"sync-early@var-backups.service",
		"sync-early@var-tmp.service"
	};

	bool all_success = true;
	for (const std::string& service : overlay_services) {
		// Force stop and mask mount services, disable sync services
		if (service.find(".mount") != std::string::npos) {
			std::string stop_cmd = "systemctl stop " + service;
			std::string mask_cmd = "systemctl mask " + service;
			if (system(stop_cmd.c_str()) != 0) {
				LOG_W(TAG, "Failed to stop %s", service.c_str());
				all_success = false;
			}
			if (system(mask_cmd.c_str()) != 0) {
				LOG_W(TAG, "Failed to mask %s", service.c_str());
				all_success = false;
			}
		} else {
			bool unused = false;
			if (!disable_service(service, unused)) {
				LOG_W(TAG, "Failed to disable %s", service.c_str());
				all_success = false;
			}
		}
	}

	LOG_I(TAG, "Force disable overlay services: %s", all_success ? "SUCCESS" : "PARTIAL");
	return all_success;
}

void run_overlay_management_wrapper(bool mounts_enabled) {
	LOG_I(TAG, "Running overlay management wrapper (mounts_enabled: %s)", mounts_enabled ? "true" : "false");

	if (mounts_enabled) {
		bool overlay_config_success = configure_overlay_filesystem_services(mounts_enabled);
		if (overlay_config_success) {
			LOG_D(TAG, "Overlay filesystem services configured successfully");
		} else {
			LOG_C(TAG, "Overlay filesystem services configuration had failures");
		}

		// Check live status of overlay services
		bool overlay_status_check = check_overlay_services_status(mounts_enabled);
		if (overlay_status_check) {
			LOG_D(TAG, "Overlay services status check passed");
		} else {
			LOG_W(TAG, "Overlay services status check found inconsistencies");
		}
	} else {
		LOG_I(TAG, "Overlay filesystem services configuration is disabled from the config");
		bool overlay_config_success = configure_overlay_filesystem_services(mounts_enabled);
		if (overlay_config_success) {
			LOG_D(TAG, "Overlay filesystem services configured successfully");
		} else {
			LOG_C(TAG, "Overlay filesystem services configuration had failures");
		}
	}
}

void periodic_overlay_management_thread(bool mounts_enabled) {
	LOG_D(TAG, "Starting periodic overlay management thread (5-minute intervals)");
	LOG_D(TAG, "Using mounts_enabled: %s", mounts_enabled ? "true" : "false");

	int counter = 0;
	const int FIVE_MINUTES = 300; // 5 minutes in seconds

	while (true) {
		try {
			// Sleep for 1 second and increment counter
			std::this_thread::sleep_for(std::chrono::seconds(1));
			counter++;

			// Check if 5 minutes have passed
			if (counter >= FIVE_MINUTES) {
				counter = 0; // Reset counter

				LOG_D(TAG, "=== PERIODIC OVERLAY MANAGEMENT (5-MINUTE CHECK) ===");

				// Run the overlay management wrapper
				run_overlay_management_wrapper(mounts_enabled);

				LOG_D(TAG, "Periodic overlay management check completed");
			}

		} catch (const std::exception& e) {
			LOG_E(TAG, "Error in periodic overlay management thread: %s", e.what());
		} catch (...) {
			LOG_E(TAG, "Unknown error in periodic overlay management thread");
		}
	}
}

/*
The below functions does
1) Enable/Disable overlay filesystem related services based on the configuration
2) Check the status of overlay filesystem related services and compare with expected state
3) Retry logic for enabling/disabling services up to 3 times
4) Logging of service configuration and status results
5) Return overall success/failure of operations
6) Handles both mount services and template services appropriately
7) Separate handling for critical and non-critical services
8) Combines configuration and status checking into a single function for efficiency
9) if configure_services is true, it will enable/disable services based on expected_overlay_state
10) if check_status is true, it will check the status of services and compare with expected_overlay_state
11) expected_overlay_state indicates whether overlay filesystem is expected to be enabled (true)
*/

bool manage_overlay_filesystem_services(bool configure_services = true, bool check_status = true ,  bool expected_overlay_state = false) {
	LOG_I(TAG, "Managing overlay filesystem services (configure: %s, check_status: %s)",
		configure_services ? "true" : "false", check_status ? "true" : "false");

	LOG_I(TAG, "eMMC overlay services from config: %d", expected_overlay_state);

	// Define all services with their paths and critical status
	std::vector<std::tuple<std::string, std::string, bool>> all_services = {
		std::make_tuple("var-log.mount", "/lib/systemd/system/", true),           // critical
		std::make_tuple("home-ubuntu-.nddevice-log.mount", "/lib/systemd/system/", true), // critical
		std::make_tuple("var-backups.mount", "/lib/systemd/system/", false),      // non-critical
		std::make_tuple("var-tmp.mount", "/lib/systemd/system/", false),          // non-critical
		std::make_tuple("sync-early@.service", "/etc/systemd/system/", false)     // template service
	};

	// Define template service instances to enable/disable separately
	std::vector<std::string> template_instances = {
		"sync-early@var-log.service",
		"sync-early@nddevice-log.service", 
		"sync-early@var-backups.service",
		"sync-early@var-tmp.service"
	};

	// Lambda for service operations with retry logic
	auto configure_service = [&](const std::string& service_name, bool should_enable) -> bool {
		const int max_retries = 3;
		for (int attempt = 1; attempt <= max_retries; attempt++) {
			bool operation_status = false;
			bool is_service_start_required = false;

			if (should_enable) {
					// For enable: unmask first (in case it was masked), then enable
					if (service_name.find(".mount") != std::string::npos) {
						unmask_service(service_name);
					}
					operation_status = enable_service(service_name, is_service_start_required);
				} else {
					// For disable: mask mount services to prevent dependency activation, disable others
					if (service_name.find(".mount") != std::string::npos) {
						operation_status = mask_service(service_name);
					} else {
						operation_status = disable_service(service_name, is_service_start_required);
					}
				}

				if (operation_status) {
					LOG_D(TAG, "%s %s successfully on attempt %d", 
						service_name.c_str(), should_enable ? "enabled" : (service_name.find(".mount") != std::string::npos ? "masked" : "disabled"), attempt);
					return true;
				} else {
					LOG_W(TAG, "Failed to %s %s on attempt %d",
						should_enable ? "enable" : (service_name.find(".mount") != std::string::npos ? "mask" : "disable"), service_name.c_str(), attempt);
					if (attempt < max_retries) {
						LOG_D(TAG, "Retrying %s in 1 second...", service_name.c_str());
						sleep(1);
					}
				}
			}
			LOG_C(TAG, "Failed to configure %s after %d attempts", service_name.c_str(), 3);
			return false;
		};

	// Lambda for bulk service operations
	auto bulk_disable_services = [&](bool critical_only = false) -> void {
		for (auto it = all_services.begin(); it != all_services.end(); ++it) {
			const std::string& service_name = std::get<0>(*it);
			const std::string& service_path = std::get<1>(*it);
			bool is_critical = std::get<2>(*it);

			if (critical_only && !is_critical) continue;

			std::ifstream service_file((service_path + service_name).c_str());
			if (!service_file.good()) continue;

			LOG_I(TAG, "Disabling service: %s", service_name.c_str());
			configure_service(service_name, false);
		}
	};

	// Service tracking variables
	bool all_services_configured = true;
	bool all_services_match_expected = true;
	bool overall_config_success = true;
	bool critical_service_failed = false;
	int active_services = 0;
	int inactive_services = 0;
	int missing_services = 0;

	// First pass: analyze all services
	for (auto it = all_services.begin(); it != all_services.end(); ++it) {
		const std::string& service_name = std::get<0>(*it);
		const std::string& service_path = std::get<1>(*it);
		bool is_critical = std::get<2>(*it);
		const std::string full_service_path = service_path + service_name;
		std::ifstream service_file(full_service_path.c_str());

		if (!service_file.good()) {
			if (check_status) {
				LOG_W(TAG, "Service file %s not found at %s", service_name.c_str(), full_service_path.c_str());
				missing_services++;
			}
			continue;
		}

		// Check current service states
		bool is_currently_enabled = is_service_enabled(service_name);
		bool is_currently_active = is_service_active(service_name);

		if (check_status) {
			LOG_D(TAG, "%s - Enabled: %s, Active: %s", service_name.c_str(),
				is_currently_enabled ? "YES" : "NO", is_currently_active ? "YES" : "NO");

			if (is_currently_active) active_services++;
			else inactive_services++;

			if (expected_overlay_state) {
				if (!is_currently_enabled) {
					LOG_W(TAG, "%s should be enabled but is disabled", service_name.c_str());
					all_services_match_expected = false;
				}
			} else {
				if (is_currently_enabled) {
					LOG_W(TAG, "%s should be disabled but is enabled", service_name.c_str());
					all_services_match_expected = false;
				}
			}
		}

		if (configure_services) {
			bool needs_configuration = (expected_overlay_state && !is_currently_enabled) ||
									  (!expected_overlay_state && is_currently_enabled);

			if (needs_configuration) {
				all_services_configured = false;
				LOG_I(TAG, "Configuring service %s (%s)", service_name.c_str(),
					  expected_overlay_state ? "enabling" : "disabling");

				bool service_success = configure_service(service_name, expected_overlay_state);

				if (!service_success) {
					overall_config_success = false;
					if (is_critical && expected_overlay_state) {
						LOG_C(TAG, "Critical service %s failed", service_name.c_str());
						critical_service_failed = true;
					}
				}
			}
		}
	}

	// Handle template service instances separately (they don't have physical files)
	for (const std::string& instance_name : template_instances) {
		bool is_currently_enabled = is_service_enabled(instance_name);

		if (configure_services) {
			bool needs_configuration = (expected_overlay_state && !is_currently_enabled) ||
									  (!expected_overlay_state && is_currently_enabled);

			if (needs_configuration) {
				all_services_configured = false;
				LOG_I(TAG, "Configuring template instance %s (%s)", instance_name.c_str(),
					  expected_overlay_state ? "enabling" : "disabling");

				bool service_success = configure_service(instance_name, expected_overlay_state);

				if (!service_success) {
					overall_config_success = false;
					LOG_I(TAG, "Template service instance %s failed", instance_name.c_str());
				}
			}
		}

		// Update status tracking for template instances
		if (check_status) {
			bool is_currently_active = is_service_active(instance_name);
			LOG_I(TAG, "%s - Enabled: %s, Active: %s", instance_name.c_str(),
				is_currently_enabled ? "YES" : "NO", is_currently_active ? "YES" : "NO");

			if (is_currently_active) active_services++;
			else inactive_services++;

			if (expected_overlay_state && !is_currently_enabled) {
				LOG_I(TAG, "%s should be enabled but is disabled", instance_name.c_str());
				all_services_match_expected = false;
			} else if (!expected_overlay_state && is_currently_enabled) {
				LOG_I(TAG, "%s should be disabled but is enabled", instance_name.c_str());
				all_services_match_expected = false;
			}
		}
	}

	// Handle configuration results
	if (configure_services) {
		if (all_services_configured) {
			LOG_I(TAG, "All overlay filesystem services are already in the desired state");
		} else if (overall_config_success) {
			LOG_I(TAG, "All overlay filesystem services configured successfully");
		} else {
			LOG_C(TAG, "Some overlay filesystem services failed to configure");

			if (critical_service_failed) {
				LOG_C(TAG, "Critical service failure - reverting ALL overlay services");
				bulk_disable_services();
			} else if (expected_overlay_state) {
				LOG_I(TAG, "Non-critical services failed - cleaning up failed services only");
				// Disable only the failed non-critical services
				for (auto it = all_services.begin(); it != all_services.end(); ++it) {
					const std::string& service_name = std::get<0>(*it);
					bool is_critical = std::get<2>(*it);

					if (!is_critical && !is_service_enabled(service_name)) {
						LOG_I(TAG, "Cleaning up failed service: %s", service_name.c_str());
						configure_service(service_name, false);
					}
				}
			}
		}
	}

	// Status check phase - log summary
	if (check_status) {
		LOG_I(TAG, "Overlay Services Status Summary:");
		LOG_I(TAG, "  Expected state: %s", expected_overlay_state ? "ENABLED" : "DISABLED");
		LOG_I(TAG, "  Active services: %d", active_services);
		LOG_I(TAG, "  Inactive services: %d", inactive_services);
		LOG_I(TAG, "  Missing services: %d", missing_services);
		LOG_I(TAG, "  Configuration matches live status: %s", all_services_match_expected ? "YES" : "NO");

		if (missing_services > 0) {
			LOG_C(TAG, "%d overlay service files are missing from the system", missing_services);
		}

		if (!all_services_match_expected) {
			LOG_C(TAG, "Some overlay services don't match the expected configuration state");
		} else {
			LOG_I(TAG, "All overlay services match the expected configuration state");
		}
	}

	// Embedded overlay scenario management when configuring services
	if (configure_services) {
		LOG_I(TAG, "=== OVERLAY SCENARIO MANAGEMENT===");

		// Check if external eMMC is down using existing nd_device_obj function
		bool external_emmc_down = nd_device_obj->is_external_eMMC_down();
		LOG_I(TAG, "External eMMC status: %s", external_emmc_down ? "DOWN" : "UP");

		// Initialize state tracking on first call
		if (!emmc_state_initialized) {
			emmc_was_down_this_boot = external_emmc_down;
			emmc_state_initialized = true;
			LOG_I(TAG, "eMMC state tracking initialized. Initial state: %s", external_emmc_down ? "DOWN" : "UP");
		}

		// SCENARIO 2: External eMMC down -> immediately stop and disable overlays on every boot
		if (external_emmc_down) {
			emmc_was_down_this_boot = true;
			LOG_I(TAG, "eMMC marked as down during this boot");
			LOG_I(TAG, "SCENARIO 2: External eMMC down - immediately stopping and disabling overlays");

			// Force stop and disable all overlay services immediately on every boot when eMMC is down
			LOG_I(TAG, "Force stopping and disabling overlay services due to eMMC down");
			force_disable_overlay_services();

			// Override the expected_overlay_state to prevent enabling when eMMC is down
			expected_overlay_state = false;
			LOG_I(TAG, "Overriding expected_overlay_state to false due to eMMC down");
		} else if (emmc_was_down_this_boot) {
			// SCENARIO 3: External eMMC back (was down, now up) -> forward sync and enable
			LOG_I(TAG, "SCENARIO 3: External eMMC came back up (was down this boot) - performing forward sync");

			// Forward sync from internal to external overlay upper directories
			std::vector<std::pair<std::string, std::string>> forward_mappings = {
				{"/var/log", "/media/data/overlay_var_log/upper"},
				{"/home/ubuntu/.nddevice/log", "/media/data/overlay_nddevice_log/upper"},
				{"/var/backups", "/media/data/overlay_var_backups/upper"},
				{"/var/tmp", "/media/data/overlay_var_tmp/upper"}
			};

			bool forward_sync_success = true;
			for (const auto& mapping : forward_mappings) {
				const std::string& source = mapping.first;
				const std::string& dest = mapping.second;

				if (!safe_sync_directories(source, dest, "Internal to External: " + source)) {
					forward_sync_success = false;
				}
			}

			if (forward_sync_success) {
				LOG_I(TAG, "Forward sync successful - overlay services will be enabled by normal flow");
				// Reset the flag since eMMC recovery is complete
				emmc_was_down_this_boot = false;
				LOG_I(TAG, "Reset eMMC down flag - ready to detect future down/up cycles this boot");
			} else {
				LOG_E(TAG, "Forward sync failed - overlay services may not work properly");
			}
		} else {
			// SCENARIO 1 CHECK: eMMC is up - check if overlays are actually failing (only when trying to enable)
			if (expected_overlay_state) {
				bool overlay_services_ok = check_overlay_services_status(true); // expect enabled
				if (!overlay_services_ok) {
					LOG_I(TAG, "SCENARIO 1: External eMMC up but overlays failing - syncing upper to internal");

					// Sync overlay upper directories to internal storage  
					std::vector<std::pair<std::string, std::string>> overlay_mappings = {
						{"/media/data/overlay_var_log/upper", "/var/log"},
						{"/media/data/overlay_nddevice_log/upper", "/home/ubuntu/.nddevice/log"},
						{"/media/data/overlay_var_backups/upper", "/var/backups"},
						{"/media/data/overlay_var_tmp/upper", "/var/tmp"}
					};

					bool sync_success = true;
					for (const auto& mapping : overlay_mappings) {
						const std::string& source = mapping.first;
						const std::string& dest = mapping.second;

						// Check if source exists before syncing
						struct stat st;
						if (stat(source.c_str(), &st) == 0) {
							if (!safe_sync_directories(source, dest, "Upper to Internal: " + source)) {
								sync_success = false;
							}
						} else {
							LOG_W(TAG, "Overlay upper directory not found: %s", source.c_str());
						}
					}

					LOG_I(TAG, "Scenario 1 upper-to-internal sync: %s", sync_success ? "SUCCESS" : "PARTIAL");
				} else {
					// eMMC is up and overlays are working fine - normal service configuration
					LOG_I(TAG, "External eMMC up and overlays working - normal overlay configuration");
				}
			} else {
				// We're trying to disable overlays and eMMC is up - normal disable flow
				LOG_I(TAG, "External eMMC up - proceeding with overlay disable request");
			}
		}
	}

	// Return combined result
	bool status_check_result = check_status ? (all_services_match_expected && (missing_services == 0)) : true;
	bool config_result = configure_services ? overall_config_success : true;

	return config_result && status_check_result;
}

/*
1) Wrapper function to configure overlay filesystem services
2) Calls manage_overlay_filesystem_services with appropriate parameters
3) On overlay_state true, services will be enabled; on false, services will be disabled
*/
bool configure_overlay_filesystem_services(bool expected_overlay_state) {
	return manage_overlay_filesystem_services(true, false , expected_overlay_state );
}

bool check_overlay_services_status( bool expected_overlay_state) {
	return manage_overlay_filesystem_services(false, true, expected_overlay_state );
}

void diagnostic_msg_loop()
{
    LOG_D(TAG, "entered into diagnostic_msg_loop");

    nd_msgq_t::nd_msg_t *msg;
    string messenger_addr_analytics;
    string messenger_topic_health_analytics;
    get_analytics_msg_server(messenger_addr_analytics);
    get_health_analytics_msg_topic(messenger_topic_health_analytics);

    health_analytics_publisher.setServer(messenger_addr_analytics);
    health_analytics_publisher.setTopic(messenger_topic_health_analytics);

   power_metrics_msg_t* power_metrics = nullptr;
    apm_metrics_msg_t* apm_metrics = nullptr;
    wom_metrics_msg_t* wom_metrics = nullptr;
    res_dhub_health_msg_t* dhub_metrics = nullptr;

   while(1) {

        if( (msg = server_q->receive( )) == NULL ) {
            LOG_C(TAG, "Receive message failed");
            break;
        }

        else {
            LOG_I(TAG, "Receive message Success. ");
        }
        diagnostic_generic_msg_t *g_msg = (diagnostic_generic_msg_t *)msg->get_buffer();
        if(g_msg == NULL) {
            LOG_E(TAG, "Empty message received in CB ");
            continue;
        }
        LOG_I(TAG, "g_msg->len %d g_msg->type %d", g_msg->len, g_msg->type); 

        switch( g_msg->type ) {

            case REQ_CIRCBUFF_SDCARD_MOUNT:
            {
#ifdef BAGHEERA
                LOG_I(TAG, " received REQ_CIRCBUFF_SDCARD_MOUNT: %d ", REQ_CIRCBUFF_SDCARD_MOUNT );
                std::lock_guard<std::mutex> lk(sd_mutex);
                sd_cond.notify_one(); 
#endif
                break;
            }
            case REQ_CIRCULAR_BUFFER_SDCARD_FSCK_CHECK:
                LOG_I(TAG, "creating thread to run fsck command to check for filesystem errors");
                fork_fsck_run_process();
            break;

            case REQ_CIRCBUFF_FSCK_RUN_MOUNT_FAIL:
            {
                LOG_I(TAG, "creating thread to run fsck command to check for filesystem errors");
                fork_fsck_image_correction_run_process(false);

                break;
            }
            case REQ_CIRCBUFF_FSCK_RUN_RESIZE_FAIL:
            {
                LOG_I(TAG, "creating thread to run fsck command to check for filesystem errors and resize nd_sdcard.img");
                fork_fsck_image_correction_run_process(true);
                break;
            }
            case RES_POWERMON_METRICS:
            {
                LOG_I(TAG, "Received RES_POWERMON_METRICS");
		        power_metrics = (power_metrics_msg_t*) g_msg;
                publish_power_metrics(power_metrics, messenger_addr_analytics, messenger_topic_health_analytics);
                break;
            }
            case RES_APM_METRICS:
            {
                LOG_I(TAG, "Received RES_APM_METRICS");
                apm_metrics = (apm_metrics_msg_t*) g_msg;
                publish_apm_metrics(apm_metrics);
                break;
            }
            case RES_APM_THRESHOLDS:
            {
                LOG_I(TAG, "Received RES_APM_THRESHOLDS");
                apm_thresholds_msg_t* apm_thresholds = (apm_thresholds_msg_t*) g_msg;
                publish_apm_thresholds(apm_thresholds);
                break;
            }
            case RES_WOM_METRICS:
            {
                LOG_I(TAG, "Received RES_WOM_METRICS");
                wom_metrics = (wom_metrics_msg_t*) g_msg;
                publish_wom_metrics(wom_metrics);
                break;
            }
            case RES_DHUB_INFO:
            {
                SKIP_IF_HEALTH_ANALYTICS_DISABLED();
                LOG_D(TAG, "Received DHUB_INFO");
                dhub_msg_received = true;
		        dhub_metrics = (res_dhub_health_msg_t*) g_msg;
		        publish_dhub_metrics(dhub_metrics, messenger_addr_analytics, messenger_topic_health_analytics);
                break;
            }
            case RES_STORAGE_INFO:
            {
                LOG_D(TAG, "Received STORAGE_INFO");
                res_storage_health_msg_t* storage_metrics = (res_storage_health_msg_t*) g_msg;
                publish_storage_metrics(storage_metrics, power_metrics, dhub_metrics, messenger_addr_analytics, messenger_topic_health_analytics);
                break;
            }                
            case RESET_MASTERDATA:
            {
                LOG_I(TAG, "Resetting master data"); //receive message from the the HS to reset master data after every 10 minutes or after health stats is restarted
                reset_master_data = true;
                break;
            }
                
            default:
            LOG_E(TAG, "default %d,  ", g_msg->type );
            break;
            
        }
        
        if((root_count % ROOT_FS_CHECK_MSG_COUNT)== 0) {
          
        
        if (nd_factory_utils::is_readonly()) {
            string critical_msg = "Root filesystem is RO";
            LOG_C(TAG, "%s", critical_msg.c_str());
            
            // Send critical error to Service Manager
            nd_service_obj->send_err_msg(SM_E_CB_ROOT_FS_READ_ONLY, NDService::UNUSED_ERR_AUX_CODE, critical_msg);
    
        }
    } root_count++;
}
#ifdef BAGHEERA
bool check_sdcard_status(){

   int ret =0;
   char str1[1000];

   static const string sdcard_test = "ls /dev/ | grep mmcblk1p1 > sdcard.txt";

   ret = system_execute("checking sdcard status", sdcard_test);
   if (ret !=0){
      LOG_I(TAG, "system call failed\n");
      return false;
   }

    FILE* in_file = fopen("sdcard.txt", "r");
    if (!in_file){
       LOG_E(TAG, "can not read sdcard file\n");
       return false;
    }

   while (fscanf(in_file, "%s", str1)!=EOF){
          LOG_I(TAG, "*********** %s *********\n", str1);
          if(strcmp(str1, "mmcblk1p1") == 0){
                fclose (in_file);
                system_execute("remove_sdcard.txt", "rm sdcard.txt");

                LOG_I(TAG, "first test for sd card done and sucessful\n");
                return true;

           }
           else {
                fclose (in_file);
                system_execute("remove_sdcard.txt", "rm sdcard.txt");
                LOG_I(TAG, "first test for sd card done and failed\n");
                return false;

           }
    }

   return false;

}
#endif
}
/* execute generic device test */
void generic_test(){

    string emmc_memory_used = "", sdcard_memory_used = "", free_ram = "";
    bool ret;
    int counter = 0;
    int fan_speed = 0;
    enum SIM_STATUS_CHECK sim_status_checked = modem_not_up ; 
    int simCheckFailCnt = 0;
    sleep(30); //time taken for initialization 

    LOG_I(TAG, "Execute generic device test start");


#ifdef BAGHEERA
    ret = check_sdcard_status();
    if(ret == true) {
        LOG_I(TAG, "sdcard is connected");
    } else {
        LOG_E(TAG, "Failed to detect sdcard");
    }
#endif
    while(1) {
#if 0
        if(sim_status_checked == modem_not_up){
            if(file_is_present(modem_devnode_path) == true) {
                sim_status_checked = modem_up ;
                ret = nd_device_obj->check_sim_status();
                if(ret == true) {
                    LOG_I(TAG, "sim card connected");
                    sim_status_checked = sim_status_check_done ;
                } else {
                    LOG_I(TAG, "sim card not connected");
                }
            }
        }
        else if(sim_status_checked == modem_up) {
            if(file_is_present(modem_devnode_path) == true) {
                ret = nd_device_obj->check_sim_status();
                if(ret == true) {
                    LOG_I(TAG, "sim card is connected");
                    sim_status_checked = sim_status_check_done ;
                } else {
                    LOG_E(TAG, "Failed to detect simcard. simCheckFailCnt: %d", simCheckFailCnt);
                    simCheckFailCnt++;
                    if(simCheckFailCnt == 3){
                        sim_status_checked = sim_status_check_done ;
                        nd_service_obj->send_err_msg(SM_E_DIAG_SIM_NOT_DETECTED, NDService::UNUSED_ERR_AUX_CODE, "SIM card is not detected");
                    }
                }

            }

        }
#endif
        //Below Counter gets reset every 1 hr
        if(counter == 0) {
            int open_fd_count = nd_device_obj->count_open_file_descriptors(getpid());
            LOG_I(TAG, "Open file descriptor count: %d", open_fd_count);
            fan_speed = nd_device_obj->get_fan_status();
            if (fan_speed < 0) {
                LOG_E(TAG, "Failed to check FAN speed");
            } else {
                LOG_I(TAG, "FAN speed : %d", fan_speed);
		power_health_info.fan_spd = fan_speed;
            }
        }
        counter++;
        if(counter >= 60) {
            counter = 0;
        }
        ret = nd_device_obj->get_system_memory_info(emmc_memory_used, sdcard_memory_used, free_ram);
        if(ret == true) {
            LOG_I(TAG, "used memory of %s is %s", nd_device_obj->get_internal_eMMC_dev_node().c_str(),emmc_memory_used.c_str());
            LOG_I(TAG, "used memory of %s is %s", nd_device_obj->get_external_eMMC_dev_node().c_str(),sdcard_memory_used.c_str());
            LOG_I(TAG, "Free space in RAM is %s", free_ram.c_str());
        } else {
            LOG_E(TAG, "Failed to get system memory info");
        }

        fan_speed = -1;
        emmc_memory_used.clear();
        sdcard_memory_used.clear();
        free_ram.clear();
        sleep(60);
    }
    LOG_I(TAG, "Execute generic device test end");
}

int callback(void *NotUsed, int argc, char **argv, char **azColName){
   int i;
    string str = "" ;
   for(i=0; i<argc; i++){
    str += (argv[i] ? argv[i] : "NULL") ;
    str += " |";
   }
    LOG_I(TAG, "%s ", str.c_str() );
   return 0;
}

void* get_health_metrics ()
{
    LOG_I(TAG, "Initiating Health Collection For DriverI");
    int dhub_msg_id;
    hs_gen_time gen_time;

    int64_t hs_collection_time = get_system_time(); 
    gen_time.time              = hs_collection_time;
    power_health_info.ts       = hs_collection_time;
    storage_health_info.time   = hs_collection_time;
    int res = send_msg ((generic_msg_t *)&power_health_info, REQ_POWERMON_METRICS, sizeof(power_health_info), diagnostic_q_name, "q_power_monitor", 0);
    if(res == 0){
        LOG_E(TAG, "Error sending power metrics request");
    }
    else
    {
        LOG_I(TAG, "Power Metrics Request Message Sent");
    }
    res = send_msg ((generic_msg_t *)&gen_time, REQ_HEALTH_INFO, sizeof(gen_time), diagnostic_q_name, "CONN_MGR", 0);
    if(res == 0){
        LOG_E(TAG, "Error sending REQ_HEALTH_INFO request");
    }
    else
    {
        LOG_I(TAG, "REQ_HEALTH_INFO request Sent");
    }
    res = send_msg ((generic_msg_t *)&gen_time, REQ_HEALTH_INFO, sizeof(gen_time), diagnostic_q_name, "WIFI_MGR", 0);
    if(res == 0){
        LOG_E(TAG, "Error sending REQ_HEALTH_INFO request to WIFI_MGR");
    }
    else
    {
        LOG_I(TAG, "REQ_HEALTH_INFO request Sent");
    }
    res = send_msg ((generic_msg_t *)&gen_time, REQ_HEALTH_INFO, sizeof(gen_time), diagnostic_q_name, "Q_GPS", 0);
    if(res == 0){
        LOG_E(TAG, "Error sending REQ_HEALTH_INFO request to GPS");
    }
    else
    {
        LOG_I(TAG, "REQ_HEALTH_INFO request Sent to GPS");
    }

    dhub_health_info.time = hs_collection_time; 

    if(is_ext_cam_enabled)
    {
        res = send_msg((generic_msg_t *)&dhub_health_info, REQ_DHUB_INFO, sizeof(dhub_health_info), diagnostic_q_name, "EXT_CAM", 0);
        if(res == 0){
            LOG_E(TAG, "Error sending dhub metrics request");
        }
        else
        {
            LOG_D(TAG, "DHUB Metrics Request Message Sent");
        }
    }
    else
    {
        LOG_I(TAG, "External Camera Feature Is Not Enabled, Skipping DHUB Metrics Request");
    }

    if(count_sdcard % 5 == 0)
    {
        res = send_msg((generic_msg_t *)&storage_health_info, REQ_STORAGE_INFO, sizeof(storage_health_info), diagnostic_q_name, "q_circular_buffer", 0);
        if(res == 0){
            LOG_E(TAG, "Error Sending Storage Metrics Request");
        }
        else
        {
            LOG_I(TAG, "Storage Metrics Request Message Sent");
        }
    }
    count_sdcard++;
}

int main() {
    nd_service_obj = NDService::get_service_obj(TAG);
    service_start_time = get_system_monotonic_time();
    is_automation_enabled = isAutomationEnabled();
    is_ext_cam_enabled = is_ext_cam_feature_enabled();
    printf("initilizing logger\n");
    bool status_log = nd_log_init( log_dir.c_str() );
    if(status_log == false) {
            printf("unable to init logger :: Exiting from main");
            nd_service_obj->send_err_msg(SM_E_DIAG_LOG_INIT_FAIL, NDService::UNUSED_ERR_AUX_CODE, 
                "unable to init logger :: Exiting from main" );
            return 1;
    }

#ifdef ROUTE_LOGS
    route_logs( log_dir.c_str() );
#endif

    LOG_I(TAG, "############### starting diagnostic ################");

    nd_device_obj_init();

    read_health_analytics_config(health_analytics);    
    init_msgq();
    //////////////// DIAGNOSTIC DB SETUP ///////////////////////
    bool ret;
    string db_path_file = PM_DB_PATH_NAME;
    ret = ComponentBase::open_db(db_path_file.c_str(), &ComponentBase::db_handle);
    if(ret == false)    {
        // Notify health mon
        string str_msg = "DIAG Failed to open DB";
        LOG_C(TAG, str_msg.c_str() );
        nd_service_obj->send_err_msg(SM_E_DIAG_DB_OPEN_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
        ComponentBase::db_handle = NULL;
    }
    LOG_I(TAG, "success in open DB");
    if (pthread_mutex_init(&ComponentBase::db_handle_mutex, NULL) != 0){
        // Notify health mon
        string str_msg = "db_handle_mutex init failed ; Exiting from circ_buff";
        LOG_C(TAG, str_msg.c_str() );
        nd_service_obj->send_err_msg(SM_E_PM_DB_MUTEX_INIT_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
        ComponentBase::db_handle = NULL;
    }
    LOG_I(TAG, "success mutex init");
    ret = ComponentBase::create_table_db( ComponentBase::db_handle);
    if(ret == false)    {
        // Notify health mon
        string str_msg = "PM Failed to create_table_db";
        LOG_C(TAG, str_msg.c_str() );
        nd_service_obj->send_err_msg(SM_E_PM_DB_CREATION_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
        ComponentBase::db_handle = NULL;
    }
    LOG_I(TAG, "success in create_table_db");

    ComponentBase::db_limit_rows(ComponentBase::db_handle);
    //////////////// DIAGNOSTIC DB SETUP END ///////////////////////

    std::thread generic_test_thread(generic_test);
    bool val_overridden = false;
    Config_parser cfg_prsr(BAGHEERACONFIG_INI);
    int sdcard_diag_interval_time;
    string_to_integer(cfg_prsr.getConfig("diagnostic", "sdcard_diag_interval_time", "30", true, val_overridden).c_str(), sdcard_diag_interval_time);
	LOG_I(TAG, "sdcard_diag_interval_time: %d", sdcard_diag_interval_time);
    int sdcard_diag_start_time;
    string_to_integer(cfg_prsr.getConfig("diagnostic", "sdcard_diag_start_time", "30", true, val_overridden).c_str(), sdcard_diag_start_time);
	LOG_I(TAG, "sdcard_diag_start_time: %d", sdcard_diag_start_time);
    string enable_all_time_sdcard_error_state_log = "false" ;
    enable_all_time_sdcard_error_state_log = cfg_prsr.getConfig("diagnostic", "enable_all_time_sdcard_error_state_log", "false", true, val_overridden );
	LOG_I(TAG, "enable_all_time_sdcard_error_state_log: %s", enable_all_time_sdcard_error_state_log.c_str() );
    int cpugpuinfo_time;
    string_to_integer(cfg_prsr.getConfig("healthstats", "cpu_gpu_info_secs", "5", true, val_overridden).c_str(), cpugpuinfo_time); //get the frequency for cpugpuinfo thread
    LOG_I(TAG, "cpugpuinfo_time: %d", cpugpuinfo_time);
    int gpu_poll_time;
    string_to_integer(cfg_prsr.getConfig("healthstats", "gpu_poll_secs", GPU_POLL_DEFAULT_STRING, true, val_overridden).c_str(), gpu_poll_time); //get the frequency for gpu polling
    LOG_I(TAG, "gpu_poll_time: %d", gpu_poll_time);
    int processinfo_time;
    string_to_integer(cfg_prsr.getConfig("healthstats", "process_info_secs", "5", true, val_overridden).c_str(), processinfo_time); //get the frequency for processinfo thread
    LOG_I(TAG, "processinfo_time: %d", processinfo_time);
    if(enable_all_time_sdcard_error_state_log == "true"){
        std::stringstream val_stream;
        val_stream << "SELECT * FROM COMPONENTSTATES WHERE COMPONENT_NAME == \"SDCARD\"";
        int rc ;
        rc = ComponentBase::exec_cmd_db(ComponentBase::db_handle, val_stream.str(), callback, NULL);
        if (rc == false){
            LOG_E(TAG, "Failed to execute %s", val_stream.str().c_str());
            return false;
        }
            
    }
    sleep(1);
    int maxWaitTime_for_udid_update = 30 , waitTime = 0;
    while(file_is_present(time_sync_token_file) == false) {
        sleep(1);
        waitTime++;
        if(waitTime > maxWaitTime_for_udid_update) {
            break;
        }
        
    }
    if(waitTime >= maxWaitTime_for_udid_update) {
        LOG_E(TAG, "udid is not updated till %d seconds of wait. check time_sync service", waitTime);
    } 
    else {
        LOG_I(TAG, "udid updated by time_sync service after waiting here for %d sec.", waitTime);
    }
    sleep(1);
    prop_data_t entry;
    string udid_string = "";
    if( get_property_DB("udid", &entry) ) {
        udid_string = entry.value;
        string_to_int64(udid_string , ComponentBase::udid);
        LOG_I(TAG, "udid: %lld", ComponentBase::udid );
    }



    // create a sdcard object
    SdCard sdcard("SDCARD", sdcard_diag_interval_time, sdcard_diag_start_time);
    if(!sdcard.start_thread()) {
        LOG_E(TAG, "Failed to start sdcard thread!!! Check the logs for more details");
    }
    sdcard_obj_ptr = &sdcard;

       pthread_t get_health_metrics_th;
        Timer AppTimer;
        AppTimer.register_for_timer(get_health_metrics, 60);
        AppTimer.startTimer();


    CpuGpuInfo cpugpuinfo("CPUGPUINFO", cpugpuinfo_time,0, gpu_poll_time); //Thread creation for CpuGpuinfo
    if(!cpugpuinfo.start_thread()) {
        LOG_E(TAG, "Failed to start cpugpuinfo thread!!! Check the logs for more details");
    }
    ProcessInfo processinfo("PROCESSINFO", processinfo_time,0);//Thread creation for Processinfo
    if(!processinfo.start_thread()) {
        LOG_E(TAG, "Failed to start processinfo thread!!! Check the logs for more details");
    }
    int waf_diag_interval_time;
    string_to_integer(cfg_prsr.getConfig("diagnostic", "waf_diag_interval_time", to_string(WAF_MONITORING_INTERVAL_TIME) , true, val_overridden).c_str(), waf_diag_interval_time);
    LOG_I(TAG, "waf_diag_interval_time: %d", waf_diag_interval_time);

    //Below is added as a part of DT-2544 i.e Mounting /tmp on tmpfs for D4X0 Platforms.
    if (nd_device_obj->is_external_emmc_supported()){                      //Ensures we will be running this only for the D4X0 Platforms...

	const std::string mnt_path = "/lib/systemd/system/tmp.mount";      // mount service path to be installed...
	std::ifstream tmp_mount_file(mnt_path);
	bool tmp_mount_exists = false;                                     // checks if tmp.mount file exists or not
	if (tmp_mount_file.good()) {
		tmp_mount_exists = true;                                       // Implies tmp.mount file exists..
	}

	bool mounts_enabled = false;                                            // Flag to enable/disable tmp.mount from the override or bagheeraconfig

	//Default will be false unless it's enabled from the config to support tmpfs for /tmp
	std::string tmp_mount_active_str = cfg_prsr.getConfig("diagnostic", "log_overlay", "false", true, val_overridden);
	if (tmp_mount_active_str == "true") {
		mounts_enabled = true;
	}
	LOG_I(TAG, "tmp.mount updation from the config : %d", mounts_enabled);

	const string tmpfs_service= "tmp.mount";
	bool tmpfs_service_enable_status = false;                          // Status Flag to capture the tmp.mount is enabled or not..

	// Implies after the service enable/disable , start/stop of the services is required or not according to func implementation...
	// This is important , as mounting the /tmp in the middle could break the existing resources and can make a possible seg 11.
	// Therefore Pushing the start to the next boot cycle..and same goes for the stop too.
	bool is_tmpfs_start_required = false;
	if (tmp_mount_exists){
		if (mounts_enabled){                                           // File exists and we can decide based on config , whether to enable/disable the service.
			tmpfs_service_enable_status = enable_service(tmpfs_service,is_tmpfs_start_required);  //not enabling by default as start status is not being returned with current implementation....
			LOG_I(TAG,"tmp.mount - mount enable status :%d",tmpfs_service_enable_status);
		}
		else{
			LOG_I(TAG,"tmp.mount is set to disabled from the config");
			tmpfs_service_enable_status = disable_service(tmpfs_service,is_tmpfs_start_required);  //true corresponds to stopping the service and then disabling it.
			if(tmpfs_service_enable_status){
				LOG_I(TAG,"tmp.mount is disabled from the config...");
			}
			else{
				LOG_C(TAG,"Failed to disable tmp.mount from the config...");
			}
		}
	}
	else{
		LOG_C(TAG,"tmp.mount is not found");
	}

	// Create periodic overlay management thread that runs every 5 minutes
	std::thread overlay_mgmt_thread(periodic_overlay_management_thread, mounts_enabled);
	overlay_mgmt_thread.detach(); // Detach so it runs independently
	LOG_I(TAG, "Created periodic overlay management thread (5-minute intervals)");

	// Run initial overlay management at boot
	run_overlay_management_wrapper(mounts_enabled);
	LOG_I(TAG, "Initial overlay management at boot completed");

    }



    eMMCWafMonitor waf_obj("WAF",waf_diag_interval_time, 0);

    // msg loop //
    diagnostic_msg_loop();

    cpugpuinfo.notify();
    processinfo.notify();

    try{
        if (waf_obj.is_thread_joinable()) {
            waf_obj.join_thread();  // Or directly join: waf_obj.c_thread.join();
        }
    } catch (const std::exception& e) {
        LOG_E(TAG, "Caught exception in WAF Thread Joining : %s ", e.what());
    }

    sdcard.join_child_thread();
    cpugpuinfo.join_child_thread();
    processinfo.join_child_thread();
    nd_service_obj->release_service_obj(); 
	return 0;
}

string get_msgq_name() {
	return diagnostic_q_name;
}
