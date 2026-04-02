#include <log.h>
#include <unistd.h>
#include <string.h>
#include <waf_utils.h>
#include "nd_msg_types.h"
#include <sstream>
#include <fstream>
#include <nd_file_utils.h>
#include <nd_msg_utils.h>
#include <iterator>
#include <jansson.h>
#include <nlohmann/json.hpp>
#include <system_utils.h>
#include <sys/statvfs.h>
#include "diag_resource_info_helper.h"
#include <thread>

#define TAG_WAF "WAF_U"
using json = nlohmann::json;
static const string ND_Dev_folder_path = "/home/ubuntu/.nddevice/";
extern ND_DeviceFactory *nd_device_obj;
extern NDService *nd_service_obj;
#define time_in_seconds 60

bool eMMCWafMonitor::is_thread_joinable() const {
    return c_thread.joinable();
}

void eMMCWafMonitor::join_thread() {
    if (c_thread.joinable()) {
        c_thread.join();
    }
}


void eMMCWafMonitor::send_waf_info_to_healthstats(const StorageHealthMonitor& stats , bool isInternal)
{
    try {
        string header = isInternal ? "health_info:Int_eMMC_health_info" : "health_info:Ext_eMMC_health_info";
        auto timestamp = std::chrono::system_clock::now();
        uint64_t timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(timestamp.time_since_epoch()).count();

        json_t* data_object = json_object();
		if (data_object) {
			if (stats.host_write_size != uint64_t(-1))
				json_object_set_new(data_object, string_to_const_char("host_write_byte"), json_integer(stats.host_write_size));
			if (stats.erase_count != uint32_t(-1))
				json_object_set_new(data_object, string_to_const_char("erase_count"), json_integer(stats.erase_count));
			if (stats.erase_count_slc_pool1 != uint32_t(-1) && stats.erase_count_slc_pool1 != -1 && stats.erase_count_slc_pool1 != 0)
				json_object_set_new(data_object, string_to_const_char("erase_count_slc_pool1"), json_integer(stats.erase_count_slc_pool1));
			if (stats.erase_count_slc_pool2 != uint32_t(-1) && stats.erase_count_slc_pool2 != -1 && stats.erase_count_slc_pool2 != 0)
				json_object_set_new(data_object, string_to_const_char("erase_count_slc_pool2"), json_integer(stats.erase_count_slc_pool2));
			if (stats.erase_count_slc_pool4 != uint32_t(-1) && stats.erase_count_slc_pool4 != -1 && stats.erase_count_slc_pool4 != 0)
				json_object_set_new(data_object, string_to_const_char("erase_count_slc_pool4"), json_integer(stats.erase_count_slc_pool4));
			if (stats.device_age_tlc != uint32_t(-1) && stats.device_age_tlc != -1 && stats.device_age_tlc != 0)
				json_object_set_new(data_object, string_to_const_char("device_age_tlc"), json_integer(stats.device_age_tlc));
			if (stats.device_age_slc1 != uint32_t(-1) && stats.device_age_slc1 != -1 && stats.device_age_slc1 != 0)
				json_object_set_new(data_object, string_to_const_char("device_age_slc1"), json_integer(stats.device_age_slc1));
			if (stats.device_age_slc2 != uint32_t(-1) && stats.device_age_slc2 != -1 && stats.device_age_slc2 != 0)
				json_object_set_new(data_object, string_to_const_char("device_age_slc2"), json_integer(stats.device_age_slc2));
			if (stats.device_age_slc4 != uint32_t(-1) && stats.device_age_slc4 != -1 && stats.device_age_slc4 != 0)
				json_object_set_new(data_object, string_to_const_char("device_age_slc4"), json_integer(stats.device_age_slc4));
			if (stats.waf != -1.0)
				json_object_set_new(data_object, string_to_const_char("waf"), json_real(stats.waf));
			if (stats.bad_block_manufactured != uint32_t(-1) && stats.bad_block_manufactured != -1)
				json_object_set_new(data_object, string_to_const_char("bad_blocks_manufactured"), json_integer(stats.bad_block_manufactured));
			if (stats.bad_block_slc != uint32_t(-1) && stats.bad_block_slc != -1 && stats.bad_block_slc != 0)
				json_object_set_new(data_object, string_to_const_char("bad_block_SLC"), json_integer(stats.bad_block_slc));
			if (stats.bad_block_tlc != uint32_t(-1) && stats.bad_block_tlc != -1 && stats.bad_block_tlc != 0)
				json_object_set_new(data_object, string_to_const_char("bad_block_TLC"), json_integer(stats.bad_block_tlc));
			if (stats.bad_block_overall != uint32_t(-1) && stats.bad_block_overall != -1)
				json_object_set_new(data_object, string_to_const_char("bad_block_overall"), json_integer(stats.bad_block_overall));
			if (stats.current_temperature != uint32_t(-1) && stats.current_temperature != 0)
				json_object_set_new(data_object, string_to_const_char("temperature"), json_integer(stats.current_temperature));
			if (stats.temperature_threshold_cross_counter != uint32_t(-1) && stats.temperature_threshold_cross_counter != 0)
				json_object_set_new(data_object, string_to_const_char("warning_temp_cross_counter"), json_integer(stats.temperature_threshold_cross_counter));
			if (stats.num_of_write_abort_failures != uint32_t(-1) && stats.num_of_write_abort_failures != 0)
				json_object_set_new(data_object, string_to_const_char("num_of_write_abort_failures"), json_integer(stats.num_of_write_abort_failures));
			if (stats.power_up_count_device_lifetime != uint32_t(-1) && stats.power_up_count_device_lifetime != 0)
				json_object_set_new(data_object, string_to_const_char("power_up_counter"), json_integer(stats.power_up_count_device_lifetime));
			if (stats.total_voltage_drops != uint32_t(-1) && stats.total_voltage_drops != 0)
				json_object_set_new(data_object, string_to_const_char("total_voltage_drops"), json_integer(stats.total_voltage_drops));
			if (stats.all_power_droops != uint32_t(-1) )
				json_object_set_new(data_object, string_to_const_char("power_drops"), json_integer(stats.all_power_droops));
			if (stats.emmc_size != uint32_t(-1))
				json_object_set_new(data_object, string_to_const_char("emmc_size"), json_integer(stats.emmc_size));
			if (!stats.vendor.empty())
				json_object_set_new(data_object, string_to_const_char("vendor"), json_string(stats.vendor.c_str()));
			if (!stats.firmware_version.empty())
				json_object_set_new(data_object, string_to_const_char("firmware_version"), json_string(stats.firmware_version.c_str()));
			if (stats.uecc_count != uint32_t(-1))
				json_object_set_new(data_object, string_to_const_char("uecc_count"), json_integer(stats.uecc_count));
			json_object_set_new(data_object, string_to_const_char("timestamp"), json_integer(timestamp_ms));
		}

        json_t* root = json_object();
        if (root && data_object) {
            json_object_set_new(root, string_to_const_char(header), data_object);
            json_object_set_new(root, string_to_const_char("isArray"), json_string(string_to_const_char("true")));
            char* jsonString = json_dumps(root, JSON_REAL_PRECISION(13));
            if (jsonString) {
                nd_service_obj->send_msg_healthstats(jsonString, strlen(jsonString));
                free(jsonString);
            }
            json_decref(root);
        }
		else if (root){
			json_decref(root);
			LOG_E(TAG_WAF, "Failed to create JSON object for eMMC health info");
		}
    }
    catch (const std::exception& e) {
        LOG_I(TAG_WAF, "Exception caught sending eMMC health info to healthstats");
    }
}

void eMMCWafMonitor::waf_monitor_thread() {
	int waf_enabled = 1;  //enabling WAF Support By Default
	bool val_overridden = false;
	const string BAGHEERACONFIG_INI = nd_device_obj->get_bagheera_config_path();
	string BAGHEERA_OVERRIDE_INI = nd_device_obj->get_bagheera_override_path();
	Config_parser cfg_prsr(BAGHEERACONFIG_INI);
	Config_parser over_prsr(BAGHEERA_OVERRIDE_INI);
    bool  config_parser_status = string_to_integer(cfg_prsr.getConfig("diagnostic", "waf_enabled", to_string(waf_enabled), true, val_overridden).c_str(), waf_enabled);
	bool override_status = string_to_integer(over_prsr.getConfig("diagnostic", "waf_enabled", to_string(waf_enabled), true, val_overridden).c_str(), waf_enabled);

    if ( !override_status && !config_parser_status ){
        waf_enabled = 1 ; // Assuming default case if not defined in both config and override
    }

	if ( ! waf_enabled ){
		LOG_I ( TAG_WAF , "WAF Support is disabled From the Config...Exiting");
		return ;
	}
	std::stringstream ss;
	int manfid_op;
	int reboot_cycle_monitor = 0;
	const string int_file_path = ND_Dev_folder_path + "Int_EMMC_WAF_Dict.json";
	const string ext_file_path = ND_Dev_folder_path + "Ext_EMMC_WAF_Dict.json";
	bool external_emmc_supported =  nd_device_obj->is_external_emmc_supported();
	StorageHealthMonitor internal_emmc_waf_info;
	StorageHealthMonitor external_emmc_waf_info ;
	bool isInternal = true;
	//Above part is to send critical info once per boot cycle of diagnostic ..

	map<string, void*> params;
	double waf_correction_factor = -1.0;  // -1 -> Partial Data of the initial 2 erase counts.
	int critical_cycle_check_duration = 2; //Represents twice the duration we ned to wait before sending critical info
	bool send_this_cycle = true;
	bool send_this_cycle_ext = true;
	while(true){
		//Checking the below condition to avoid sending critical info in every cycle
		internal_emmc_waf_info = nd_device_obj->get_waf_info(true);
		isInternal = true;
		params["is_internal"] = static_cast<void*>(&isInternal);
		bool is_internal_waf_data_valid = nd_device_obj->get_waf_utility((WafFunctionType)IS_WAF_DATA_VALID,params);
		if( !is_internal_waf_data_valid ){
			internal_emmc_waf_info.waf = waf_correction_factor;
			LOG_E(TAG_WAF, "WAF Data is not valid...Applying Correction Factor");
		}
		send_waf_info_to_healthstats(internal_emmc_waf_info, true);
		if ( is_internal_waf_data_valid && ( internal_emmc_waf_info.waf_changed || !reboot_cycle_monitor ) ) {    //specifies waf is changed or 1st time since reboot
			string int_emmc_critical_info = "";
			int_emmc_critical_info = "Int eMMC - " + internal_emmc_waf_info.vendor +" : Host Write Byte: " + to_string(internal_emmc_waf_info.host_write_size/1024) + " GB" +
                                    " , Erase Count: " + to_string(internal_emmc_waf_info.erase_count) +
				    " , WAF : " + to_string(internal_emmc_waf_info.waf);
			
			ss << std::hex <<  internal_emmc_waf_info.manfid;
			ss >> manfid_op;
			manfid_op += eMMC_vendor_code_aux_mapping::internal_emmc_mapping;
			nd_service_obj->send_err_msg(SM_E_DIAG_EMMC_RUN_PERCENTAGE, manfid_op , int_emmc_critical_info);
			ss.clear();
			ss.str("");
		}
		if (send_this_cycle) {
			nd_device_obj->send_emmc_critical_events(internal_emmc_waf_info, true);
			send_this_cycle = false;
		}
		if ( external_emmc_supported ){
			isInternal = false;
			params["is_internal"] = static_cast<void*>(&isInternal);
			bool is_external_waf_data_valid = nd_device_obj->get_waf_utility((WafFunctionType)IS_WAF_DATA_VALID,params);
			external_emmc_waf_info = nd_device_obj->get_waf_info(false);
			if( !is_external_waf_data_valid ){
				external_emmc_waf_info.waf = waf_correction_factor;
				LOG_E(TAG_WAF, "External eMMC WAF Data is not valid...Applying Correction Factor");
			}
			send_waf_info_to_healthstats(external_emmc_waf_info, false);
			if ( is_external_waf_data_valid && ( external_emmc_waf_info.waf_changed || !reboot_cycle_monitor ) ) {    //specifies waf is changed or 1st time since reboot
				string ext_emmc_critical_info = "Ext eMMC - " + external_emmc_waf_info.vendor +" : Host Write Byte: " + to_string((external_emmc_waf_info.host_write_size)/1024) + " GB" +
					    " , Erase Count: " + to_string((external_emmc_waf_info.erase_count)) +
					    " , WAF : " + to_string((external_emmc_waf_info.waf));
				ss << std::hex <<  external_emmc_waf_info.manfid;
				ss >> manfid_op;
				manfid_op += eMMC_vendor_code_aux_mapping::external_emmc_mapping;
				nd_service_obj->send_err_msg(SM_E_DIAG_EMMC_RUN_PERCENTAGE, manfid_op , ext_emmc_critical_info);
				ss.clear();
				ss.str("");
			}
			if (send_this_cycle_ext) {
				nd_device_obj->send_emmc_critical_events(external_emmc_waf_info, false);
				send_this_cycle_ext = false;
			}
		}
		reboot_cycle_monitor = 1;
		sleep(waf_interval * time_in_seconds);
	}
	LOG_I(TAG_WAF, "Got out of waf monitoring thread");
}

