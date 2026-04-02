/* Copyright (C) 2019 - 2020 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Devendra Yadav <devendra.yadav@netradyne.com>
 * Written by Hari Seenivasan <hari.seenivasan@netradyne.com>
 */

#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <config_parser.h>
#include <thread>
#include "service_utils.h"
#include "system_utils.h"
#include "nd_file_utils.h"
#include "apm.h"
#include "apm_imu.h"
#include "apm_gps.h"
#include "apm_ignition.h"
#include "apm_supercap.h"
#include "apm_can.h"
#include "apm_power_volt.h"
#include "ndmb/nd_msg_interface.h"
#include "ndmb/nd_mbclient.h"
#include "ndmb/nd_mbserver.h"
#include <svc.h>
#include "wake_up_reason.h"
#include <algorithm>
#include <cctype>
#include "apm_engine_status.h"

#define TAG "APM"
#define ROUTE_LOGS

NDService *nd_service_obj;
ND_DeviceFactory *nd_device_obj = NULL;  // nd device object based on deviceType

#ifdef IGNITION_BROADCAST
    NDMBServer server(SERVICE_APM);
#endif

bool apm_motion_detection_enable = false;
bool apm_wom_enable = false;
bool enable_ignition_based_wakeup = false;
bool apm_ign_wake_enable = false;
bool glitch_suppression_support = true;
bool engine_status_detection_enable = true;

// bool engine_state_privacy = false;

bool apm_imu_enable = true;
bool apm_gps_enable = true;
bool apm_ign_enable = true;
bool apm_can_enable = false;
bool apm_supercap_enable = true;
bool apm_power_volt_enable = true;
// bool apm_ign_volt_enable = false;

string veh_class("");
int wom_x_thr,wom_y_thr,wom_z_thr;
float imu_threshold;
float imu_engine_off_threshold;
float imu_speed_threshold;
float imu_speed_engine_off_threshold;
float imu_dist_threshold;
float imu_dist_engine_off_threshold;
float gps_accuracy;
float gps_speed_threshold;
float gps_speed_engine_off_threshold;
double gps_latitude_threshold;
double gps_longitude_threshold;
int vehicle_idle_time;
int all_thread_enable_status = 0;
int pow_on_off_reason = 0;
int lanai_reset_reason = 0;
int obd_data_retry_count;
int obd_data_retry_time;
int obd_engine_off_counter;
int obd_rpm_threshold = DEF_OBD_RPM_THRESHOLD;
extern int all_thread_keepalive_status;
extern std::mutex keepalive_status_mutex;
float engine_imu_threshold = DEF_IMU_ACCEL_RMS_ON_THRESHOLD, engine_imu_threshold_off = DEF_IMU_ACCEL_RMS_OFF_THRESHOLD;
bool engine_imu_threshold_overridden = false;

static bool apm_imu_use_legacy = false;
static bool apm_gps_use_legacy = false;
static bool apm_can_use_legacy = false;
static bool apm_pwr_use_legacy = false;
static bool apm_ign_use_legacy = false;
static bool apm_pfi_use_legacy = false;

// To enable or disable sensor for decision making
static bool apm_imu_disable_sensor = false;
static bool apm_gps_disable_sensor = false;
static bool apm_can_disable_sensor = true;
static bool apm_pwr_disable_sensor = true;
static bool apm_ign_disable_sensor = false;
static bool apm_pfi_disable_sensor = false;
static bool apm_eng_stat_disable_sensor = false;

static int easy_install = EASY_INSTALL_DISABLE;

static bool can_fusion_active = false;
static bool gps_fusion_active = false;
static bool imu_fusion_active = false; // for engine status detection using IMU

sensor_config_t sensor_cfg = {

	// 12/24V thresholds
	.eng_off_crank_v_th = ENGINE_OFF_CRANK_VOLTAGE_THRESHOLD,
	.eng_on_crank_v_th = ENGINE_ON_CRANK_VOLTAGE_THRESHOLD,
	.idle_crank_v_th = ENGINE_IDLING_CRANK_VOLTAGE_THRESHOLD,
	.eng_off_crank_v_th_24v = ENGINE_OFF_CRANK_VOLTAGE_THRESHOLD_24V,
	.eng_on_crank_v_th_24v = ENGINE_ON_CRANK_VOLTAGE_THRESHOLD_24V,
	.idle_crank_v_th_24v = ENGINE_IDLING_CRANK_VOLTAGE_THRESHOLD_24V,

	// min voltage limits
	.min_v_lim_12v = DEF_MIN_POWER_VOLTAGE,
	.min_v_lim_24v = DEF_MIN_POWER_VOLTAGE_24V,

	// sc thresholds
	.pfi_off_crank_v_th = DEF_SUPERCAP_UPPER_THRESHOLD,
	.pfi_on_crank_v_th = DEF_SUPERCAP_LOWER_THRESHOLD,
	.pfi_off_crank_v_th_24v = DEF_SUPERCAP_UPPER_THRESHOLD_24V,
	.pfi_on_crank_v_th_24v = DEF_SUPERCAP_LOWER_THRESHOLD_24V,

	// fusion thresholds for 12/24V
	.fusion_eng_off_crank_v_th = DEF_ENGINE_OFF_CRANK_VOLTAGE_THRESHOLD_FUSION,
	.fusion_eng_on_crank_v_th = DEF_ENGINE_ON_CRANK_VOLTAGE_THRESHOLD_FUSION,
	.fusion_idle_crank_v_th = DEF_ENGINE_IDLING_CRANK_VOLTAGE_THRESHOLD_FUSION,
	.fusion_eng_off_crank_v_th_24v = DEF_ENGINE_OFF_CRANK_VOLTAGE_THRESHOLD_24V_FUSION,
	.fusion_eng_on_crank_v_th_24v = DEF_ENGINE_ON_CRANK_VOLTAGE_THRESHOLD_24V_FUSION,
	.fusion_idle_crank_v_th_24v = DEF_ENGINE_IDLING_CRANK_VOLTAGE_THRESHOLD_24V_FUSION,

	.pwr_volt_fusion_enable = true,

	// CAN Thresholds
	.can_rpm_threshold = DEF_CAN_RPM_THRESHOLD,
	.can_rpm_engine_off_threshold = DEF_CAN_RPM_THRESHOLD,
	.can_speed_threshold = DEF_CAN_SPEED_THRESHOLD
};

static float IMU_AXIS_THRESHOLDS[eIMU_AxisMax][eAPM_Decision_Max];
static string IMU_AXIS_CONFIG_THRESHOLDS[eIMU_AxisMax] = {"imu_ax_th", "imu_ay_th", "imu_az_th", "imu_gx_th", "imu_gy_th", "imu_gz_th"};
static float GPS_THRESHOLDS[eGPS_ParamMax][eAPM_Decision_Max];

int adaptive_wom_trigger_count = 5;

float ignition_threshold = -1.0;

float power_volt_threshold = -1.0;
float power_volt_threshold_24V = -1.0;

float supercap_threshold = -1.0;
float supercap_threshold_24V = -1.0;

float can_rpm_threshold = -1.0;

// From overrides, if any sensor threshold is set to this value, disable that sensor's decision making
// imu_threshold, gps_speed_threshold, can_rpm_threshold, power_volt_threshold, supercap_threshold
// State for that sensor always remains to STATIONARY
constexpr float SENSOR_DECISION_DISABLE_THRESHOLD = 0.0;

constexpr int DECIMAL_PLACES_FLOAT_THRESHOLD_IMU_AND_PWR = 4;
constexpr int DECIMAL_PLACES_FLOAT_THRESHOLD_OTHERS = 1;

std::thread init_ign_status_th;
std::atomic<bool> constructor_called(false);

IGNS_worker *igns_worker = NULL;
IMU_worker *imu_worker = NULL;
SC_worker *sc_worker = NULL;
GPS_worker *gps_worker = NULL;
CAN_worker *can_worker = NULL;
POWER_VOLT_worker *power_volt_worker = NULL;
ENG_STAT_worker *eng_stat_worker = nullptr;

int imu_dec_window[eDecisionWindow_Max] = {IMU_SHORT_WINDOW, IMU_FULL_WINDOW, IMU_DATA_OUTAGE_THRESHOLD, IMU_SHORT_WINDOW, IMU_READ_INTERVAL_SEC, IMU_MEDIAN_DEC_WINDOW, IMU_CONTINUOUS_DEC_WINDOW, IMU_CACHE_WINDOW};
bool imu_dec_window_overridden = false;
int power_volt_dec_window[eDecisionWindow_Max] = {PWR_SHORT_WINDOW, PWR_FULL_WINDOW, PWR_DATA_OUTAGE_THRESHOLD, PWR_SHORT_WINDOW, POWER_VOLT_READ_INTERVAL_SEC, PWR_MEDIAN_DEC_WINDOW, PWR_CONTINUOUS_DEC_WINDOW, PWR_CACHE_WINDOW};
bool power_volt_dec_window_overridden = false;
int gps_dec_window[eDecisionWindow_Max] = {GPS_SHORT_WINDOW, GPS_FULL_WINDOW, GPS_DATA_OUTAGE_THRESHOLD, GPS_SHORT_WINDOW, GPS_READ_INTERVAL_SEC, GPS_MEDIAN_DEC_WINDOW, GPS_CONTINUOUS_DEC_WINDOW, GPS_CACHE_WINDOW};
bool gps_dec_window_overridden = false;
int can_dec_window[eDecisionWindow_Max] = {CAN_SHORT_WINDOW, CAN_FULL_WINDOW, CAN_DATA_OUTAGE_THRESHOLD, CAN_SHORT_WINDOW, CAN_DATA_READ_INTERVAL_SEC, CAN_MEDIAN_DEC_WINDOW, CAN_CONTINUOUS_DEC_WINDOW, CAN_CACHE_WINDOW};
bool can_dec_window_overridden = false;
int eng_stat_dec_window[eDecisionWindow_Max] = {ENG_STAT_SHORT_WINDOW, ENG_STAT_FULL_WINDOW, ENG_STAT_DATA_OUTAGE_THRESHOLD, ENG_STAT_SHORT_WINDOW, ENG_STAT_READ_INTERVAL_SEC, ENG_STAT_MEDIAN_DEC_WINDOW, ENG_STAT_CONTINUOUS_DEC_WINDOW, ENG_STAT_FULL_WINDOW};
bool eng_stat_dec_window_overridden = false;
eng_stat_thresholds eng_stat_thr;
/*
 * Func name: apm_wom_thr_setting()
 * function is to set thresholds for WOM(Wake On Montion)
 * returns true on success and false on failure
 */
bool apm_wom_thr_setting() {
	unsigned int wom_x_thres, wom_y_thres, wom_z_thres;
	APM::Instance()->get_wom_thresholds(wom_x_thres, wom_y_thres, wom_z_thres);
    string err_msg_wom = "Class:" + veh_class + ", WOM Thresholds X:" + std::to_string(wom_x_thres) + ",Y:" + std::to_string(wom_y_thres) + ",Z:" + std::to_string(wom_z_thres);
	int aux_code = wom_x_thres + (wom_y_thres << 10) + (wom_z_thres << 20);
    nd_service_obj->send_err_msg(SM_E_APM_AON_VERSION_INFO, aux_code, err_msg_wom);
	return nd_device_obj->configure_wom_thresholds(wom_x_thres, wom_y_thres, wom_z_thres);
}

void device_info () {

    std::ifstream os_ver_stream;
    os_ver_stream.open(nd_device_obj->get_os_version_file().c_str());
    if (!os_ver_stream) {
        LOG_E(TAG, "Unable to open file : %s",nd_device_obj->get_os_version_file().c_str() );
        os_ver_stream.close();
    }
    else {
        std::string nd_os_version( (std::istreambuf_iterator<char>(os_ver_stream) ),
                           (std::istreambuf_iterator<char>()    ) );
        os_ver_stream.close();
        LOG_I( TAG,"Current Netradyne OS Version : %s",nd_os_version.c_str() );
    }

    nd_device_obj->dump_aon_device_info();
}

/*
 * Func name: device_on_off_reason()
 * function provides the device power on/off reason
 * returns true on success and false on failure
 */
bool device_on_off_reason() {
    string reason("");
    bool status = nd_device_obj->get_reset_wake_reason(pow_on_off_reason, reason, true);
    LOG_I(TAG, "AON: LANAI Power On/Off reason : 0x%02x, %s",pow_on_off_reason, reason.c_str());
    nd_service_obj->send_err_msg(SM_E_PM_DB_PREV_SHUTDOWN, pow_on_off_reason, reason);

    reason.clear();
    status &= nd_device_obj->get_lanai_pmic_reset_reason(lanai_reset_reason, reason);
    LOG_I(TAG, "PMIC:LANAI Power Off reason : 0x%02x",lanai_reset_reason, reason.c_str());

    return status;
}

static VehicleClass set_vehicle_class() {

	std::unique_ptr<Config_parser> c1(new Config_parser(DEVICE_CONFIG_INI));
    if (c1 == nullptr) {
        LOG_E(TAG, "Creating object of Config_parser for %s failed", DEVICE_CONFIG_INI.c_str());
        return eVehicleUnknown;
    }

	if (c1->getParseStatus() == false) {
        LOG_E(TAG, "Parsing %s failed", DEVICE_CONFIG_INI.c_str());
        return eVehicleUnknown;
    }

	bool get_override_val = true, val_overridden = false;

	if (c1->isPresent("vehicle", "vehclass")) {
		veh_class = c1->getConfig("vehicle", "vehclass","Unknown",get_override_val, val_overridden);

		LOG_I(TAG, "vehclass = %s", veh_class.c_str());

		std::transform(veh_class.begin(), veh_class.end(), veh_class.begin(),
        [](unsigned char c) {
			return std::toupper(c);
		});

		if (veh_class == "CLASS1") {
			return eVehicleClass1;
		}
		else if (veh_class == "CLASS2") {
			return eVehicleClass2;
		}
		else if (veh_class == "CLASS3") {
			return eVehicleClass3;
		}
		else if (veh_class == "CLASS4") {
			return eVehicleClass4;
		}
		else if (veh_class == "CLASS5") {
			return eVehicleClass5;
		}
		else if (veh_class == "CLASS6") {
			return eVehicleClass6;
		}
		else if (veh_class == "CLASS7") {
			return eVehicleClass7;
		}
		else if (veh_class == "CLASS8") {
			return eVehicleClass8;
		}
		else {
			LOG_E(TAG, "vehclass (%s) not CLASS1 to CLASS8", veh_class.c_str());
		}
	}

	return eVehicleUnknown;
}

void set_defaults(){
	NDDeviceTypeT device_type = ND_DeviceFactory::getBuildDeviceType();
	if(NDDeviceTypeT::bagheera2 == device_type){
		DEF_APM_GPS = "false";
		DEF_APM_IMU = "false";
		DEF_APM_WOM = "false";
		DEF_APM_SUPERCAP = "false";
		DEF_APM_MOTION_DET = "false";
		DEF_APM_CAN = "false";
	}
	return ;
}

static float parse_thresholds(const std::string &input, float def, int pos, float min, float max) {
    std::stringstream ss(input);
    std::string item;
    int index = 0;

    while (std::getline(ss, item, ',')) {

        if (index == pos) {
			float val = def;
			if (string_to_float(item, val)) {
				if (val >= min && val <= max) {
					return val;
				} else {
					LOG_E(TAG, "Value %.2f out of range [%.2f, %.2f], using default %.2f", val, min, max, def);
					return def;
				}
			} else {
				LOG_E(TAG, "Failed to convert '%s' to float, using default %.2f", item.c_str(), def);
				return def;
			}
        }
        index++;
    }

    return def;
}

static int parse_thresholds(const std::string &input, int def, int pos, int min, int max) {
    std::stringstream ss(input);
    std::string item;
    int index = 0;

    while (std::getline(ss, item, ',')) {

        if (index == pos) {
			int val = def;
			if (string_to_integer(item, val)) {
				if (val >= min && val <= max) {
					return val;
				} else {
					LOG_E(TAG, "Value %d out of range [%d, %d], using default %d", val, min, max, def);
					return def;
				}
			} else {
				LOG_E(TAG, "Failed to convert '%s' to integer, using default %d", item.c_str(), def);
				return def;
			}
        }
        index++;
    }

    return def;
}

#define READ_CONFIG_VALUE(type, var, section, key, default_value, get_override_value, val_overridden) \
   { \
       std::string value_str = c.getConfig(section, key, std::to_string(default_value), get_override_value, val_overridden); \
       if (false == string_to_##type(value_str, var)) { \
           var = default_value; \
           LOG_E(TAG, "Failed to convert config value to " #type " for " key ", setting default value " #default_value); \
       } \
   }

uint count_values(const string &s) {

	if(s.empty()) {
		return 0;
	}

	uint count = 1;

    for (char c : s) {
        if (c == ',')
            count++;
    }
    return count;
}

static void configure_crank_voltage_thresholds(sensor_config_t &sensor_cfg, float pwr_volt_th, const std::string &pwr_volt_th_str, bool is_24v) {

	constexpr uint THRESHOLD_COUNT = 1;
    const float min_v = nd_factory_utils::get_min_valid_voltage();
    const float max_v = nd_factory_utils::get_max_valid_voltage();

	const float ENG_OFF_DEF = (is_24v ? ENGINE_OFF_CRANK_VOLTAGE_THRESHOLD_24V : ENGINE_OFF_CRANK_VOLTAGE_THRESHOLD);
	const float ENG_ON_DEF = (is_24v ? ENGINE_ON_CRANK_VOLTAGE_THRESHOLD_24V : ENGINE_ON_CRANK_VOLTAGE_THRESHOLD);
	const float IDLE_DEF = (is_24v ? ENGINE_IDLING_CRANK_VOLTAGE_THRESHOLD_24V : ENGINE_IDLING_CRANK_VOLTAGE_THRESHOLD);

	const float FUSION_OFF_DEF = (is_24v ? DEF_ENGINE_OFF_CRANK_VOLTAGE_THRESHOLD_24V_FUSION : DEF_ENGINE_OFF_CRANK_VOLTAGE_THRESHOLD_FUSION);
	const float FUSION_ON_DEF = (is_24v ? DEF_ENGINE_ON_CRANK_VOLTAGE_THRESHOLD_24V_FUSION : DEF_ENGINE_ON_CRANK_VOLTAGE_THRESHOLD_FUSION);
	const float FUSION_IDLE_DEF = (is_24v ? DEF_ENGINE_IDLING_CRANK_VOLTAGE_THRESHOLD_24V_FUSION : DEF_ENGINE_IDLING_CRANK_VOLTAGE_THRESHOLD_FUSION);

	float eng_off_th = ENG_OFF_DEF;
	float eng_on_th = ENG_ON_DEF;
	float idle_th = IDLE_DEF;

	float fusion_eng_off_th = FUSION_OFF_DEF;
	float fusion_eng_on_th = FUSION_ON_DEF;
	float fusion_idle_th = FUSION_IDLE_DEF;

	uint count = count_values(pwr_volt_th_str);

    if ( (THRESHOLD_COUNT == count) && (pwr_volt_th > SENSOR_DECISION_DISABLE_THRESHOLD)) {

		const float eng_off_offset = ENG_ON_DEF - ENG_OFF_DEF;
		const float idle_offset = IDLE_DEF - ENG_ON_DEF;

		const float fusion_eng_off_offset = FUSION_ON_DEF - FUSION_OFF_DEF;
		const float fusion_idle_offset = FUSION_IDLE_DEF - FUSION_ON_DEF;

		eng_on_th = pwr_volt_th;
		eng_off_th = eng_on_th - eng_off_offset;
		idle_th = IDLE_DEF; // Setting idle_th to default which only used for wakeup crank volt information.

		fusion_eng_on_th = eng_off_th;
		fusion_eng_off_th = FUSION_OFF_DEF; // Setting fusion eng off th to minimum valid voltage.
		fusion_idle_th = fusion_eng_on_th + fusion_idle_offset;

    }
    else {

		eng_off_th = parse_thresholds(pwr_volt_th_str, ENG_OFF_DEF, APM_Decision::eAPM_Decision_Engine_Off, min_v, max_v);
		eng_on_th = parse_thresholds(pwr_volt_th_str, ENG_ON_DEF, APM_Decision::eAPM_Decision_Moving, min_v, max_v);
		idle_th = parse_thresholds(pwr_volt_th_str, IDLE_DEF, APM_Decision::eAPM_Decision_Idling, min_v, max_v);

		// Fusion eng off th can be changed through override config.
		fusion_eng_off_th = parse_thresholds(pwr_volt_th_str, FUSION_OFF_DEF, APM_Decision::eAPM_Decision_Fusion_Engine_Off, min_v, max_v);
		fusion_eng_on_th = parse_thresholds(pwr_volt_th_str, FUSION_ON_DEF, APM_Decision::eAPM_Decision_Fusion_Moving, min_v, max_v);
		fusion_idle_th = parse_thresholds(pwr_volt_th_str, FUSION_IDLE_DEF, APM_Decision::eAPM_Decision_Fusion_Idling, min_v, max_v);

    }

	if(false == is_24v) {
		// 12 V thresholds
		sensor_cfg.eng_off_crank_v_th = eng_off_th;
		sensor_cfg.eng_on_crank_v_th = eng_on_th;
		sensor_cfg.idle_crank_v_th = idle_th;

		// 12 V Fusion thresholds
		sensor_cfg.fusion_eng_off_crank_v_th = fusion_eng_off_th;
		sensor_cfg.fusion_eng_on_crank_v_th = fusion_eng_on_th;
		sensor_cfg.fusion_idle_crank_v_th = fusion_idle_th;
	}
	else {
		// 24 V thresholds
		sensor_cfg.eng_off_crank_v_th_24v = eng_off_th;
		sensor_cfg.eng_on_crank_v_th_24v = eng_on_th;
		sensor_cfg.idle_crank_v_th_24v = idle_th;

		// 24 V Fusion thresholds
		sensor_cfg.fusion_eng_off_crank_v_th_24v = fusion_eng_off_th;
		sensor_cfg.fusion_eng_on_crank_v_th_24v = fusion_eng_on_th;
		sensor_cfg.fusion_idle_crank_v_th_24v = fusion_idle_th;

	}
}

// TO update the sensor decision flag
void update_sensor_flag(bool &disable_sensor_decision, bool val_overriden, const float sensor_threshold) {
	if( (true == val_overriden) ){
		disable_sensor_decision = (sensor_threshold <= SENSOR_DECISION_DISABLE_THRESHOLD) ? true : false;
	}
}
// To update the sensor decision flag and the actual sensor flag
void update_sensor_flag(bool &disable_sensor_decision, bool& sensor_flag, bool val_overriden, const float sensor_threshold) {
	if( (true == val_overriden) ){
		disable_sensor_decision = (sensor_threshold <= SENSOR_DECISION_DISABLE_THRESHOLD) ? true : false;
	}
	sensor_flag = sensor_flag || ((true == val_overriden) && (sensor_threshold > SENSOR_DECISION_DISABLE_THRESHOLD));
}

void read_decision_window_from_config(Config_parser &c, const std::string key, int dec_window_array[], bool &dec_window_overridden) {
	// Initialize decision window default string
	string def_dec_window_str = to_string(dec_window_array[eDecisionWindow_Short]) + "," + to_string(dec_window_array[eDecisionWindow_Long]) + "," + to_string(dec_window_array[eDecisionWindow_Outage]) + "," +
		to_string(dec_window_array[eDecisionWindow_Valid]) + "," + to_string(dec_window_array[eDecisionWindow_Sleep]) + "," + to_string(dec_window_array[eDecisionWindow_Median]) + "," + to_string(dec_window_array[eDecisionWindow_Continuous]) + "," + to_string(dec_window_array[eDecisionWindow_Cache]);
	bool get_override_val = true;
	string dec_window_str = c.getConfig("apm", key, def_dec_window_str, get_override_val, dec_window_overridden);
	for(int i = 0; i < eDecisionWindow_Max; ++i) {
		dec_window_array[i] = parse_thresholds(dec_window_str, dec_window_array[i], i, DEF_DECISOIN_WINDOW_MIN, DEF_DECISOIN_WINDOW_MAX);
		LOG_I(TAG, "Key %s-  Decision Window %d: %d", key.c_str(), i, dec_window_array[i]);
	}
}

void read_eng_stat_config_and_threhsolds() {
	// Read config
	Config_parser c(BAGHEERA_CONFIG_INI);
    if (true != c.getParseStatus()) {
        LOG_E(TAG, "Config_parser failed");
    }
	bool get_override_val = true, is_val_overridden = false;
	READ_CONFIG_VALUE(bool, engine_status_detection_enable, "apm", "engine_status_detection", false, get_override_val, is_val_overridden);
	LOG_I(TAG, "Engine status detection enable: %d", engine_status_detection_enable);

	is_val_overridden = false;
	float engine_imu_threshold_local = DEF_IMU_ACCEL_RMS_ON_THRESHOLD;
	READ_CONFIG_VALUE(float, engine_imu_threshold_local, "apm", "engine_imu_threshold", engine_imu_threshold, get_override_val, is_val_overridden);
	// If engine imu threshold present as part of both config in override then use "engine_imu_threshold"
	if(true == is_val_overridden) { // To avoid override if already overridden by apm_imu_threshold config which is used for motion detection
		engine_imu_threshold = engine_imu_threshold_local;
	}else if(false == engine_imu_threshold_overridden){
		engine_imu_threshold = engine_imu_threshold_local;
	}
	if((true == is_val_overridden) || (true == engine_imu_threshold_overridden)){
		update_sensor_flag(apm_eng_stat_disable_sensor, engine_status_detection_enable, is_val_overridden || engine_imu_threshold_overridden, engine_imu_threshold);
		LOG_I(TAG, "Engine Stat Detection descision status %d, after checking engine_imu_threshold", !apm_eng_stat_disable_sensor);
		// Update off threshold according to on threshold
		if(true == apm_eng_stat_disable_sensor){
			engine_imu_threshold = DEF_IMU_ACCEL_RMS_ON_THRESHOLD;
			engine_imu_threshold_off = DEF_IMU_ACCEL_RMS_OFF_THRESHOLD;
		}else{
			engine_imu_threshold_off = engine_imu_threshold * DEF_OFF_ON_IMU_RMS_RATIO;
		}
		LOG_I(TAG, "Engine IMU off threshold updated to %f based on on threshold %f", engine_imu_threshold_off, engine_imu_threshold);
	}

	read_decision_window_from_config(c, "eng_stat_dec_window", eng_stat_dec_window, eng_stat_dec_window_overridden);

	READ_CONFIG_VALUE(float, eng_stat_thr.imu_accel_rms_on_threshold, "apm", "imu_accel_rms_on", engine_imu_threshold, get_override_val, is_val_overridden);
	READ_CONFIG_VALUE(float, eng_stat_thr.imu_accel_rms_off_threshold, "apm", "imu_accel_rms_off", engine_imu_threshold_off, get_override_val, is_val_overridden);
	READ_CONFIG_VALUE(float, eng_stat_thr.imu_gyro_rms_on_threshold, "apm", "imu_gyro_rms_on", DEF_IMU_GYRO_RMS_ON_THRESHOLD, get_override_val, is_val_overridden);
	READ_CONFIG_VALUE(float, eng_stat_thr.imu_gyro_rms_off_threshold, "apm", "imu_gyro_rms_off", DEF_IMU_GYRO_RMS_OFF_THRESHOLD, get_override_val, is_val_overridden);
	READ_CONFIG_VALUE(float, eng_stat_thr.imu_motion_score_on_threshold, "apm", "imu_motion_score_on", DEF_IMU_MOTION_SCORE_ON_THRESHOLD, get_override_val, is_val_overridden);
	READ_CONFIG_VALUE(float, eng_stat_thr.imu_motion_score_off_threshold, "apm", "imu_motion_score_off", DEF_IMU_MOTION_SCORE_OFF_THRESHOLD, get_override_val, is_val_overridden);
	READ_CONFIG_VALUE(float, eng_stat_thr.imu_motion_score_alpha, "apm", "imu_motion_score_alpha", DEF_IMU_MOTION_SCORE_ALPHA, get_override_val, is_val_overridden);
	READ_CONFIG_VALUE(float, eng_stat_thr.imu_rms_percentage_threshold, "apm", "imu_rms_percentage_threshold", DEF_MOVING_CONFIDENCE_PERCENTAGE, get_override_val, is_val_overridden);

}
void parse_float_threshold(const std::string& val, int decimalPlaces, float &first_part, float &second_part, float default_threshold)
{
    size_t dot = val.find('.');
    std::string intPart = (dot == std::string::npos) ? val : val.substr(0, dot);
    std::string frac    = (dot == std::string::npos) ? ""  : val.substr(dot + 1);

	// Pad fractional part
    if (frac.length() < decimalPlaces) {
        frac.append(decimalPlaces - frac.length(), '0');
    }

	int splitPos = decimalPlaces - 1;
    // Split strictly at decimalPlaces
    std::string first_frac  = frac.substr(0, splitPos);
    std::string second_frac = (frac.length() > splitPos) ? frac.substr(splitPos) : "0";

    std::string first_part_str  = intPart + "." + first_frac;
    std::string second_part_str = "0." + second_frac;

    if(!string_to_float(first_part_str, first_part)) {
        first_part = default_threshold;
        LOG_E(TAG, "Failed to convert '%s' to float for first_part, using default %f", first_part_str.c_str(), default_threshold);
    }

    if(!string_to_float(second_part_str, second_part)) {
        second_part = 0.0;
        LOG_E(TAG, "Failed to convert '%s' to float for second_part, setting zero", second_part_str.c_str());
    }

    LOG_I(TAG, "Parsed float thresholds: first_part = %f, second_part = %f", first_part, second_part);
}

std::string get_value_by_comma_number(const std::string& input, int comma_number)
{
    // If no comma exists, return the whole string
    if (input.find(',') == std::string::npos){
        return input;
	}
    int current = 1;
    size_t start = 0;
    size_t end;
    while ((end = input.find(',', start)) != std::string::npos) {
        if (current == comma_number){
            return input.substr(start, end - start);
		}
        start = end + 1;
        current++;
    }
    // Handle the last value
    if (current == comma_number)
        return input.substr(start);
    // If requested index is larger than number of values,
    // return the first value as fallback
    size_t first_comma = input.find(',');
    return input.substr(0, first_comma);
}

void check_pwr_volt_fusion_override(string power_volt_thresholds_str, bool& ovr_pwr_fus_volt_dis, bool val_overriden) {
	if(true == val_overriden){
		string power_volt_first_str = get_value_by_comma_number(power_volt_thresholds_str, 1);
		float pwr_volt_fusion_enable_first = 0.0, pwr_volt_fusion_enable_second = 0.0;
		parse_float_threshold(power_volt_first_str, DECIMAL_PLACES_FLOAT_THRESHOLD_IMU_AND_PWR, pwr_volt_fusion_enable_first, pwr_volt_fusion_enable_second, ENGINE_ON_CRANK_VOLTAGE_THRESHOLD);
		if(pwr_volt_fusion_enable_second <= SENSOR_DECISION_DISABLE_THRESHOLD){
			ovr_pwr_fus_volt_dis = true; // To indicate that pwr fusion disable through override
			sensor_cfg.pwr_volt_fusion_enable = false; // disable pwr volt fusion
			LOG_I(TAG, "Power volt fusion disabled through override");
		}
	}
}

void get_config() {
    Config_parser c(BAGHEERA_CONFIG_INI);
    if (true != c.getParseStatus()) {
        LOG_E(TAG, "Config_parser failed");
    }

	// In Bagheera2 where WOM not supported, motion detection sensor are by default disabled (IMU, GPS, CAN, PWR_VOLT)
	if(false == nd_device_obj->is_wake_on_motion_supported()){
		apm_ign_disable_sensor = false;
		apm_pfi_disable_sensor = false;
		apm_imu_disable_sensor = true;
		apm_gps_disable_sensor = true;
		apm_can_disable_sensor = true;
		apm_pwr_disable_sensor = true;
	}

	NDDeviceTypeT device_type = ND_DeviceFactory::getBuildDeviceType();
	VehicleClass veh_class_enum = set_vehicle_class();
	APM::Instance()->set_veh_class(veh_class_enum);
	LOG_I(TAG, "veh_class_enum = %d", veh_class_enum);

    bool get_override_val = true, val_overridden = false;

	// easy install
	READ_CONFIG_VALUE(integer, easy_install, "apm", "easy_install", easy_install, get_override_val, val_overridden);
	READ_CONFIG_VALUE(bool, apm_motion_detection_enable, "apm", "apm_motion_detection", DEF_APM_MOTION_DET, get_override_val, val_overridden);
	READ_CONFIG_VALUE(bool, apm_wom_enable, "apm", "apm_wom_enable", DEF_APM_WOM, get_override_val, val_overridden);
	READ_CONFIG_VALUE(bool, apm_ign_enable, "apm", "apm_igns_enable", DEF_APM_IGNS, get_override_val, val_overridden);
	READ_CONFIG_VALUE(bool, apm_imu_enable, "apm", "apm_imu_enable", DEF_APM_IMU, get_override_val, val_overridden);
	READ_CONFIG_VALUE(bool, apm_gps_enable, "apm", "apm_gps_enable", DEF_APM_GPS, get_override_val, val_overridden);
	READ_CONFIG_VALUE(bool, apm_supercap_enable, "apm", "apm_supercap_enable", DEF_APM_SUPERCAP, get_override_val, val_overridden);
	READ_CONFIG_VALUE(bool, apm_can_enable, "apm", "apm_can_enable", DEF_APM_CAN, get_override_val, val_overridden);
	READ_CONFIG_VALUE(integer, obd_rpm_threshold, "apm", "obd_rpm_threshold", DEF_OBD_RPM_THRESHOLD, get_override_val, val_overridden);
	READ_CONFIG_VALUE(bool, apm_power_volt_enable, "apm", "apm_crank_volt_enable", DEF_APM_POWER_VOLT_ENABLE, get_override_val, val_overridden);

	bool val_overridden_wom_x = false, val_overridden_wom_y = false, val_overridden_wom_z = false;
	READ_CONFIG_VALUE(integer, wom_x_thr, "apm", "apm_wom_x_thr", apm_wom_utils::default_wom_thres_map[(int)veh_class_enum][(int)device_type][(int)wom_axis_t::eWOM_X_AXIS], get_override_val, val_overridden_wom_x);
	READ_CONFIG_VALUE(integer, wom_y_thr, "apm", "apm_wom_y_thr", apm_wom_utils::default_wom_thres_map[(int)veh_class_enum][(int)device_type][(int)wom_axis_t::eWOM_Y_AXIS], get_override_val, val_overridden_wom_y);
	READ_CONFIG_VALUE(integer, wom_z_thr, "apm", "apm_wom_z_thr", apm_wom_utils::default_wom_thres_map[(int)veh_class_enum][(int)device_type][(int)wom_axis_t::eWOM_Z_AXIS], get_override_val, val_overridden_wom_z);

	// READING PWR VOLT THRESHOLDS before IMU THRESHOLDS
	// FORMAT: ENG_OFF, ENG_on, IDLE, FUSION_ENG_OFF, FUSION_ENG_ON, FUSION_IDLE
	// Example: 12.21,12.41,13.01,11.81,12.21,13.21
	val_overridden = false;
	// Fusion enabled by default, it will only be disabled if threshold coming through override and value after 3 decimal place is zero or less than zero.
	// This flag will only be used to disable fusion.
	bool ovr_pwr_fus_volt_dis = false;
	std::string power_volt_thresholds_str = c.getConfig("apm", "engine_voltage_threshold", DEF_POWER_VOLTAGE_THRESHOLD, get_override_val, val_overridden);

	// 12 V thresholds
	power_volt_threshold = parse_thresholds(power_volt_thresholds_str, power_volt_threshold, APM_Decision::eAPM_Decision_Engine_Off, SENSOR_DECISION_DISABLE_THRESHOLD, nd_factory_utils::get_max_valid_voltage());
	configure_crank_voltage_thresholds(sensor_cfg, power_volt_threshold, power_volt_thresholds_str, false);
	update_sensor_flag(apm_pwr_disable_sensor, val_overridden, power_volt_threshold);
	check_pwr_volt_fusion_override(power_volt_thresholds_str, ovr_pwr_fus_volt_dis, val_overridden);

	// 24 V thresholds
	val_overridden = false;
	power_volt_thresholds_str = c.getConfig("apm", "engine_voltage_threshold_24V", DEF_POWER_VOLTAGE_THRESHOLD_24V, get_override_val, val_overridden);
	power_volt_threshold_24V = parse_thresholds(power_volt_thresholds_str, power_volt_threshold_24V, APM_Decision::eAPM_Decision_Engine_Off, SENSOR_DECISION_DISABLE_THRESHOLD, nd_factory_utils::get_max_valid_voltage());
	configure_crank_voltage_thresholds(sensor_cfg, power_volt_threshold_24V, power_volt_thresholds_str, true);
	// Both 12v abd 24v threshold should not be present in the override config, at the same time.
	check_pwr_volt_fusion_override(power_volt_thresholds_str, ovr_pwr_fus_volt_dis, val_overridden);

	bool apm_pwr_disable_sensor_24V = true;
	update_sensor_flag(apm_pwr_disable_sensor_24V, val_overridden, power_volt_threshold_24V);
	apm_pwr_disable_sensor = apm_pwr_disable_sensor || apm_pwr_disable_sensor_24V;

	val_overridden = false;
	// READ_CONFIG_VALUE(float, imu_threshold, "apm", "imu_threshold", DEF_IMU_THRESHOLD, get_override_val, val_overridden);
	string imu_threshold_str = c.getConfig("apm", "imu_threshold", std::to_string(DEF_IMU_THRESHOLD), get_override_val, val_overridden);

	// Parse "imu_threshold" to get engine_imu_threshold if present after 3 decimals (default imu_threshold is 0.25)
	// But if "imu_threshold" is present like this in the override "0.250030", the first the 3 decimal places are considered for imu_threshold and the next 3 decimal places are considered for engine_imu_threshold.
	//  Also enable power volt sensor decision if engine_imu_threshold is overridden since it uses engine status detection in fusion.
	float imu_threshold_before_decimal = DEF_IMU_THRESHOLD, imu_threshold_after_decimal =  0.0;
	parse_float_threshold(imu_threshold_str, DECIMAL_PLACES_FLOAT_THRESHOLD_IMU_AND_PWR, imu_threshold_before_decimal, imu_threshold_after_decimal, DEF_IMU_THRESHOLD);
	imu_threshold = imu_threshold_before_decimal;
	update_sensor_flag(apm_imu_disable_sensor, apm_imu_enable, val_overridden, imu_threshold);
	if(imu_threshold == SENSOR_DECISION_DISABLE_THRESHOLD){
		imu_threshold = DEF_IMU_THRESHOLD;
	}
	if(imu_threshold_after_decimal > SENSOR_DECISION_DISABLE_THRESHOLD){
		engine_status_detection_enable = true;
		apm_eng_stat_disable_sensor = false;
		apm_power_volt_enable = true;
		apm_pwr_disable_sensor = false;
		apm_pwr_use_legacy = false;
		engine_imu_threshold = imu_threshold_after_decimal;
		engine_imu_threshold_overridden = true; // Used to indicate that engine imu threshold is overridden through imu_threshold config in override config file.
		imu_fusion_active = true;
		LOG_I(TAG, "Engine status detection will be enabled in fusion since engine_imu_threshold is overridden in imu_threshold config with value %f, engine_imu_threshold: %f", imu_threshold, engine_imu_threshold);
	}

	READ_CONFIG_VALUE(float, imu_engine_off_threshold, "apm", "imu_threshold_off", DEF_IMU_ENGINE_OFF_THRESHOLD, get_override_val, val_overridden);
	READ_CONFIG_VALUE(float, imu_speed_threshold, "apm", "imu_speed_threshold", DEF_IMU_SPEED_THRESHOLD, get_override_val, val_overridden);
	READ_CONFIG_VALUE(float, imu_speed_engine_off_threshold, "apm", "imu_speed_threshold_off", DEF_IMU_SPEED_ENGINE_OFF_THRESHOLD, get_override_val, val_overridden);
	READ_CONFIG_VALUE(float, imu_dist_threshold, "apm", "imu_distance_threshold", DEF_IMU_DIST_THRESHOLD, get_override_val, val_overridden);
	READ_CONFIG_VALUE(float, imu_dist_engine_off_threshold, "apm", "imu_distance_threshold_off", DEF_IMU_DIST_ENGINE_OFF_THRESHOLD, get_override_val, val_overridden);
	READ_CONFIG_VALUE(float, gps_accuracy, "apm", "gps_accuracy", MIN_GPS_ACCURACY, get_override_val, val_overridden);

	val_overridden = false;
	// READ_CONFIG_VALUE(float, gps_speed_threshold, "apm", "gps_speed", DEF_SPEED_THRESHOLD, get_override_val, val_overridden);
	std::string gps_speed_threshold_str = c.getConfig("apm", "gps_speed", std::to_string(DEF_SPEED_THRESHOLD), get_override_val, val_overridden);
	float gps_speed_threshold_before_decimal = DEF_SPEED_THRESHOLD, gps_speed_threshold_after_decimal =  0.0;
	parse_float_threshold(gps_speed_threshold_str, DECIMAL_PLACES_FLOAT_THRESHOLD_OTHERS, gps_speed_threshold_before_decimal, gps_speed_threshold_after_decimal, DEF_SPEED_THRESHOLD);
	gps_speed_threshold = gps_speed_threshold_before_decimal;
	update_sensor_flag(apm_gps_disable_sensor, apm_gps_enable, val_overridden, gps_speed_threshold);
	if(gps_speed_threshold == SENSOR_DECISION_DISABLE_THRESHOLD){
		gps_speed_threshold = DEF_SPEED_THRESHOLD;
	}
	if(gps_speed_threshold_after_decimal > SENSOR_DECISION_DISABLE_THRESHOLD){
		gps_fusion_active = true;
		LOG_I(TAG, "%s : gps_speed_threshold_before_decimal: %f, gps_speed_threshold_after_decimal: %f, gps_fusion_active: %d",
				gps_speed_threshold_str.c_str(),
				gps_speed_threshold_before_decimal,
				gps_speed_threshold_after_decimal,
				gps_fusion_active);
	}

	READ_CONFIG_VALUE(float, gps_speed_engine_off_threshold, "apm", "gps_speed_off", DEF_SPEED_ENGINE_OFF_THRESHOLD, get_override_val, val_overridden);
	READ_CONFIG_VALUE(double, gps_latitude_threshold, "apm", "gps_latitude_threshold", DEF_LAT_THRESHOLD, get_override_val, val_overridden);
	READ_CONFIG_VALUE(double, gps_longitude_threshold, "apm", "gps_longitude_threshold", DEF_LONG_THRESHOLD, get_override_val, val_overridden);

	// SuperCap thresholds
	val_overridden = false;
	READ_CONFIG_VALUE(float, sensor_cfg.pfi_off_crank_v_th, "apm", "supercap_threshold_off", DEF_SUPERCAP_UPPER_THRESHOLD, get_override_val, val_overridden);
	val_overridden = false;
	READ_CONFIG_VALUE(float, supercap_threshold, "apm", "supercap_threshold", DEF_SUPERCAP_LOWER_THRESHOLD, get_override_val, val_overridden);

	if(supercap_threshold == SENSOR_DECISION_DISABLE_THRESHOLD) {
		sensor_cfg.pfi_on_crank_v_th = DEF_SUPERCAP_LOWER_THRESHOLD;
	}
	else {
		sensor_cfg.pfi_on_crank_v_th = supercap_threshold;
	}
	update_sensor_flag(apm_pfi_disable_sensor, val_overridden, supercap_threshold);

	val_overridden = false;
	READ_CONFIG_VALUE(float, sensor_cfg.pfi_off_crank_v_th_24v, "apm", "supercap_threshold_off_24V", DEF_SUPERCAP_UPPER_THRESHOLD_24V, get_override_val, val_overridden);
	val_overridden = false;
	READ_CONFIG_VALUE(float, supercap_threshold_24V, "apm", "supercap_threshold_24V", DEF_SUPERCAP_LOWER_THRESHOLD_24V, get_override_val, val_overridden);

	if(supercap_threshold_24V == SENSOR_DECISION_DISABLE_THRESHOLD) {
		sensor_cfg.pfi_on_crank_v_th_24v = DEF_SUPERCAP_LOWER_THRESHOLD_24V;
	}
	else {
		sensor_cfg.pfi_on_crank_v_th_24v = supercap_threshold_24V;
	}
	bool apm_pfi_disable_sensor_24V = false; // This should be false. Sensor decision should be enabled by default.
	update_sensor_flag(apm_pfi_disable_sensor_24V, val_overridden, supercap_threshold_24V);
	apm_pfi_disable_sensor = apm_pfi_disable_sensor || apm_pfi_disable_sensor_24V;

	READ_CONFIG_VALUE(integer, vehicle_idle_time, "apm", "vehicle_idle_time", DEF_VEHICLE_IDLE_TIME_IN_SEC, get_override_val, val_overridden);
	READ_CONFIG_VALUE(bool, enable_ignition_based_wakeup, "apm", "enable_ignition_based_wakeup", DEF_ENABLE_IGNITION_BASED_WAKEUP, get_override_val, val_overridden);
	// Check wom thresholds if anyone is 0, disalbe wom
	bool disable_wom_threshold_x = false, disable_wom_threshold_y = false, disable_wom_threshold_z = false;
	update_sensor_flag(disable_wom_threshold_x, val_overridden_wom_x, wom_x_thr);
	update_sensor_flag(disable_wom_threshold_y, val_overridden_wom_y, wom_y_thr);
	update_sensor_flag(disable_wom_threshold_z, val_overridden_wom_z, wom_z_thr);
	if(disable_wom_threshold_x || disable_wom_threshold_y || disable_wom_threshold_z) {
		apm_wom_enable = false;
		LOG_I(TAG, "WOM disabled due to one or more WOM thresholds set to disable threshold x, y, z - %d, %d, %d", disable_wom_threshold_x, disable_wom_threshold_y, disable_wom_threshold_z);
	}

    if((false == enable_ignition_based_wakeup) && (false == apm_wom_enable)) {
		apm_wom_enable = true;
	    nd_service_obj->send_err_msg(SM_E_APM_CONFIG_OVERRIDE, 0, "wom enabled on ignition interrupt wake up disable" );
    }

	if((EASY_INSTALL_ENABLE == easy_install) && (false == apm_wom_enable)) {
		apm_wom_enable = true;
	    nd_service_obj->send_err_msg(SM_E_APM_CONFIG_OVERRIDE, 0, "WOM enabled as easy install is enabled" );
    }

	READ_CONFIG_VALUE(integer, obd_data_retry_count, "vehicle_data", "retry_count", DEF_OBD_DATA_RETRY_COUNT, get_override_val, val_overridden);
	READ_CONFIG_VALUE(integer, obd_data_retry_time, "vehicle_data", "retry_time", DEF_OBD_DATA_RETRY_TIME, get_override_val, val_overridden);
	READ_CONFIG_VALUE(integer, obd_engine_off_counter, "iosix", "engine_debounce_count", DEF_ENGINE_OFF_COUNTER, get_override_val, val_overridden);

    // Regardless of the config values, wom should be disable if it is not supported
	if(false == nd_device_obj->is_wake_on_motion_supported()){
		LOG_I(TAG, "WOM not supported on this device, disabling wom");
		apm_wom_enable = false;
		// If wom is not supported and ignition based wakeup is also disabled, then enable ignition based wakeup to make sure at least one wakeup mechanism is present.
		enable_ignition_based_wakeup = true;
	}

	READ_CONFIG_VALUE(bool, glitch_suppression_support, "apm", "glitch_suppression_support", DEF_GLITCH_SUPPRESSION_SUPPORT_VAL, get_override_val, val_overridden);

	READ_CONFIG_VALUE(bool, apm_imu_use_legacy, "apm", "imu_use_legacy", apm_imu_use_legacy, get_override_val, val_overridden);
   	READ_CONFIG_VALUE(bool, apm_gps_use_legacy, "apm", "gps_use_legacy", apm_gps_use_legacy, get_override_val, val_overridden);
   	READ_CONFIG_VALUE(bool, apm_can_use_legacy, "apm", "can_use_legacy", apm_can_use_legacy, get_override_val, val_overridden);
   	READ_CONFIG_VALUE(bool, apm_pwr_use_legacy, "apm", "pwr_use_legacy", apm_pwr_use_legacy, get_override_val, val_overridden);

	// adaptive wom trigger count
	READ_CONFIG_VALUE(integer, adaptive_wom_trigger_count, "apm", "adaptive_wom_trigger_count", adaptive_wom_trigger_count, get_override_val, val_overridden);

	// Initialize IMU thresholds with defaults for all axes
	float imu_decision_default_thres[eAPM_Decision_Max] = {imu_engine_off_threshold, imu_threshold, DEF_IMU_IDLE_THRESHOLD};
	for(int i = 0; i < eIMU_AxisMax; ++i) {
		for(int j = 0; j < eAPM_Decision_Max; ++j) {
			IMU_AXIS_THRESHOLDS[i][j] = imu_decision_default_thres[j];
		}
	}

	// Fusion enable/disable config
	// NOT READING FROM CONFIG
	// READ_CONFIG_VALUE(bool, sensor_cfg.pwr_volt_fusion_enable, "apm", "pwr_volt_fusion_enable", sensor_cfg.pwr_volt_fusion_enable, get_override_val, val_overridden);

	// Read per-axis IMU thresholds from config, override defaults if present
   	val_overridden = false;
   	string default_imu_thresholds_str = to_string(imu_threshold) + "," + to_string(imu_threshold) + "," + to_string(imu_threshold) + "," +
	                                      to_string(imu_threshold) + "," + to_string(imu_threshold) + "," + to_string(imu_threshold);
   	string imu_thresholds_str = c.getConfig("apm", "imu_axis_th", default_imu_thresholds_str, get_override_val, val_overridden);

	// string imu_fusion_on_threshold_str = c.getConfig("apm", "imu_fusion_on_threshold", to_string(DEF_ENGINE_ON_IMU_THRESHOLD_FUSION), get_override_val, val_overridden);
	// string imu_fusion_off_threshold_str = c.getConfig("apm", "imu_fusion_off_threshold", to_string(DEF_ENGINE_OFF_IMU_THRESHOLD_FUSION), get_override_val, val_overridden);

	for(int i = 0; i < eIMU_AxisMax; ++i) {
		float cur_axis_thres = parse_thresholds(imu_thresholds_str, imu_threshold, i, DEF_IMU_MIN_THRESHOLD, DEF_IMU_MAX_THRESHOLD);
		for(int j = 0; j < eAPM_Decision_Max; ++j) {
			if(j == eAPM_Decision_Engine_Off) {
				IMU_AXIS_THRESHOLDS[i][j] = imu_engine_off_threshold;
			}
			else if(j == eAPM_Decision_Moving) {
				IMU_AXIS_THRESHOLDS[i][j] = cur_axis_thres;
			}
			else if(j == eAPM_Decision_Idling) {
				IMU_AXIS_THRESHOLDS[i][j] = DEF_IMU_MIN_THRESHOLD;
			}
			// For Fusion decisions, use separate defaults
			else if(j == eAPM_Decision_Fusion_Engine_Off) {

				if(cur_axis_thres > SENSOR_DECISION_DISABLE_THRESHOLD) {
					float fusion_eng_off_offset = DEF_IMU_ENGINE_OFF_THRESHOLD - DEF_ENGINE_OFF_IMU_THRESHOLD_FUSION;
					IMU_AXIS_THRESHOLDS[i][j] = cur_axis_thres - fusion_eng_off_offset;
				}
				else {
					IMU_AXIS_THRESHOLDS[i][j] = DEF_ENGINE_OFF_IMU_THRESHOLD_FUSION;
				}

			}
			else if(j == eAPM_Decision_Fusion_Moving) {

				if(cur_axis_thres > SENSOR_DECISION_DISABLE_THRESHOLD) {
					float fusion_eng_on_offset = DEF_IMU_THRESHOLD - DEF_ENGINE_ON_IMU_THRESHOLD_FUSION;
					IMU_AXIS_THRESHOLDS[i][j] = cur_axis_thres - fusion_eng_on_offset;
				}
				else {
					IMU_AXIS_THRESHOLDS[i][j] = DEF_ENGINE_ON_IMU_THRESHOLD_FUSION;
				}
			}
			else if(j == eAPM_Decision_Fusion_Idling) {
				IMU_AXIS_THRESHOLDS[i][j] = DEF_IMU_MIN_THRESHOLD;
			}
			else{
				IMU_AXIS_THRESHOLDS[i][j] = cur_axis_thres;
			}
			LOG_I(TAG, "IMU Axis %d, Decision %d, Threshold: %f", i, j, IMU_AXIS_THRESHOLDS[i][j]);
		}
	}



	// Initialize GPS thresholds
	for(int i = 0;i < eAPM_Decision_Max; ++i) {
		if((i == eAPM_Decision_Engine_Off) || (i == eAPM_Decision_Fusion_Engine_Off)) {
			GPS_THRESHOLDS[eGPS_Speed][i] = gps_speed_engine_off_threshold;
		}
		else if((i == eAPM_Decision_Moving) || (i == eAPM_Decision_Fusion_Moving)){

			if(gps_speed_threshold > SENSOR_DECISION_DISABLE_THRESHOLD) {
				GPS_THRESHOLDS[eGPS_Speed][i] = gps_speed_threshold;
			}
			else {
				GPS_THRESHOLDS[eGPS_Speed][i] = DEF_SPEED_THRESHOLD; // setting default value
			}
		}
		else{
			// Idling and Fusion_Idling
			GPS_THRESHOLDS[eGPS_Speed][i] = gps_speed_engine_off_threshold;
		}

		GPS_THRESHOLDS[eGPS_Latitude][i] = gps_latitude_threshold;
		GPS_THRESHOLDS[eGPS_Longitude][i] = gps_longitude_threshold;

		LOG_I(TAG, "GPS Param %d, Decision %d, Threshold: %f", eGPS_Speed, i, GPS_THRESHOLDS[eGPS_Speed][i]);
		LOG_I(TAG, "GPS Param %d, Decision %d, Threshold: %f", eGPS_Latitude, i, GPS_THRESHOLDS[eGPS_Latitude][i]);
		LOG_I(TAG, "GPS Param %d, Decision %d, Threshold: %f", eGPS_Longitude, i, GPS_THRESHOLDS[eGPS_Longitude][i]);
	}
	LOG_I(TAG, "GPS Accuracy Threshold: %f", gps_accuracy);

	val_overridden = false;
	// READ_CONFIG_VALUE(float, can_rpm_threshold, "apm", "can_rpm_threshold", DEF_CAN_RPM_THRESHOLD, get_override_val, val_overridden);
	string can_rpm_threshold_str = c.getConfig("apm", "can_rpm_threshold", std::to_string(DEF_CAN_RPM_THRESHOLD), get_override_val, val_overridden);
	float can_rpm_threshold_before_decimal = DEF_CAN_RPM_THRESHOLD, can_rpm_threshold_after_decimal = 0.0;
	parse_float_threshold(can_rpm_threshold_str, DECIMAL_PLACES_FLOAT_THRESHOLD_OTHERS, can_rpm_threshold_before_decimal, can_rpm_threshold_after_decimal, DEF_CAN_RPM_THRESHOLD);
	can_rpm_threshold = can_rpm_threshold_before_decimal;
	update_sensor_flag(apm_can_disable_sensor, apm_can_enable, val_overridden, can_rpm_threshold);
	if(can_rpm_threshold == SENSOR_DECISION_DISABLE_THRESHOLD) {
		sensor_cfg.can_rpm_threshold = DEF_CAN_RPM_THRESHOLD;
	}
	else {
		sensor_cfg.can_rpm_threshold = can_rpm_threshold;
	}
	if(can_rpm_threshold_after_decimal > SENSOR_DECISION_DISABLE_THRESHOLD){
		can_fusion_active = true;
		LOG_I(TAG, "%s : can_rpm_threshold_before_decimal: %f, can_rpm_threshold_after_decimal: %f, can_fusion_active: %d",
				can_rpm_threshold_str.c_str(),
				can_rpm_threshold_before_decimal,
				can_rpm_threshold_after_decimal,
				can_fusion_active);
	}

	READ_CONFIG_VALUE(float, sensor_cfg.can_speed_threshold, "apm", "can_speed_threshold", DEF_CAN_SPEED_THRESHOLD, get_override_val, val_overridden);
	READ_CONFIG_VALUE(float, sensor_cfg.can_rpm_engine_off_threshold, "apm", "can_rpm_off", DEF_CAN_RPM_ENGINE_OFF_THRESHOLD, get_override_val, val_overridden);


	READ_CONFIG_VALUE(float, sensor_cfg.min_v_lim_12v, "power", "min_voltage_limit_V", sensor_cfg.min_v_lim_12v, get_override_val, val_overridden);
	READ_CONFIG_VALUE(float, sensor_cfg.min_v_lim_24v, "power", "min_voltage_limit_24V", sensor_cfg.min_v_lim_24v, get_override_val, val_overridden);

	if(apm_wom_enable == true){
		//Range check for WOM thresholds
		if((wom_x_thr < MIN_WOM_THRESHOLD) || (wom_x_thr > MAX_WOM_THRESHOLD)){
			wom_x_thr = apm_wom_utils::default_wom_thres_map[(int)veh_class_enum][(int)device_type][(int)wom_axis_t::eWOM_X_AXIS];
		}

		if((wom_y_thr < MIN_WOM_THRESHOLD) || (wom_y_thr > MAX_WOM_THRESHOLD)){
			wom_y_thr =  apm_wom_utils::default_wom_thres_map[(int)veh_class_enum][(int)device_type][(int)wom_axis_t::eWOM_Y_AXIS];
		}

		if((wom_z_thr < MIN_WOM_THRESHOLD) || (wom_z_thr > MAX_WOM_THRESHOLD)){
			wom_z_thr =  apm_wom_utils::default_wom_thres_map[(int)veh_class_enum][(int)device_type][(int)wom_axis_t::eWOM_Z_AXIS];
		}
	} else {
		wom_x_thr = wom_y_thr = wom_z_thr = MAX_WOM_THRESHOLD;
	}

    APM::Instance()->set_wom_thresholds(wom_x_thr, wom_y_thr, wom_z_thr);

	READ_CONFIG_VALUE(bool, apm_ign_use_legacy, "apm", "ign_use_legacy", apm_ign_use_legacy, get_override_val, val_overridden);
	READ_CONFIG_VALUE(bool, apm_pfi_use_legacy, "apm", "pfi_use_legacy", apm_pfi_use_legacy, get_override_val, val_overridden);
	// Enable Logs for Debugging
	uint64_t enable_log_debug = LOGGING_INTERVAL_SEC;
	val_overridden = false;
	READ_CONFIG_VALUE(uint64, enable_log_debug, "apm", "enable_log_debug", LOGGING_INTERVAL_SEC, get_override_val, val_overridden);
	if((true == val_overridden) && (enable_log_debug >= 0) && (enable_log_debug <= LOGGING_INTERVAL_SEC)) { // Log config is enabled, only if overridden and in valid range
		LOG_I(TAG, "APM Debug Logs Enabled, touching debug log file %s, interval %d", print_debug_log_file.c_str(), enable_log_debug);
		file_touch(print_debug_log_file);
		write_into_dev_shm_file(print_debug_log_file, enable_log_debug);
	}else{
		file_delete(print_debug_log_file);
	}

	val_overridden = false;
	READ_CONFIG_VALUE(float, ignition_threshold, "apm", "ignition_threshold", static_cast<int>(ignition_threshold), get_override_val, val_overridden);
	update_sensor_flag(apm_ign_disable_sensor, val_overridden, ignition_threshold);

	// Read config related to engine 
	read_eng_stat_config_and_threhsolds();

	// Check if all ignition sources are disabled
	if((false == apm_ign_enable) && (false == apm_imu_enable) && (false == apm_gps_enable) && (false == apm_can_enable) && (false == apm_power_volt_enable)) {
		if(true == enable_ignition_based_wakeup){
			LOG_C(TAG, "All ignition sources are disabled. enabling Crank Line Sensor");
			apm_ign_enable = true;
			apm_ign_disable_sensor = false;
			apm_ign_use_legacy = false;
		}
		if(true == apm_wom_enable){
			LOG_C(TAG, "All ignition sources are disabled. enabling Crank Volt Sensor");
			apm_power_volt_enable = true;
			apm_pwr_disable_sensor = false;
			apm_pwr_use_legacy = false;
			engine_status_detection_enable = true;
			apm_eng_stat_disable_sensor = false;
		}
	}

	// easy_intall is enabled
	// apm_power_volt_enable is false or apm_pwr_disable_sensor is true.
	// engine_status_detection_enable is false or apm_eng_stat_disable_sensor is true
	// Setting,
	// 		-> apm_power_volt_enable = true
	//      -> apm_pwr_disable_sensor = false
	//      -> engine_status_detection_enable = true
	//      -> apm_eng_stat_disable_sensor = false
	//      -> apm_pwr_use_legacy = false
	// 		-> apm_imu_enable = true (Since IMU is required for engine status detection in fusion)
	if( (EASY_INSTALL_ENABLE == easy_install)
		&& ( (false == apm_power_volt_enable)
		|| (true == apm_pwr_disable_sensor)
		|| (true == apm_pwr_use_legacy )
		|| (false == engine_status_detection_enable)
		|| (true == apm_eng_stat_disable_sensor)
		|| (false == apm_imu_enable)) ) {

		apm_power_volt_enable = true;
		apm_pwr_disable_sensor = false;
		apm_pwr_use_legacy = false;
		apm_imu_enable = true;
		engine_status_detection_enable = true;
		apm_eng_stat_disable_sensor = false;
		LOG_C(TAG, "Easy Install enabled, setting apm_power_volt_enable(%d), apm_pwr_disable_sensor(%d), apm_pwr_use_legacy(%d), apm_imu_enable(%d), engine_status_detection_enable(%d), apm_eng_stat_disable_sensor(%d)",
			apm_power_volt_enable,
			apm_pwr_disable_sensor,
			apm_pwr_use_legacy,
			apm_imu_enable,
			engine_status_detection_enable,
			apm_eng_stat_disable_sensor );
	}

	read_decision_window_from_config(c, "imu_dec_window", imu_dec_window, imu_dec_window_overridden);
	read_decision_window_from_config(c, "gps_dec_window", gps_dec_window, gps_dec_window_overridden);
	read_decision_window_from_config(c, "power_dec_window", power_volt_dec_window, power_volt_dec_window_overridden);
	read_decision_window_from_config(c, "can_dec_window", can_dec_window, can_dec_window_overridden);

	// Enable power volt fusion if engine state based decision is enabled or motion detection is enabled.
	// apm_motion_detection_enable:true  means: GPS -> true, IMU -> true, CAN -> true.
	if( ((false == apm_eng_stat_disable_sensor) || (true == apm_motion_detection_enable)) && (false == ovr_pwr_fus_volt_dis)) {
		sensor_cfg.pwr_volt_fusion_enable = true;
		LOG_I(TAG, "apm_motion_detection_enable(%d), apm_eng_stat_disable_sensor(%d) -> resetting power volt fusion enable to %d", apm_motion_detection_enable, apm_eng_stat_disable_sensor, sensor_cfg.pwr_volt_fusion_enable );
	}

	// IF Motion Detection is disabled, then decrease decision windows for Motion Sensors, to make sure that pseudo ignition events is detected quickly.
	if(false == apm_motion_detection_enable) {
		if(false == imu_dec_window_overridden) {
			LOG_I(TAG, "Motion detection is disabled, changing decision windows for IMU - long window: %d to %d, outage window: %d to %d", imu_dec_window[eDecisionWindow_Long], DEF_LONG_WINDOW_MOTION_DETECTION_DISABLED, imu_dec_window[eDecisionWindow_Outage], DEF_OUTAGE_WINDOW_MOTION_DETECTION_DISABLED);
			imu_dec_window[eDecisionWindow_Long] = DEF_LONG_WINDOW_MOTION_DETECTION_DISABLED;
			imu_dec_window[eDecisionWindow_Outage] = DEF_OUTAGE_WINDOW_MOTION_DETECTION_DISABLED;
		}
		if(false == gps_dec_window_overridden) {
			LOG_I(TAG, "Motion detection is disabled, changing decision windows for GPS - long window: %d to %d, outage window: %d to %d", gps_dec_window[eDecisionWindow_Long], DEF_LONG_WINDOW_MOTION_DETECTION_DISABLED, gps_dec_window[eDecisionWindow_Outage], DEF_OUTAGE_WINDOW_MOTION_DETECTION_DISABLED);
			gps_dec_window[eDecisionWindow_Long] = DEF_LONG_WINDOW_MOTION_DETECTION_DISABLED;
			gps_dec_window[eDecisionWindow_Outage] = DEF_OUTAGE_WINDOW_MOTION_DETECTION_DISABLED;
		}
		if(false == can_dec_window_overridden) {
			LOG_I(TAG, "Motion detection is disabled, changing decision windows for CAN - long window: %d to %d, outage window: %d to %d", can_dec_window[eDecisionWindow_Long], DEF_LONG_WINDOW_MOTION_DETECTION_DISABLED, can_dec_window[eDecisionWindow_Outage], DEF_OUTAGE_WINDOW_MOTION_DETECTION_DISABLED);
			can_dec_window[eDecisionWindow_Long] = DEF_LONG_WINDOW_MOTION_DETECTION_DISABLED;
			can_dec_window[eDecisionWindow_Outage] = DEF_OUTAGE_WINDOW_MOTION_DETECTION_DISABLED;
		}
		if(false == power_volt_dec_window_overridden) {
			LOG_I(TAG, "Motion detection is disabled, changing decision windows for Power Volt - long window: %d to %d, outage window: %d to %d", power_volt_dec_window[eDecisionWindow_Long], DEF_LONG_WINDOW_MOTION_DETECTION_DISABLED, power_volt_dec_window[eDecisionWindow_Outage], DEF_OUTAGE_WINDOW_MOTION_DETECTION_DISABLED);
			power_volt_dec_window[eDecisionWindow_Long] = DEF_LONG_WINDOW_MOTION_DETECTION_DISABLED;
			power_volt_dec_window[eDecisionWindow_Outage] = DEF_OUTAGE_WINDOW_MOTION_DETECTION_DISABLED;
		}
	}

	LOG_I(TAG, "apm_motion_detection            : %d", apm_motion_detection_enable);
	LOG_I(TAG, "apm_wom_enable                  : %d", apm_wom_enable);
	LOG_I(TAG, "apm_igns_enable                 : %d", apm_ign_enable);
	LOG_I(TAG, "apm_imu_enable                  : %d", apm_imu_enable);
	LOG_I(TAG, "apm_gps_enable                  : %d", apm_gps_enable);
	LOG_I(TAG, "apm_supercap_enable             : %d", apm_supercap_enable);
	LOG_I(TAG, "apm_can_enable                  : %d", apm_can_enable);
	LOG_I(TAG, "obd_rpm_threshold               : %d", obd_rpm_threshold);
	LOG_I(TAG, "apm_power_volt_enable           : %d", apm_power_volt_enable);
	// LOG_I(TAG, "apm_ign_volt_enable             : %d", apm_ign_volt_enable);
	LOG_I(TAG, "engine_status_detection_enable  : %d", engine_status_detection_enable);
	LOG_I(TAG, "apm_wom_x_thr                   : %d", wom_x_thr);
	LOG_I(TAG, "apm_wom_y_thr                   : %d", wom_y_thr);
	LOG_I(TAG, "apm_wom_z_thr                   : %d", wom_z_thr);
	LOG_I(TAG, "imu_threshold                   : %f", imu_threshold);
	LOG_I(TAG, "imu_threshold_off               : %f", imu_engine_off_threshold);
	LOG_I(TAG, "gps_accuracy                    : %f", gps_accuracy);
	LOG_I(TAG, "gps_speed                       : %f", gps_speed_threshold);
	LOG_I(TAG, "gps_speed_off                   : %f", gps_speed_engine_off_threshold);
	LOG_I(TAG, "gps_latitude_threshold          : %.10lf", gps_latitude_threshold);
	LOG_I(TAG, "gps_longitude_threshold         : %.10lf", gps_longitude_threshold);
	LOG_I(TAG, "supercap_upper_threshold        : %f", sensor_cfg.pfi_off_crank_v_th);
	LOG_I(TAG, "supercap_lower_threshold        : %f", sensor_cfg.pfi_on_crank_v_th);
	LOG_I(TAG, "supercap_upper_threshold_24V    : %f", sensor_cfg.pfi_off_crank_v_th_24v);
	LOG_I(TAG, "supercap_lower_threshold_24V    : %f", sensor_cfg.pfi_on_crank_v_th_24v);
	LOG_I(TAG, "vehicle_idle_time               : %d", vehicle_idle_time);
	LOG_I(TAG, "enable_ignition_based_wakeup    : %d", enable_ignition_based_wakeup);
	LOG_I(TAG, "obd_data_retry_count            : %d", obd_data_retry_count);
	LOG_I(TAG, "obd_data_retry_time             : %d", obd_data_retry_time);
	LOG_I(TAG, "obd_engine_off_counter          : %d", obd_engine_off_counter);
	LOG_I(TAG, "glitch_suppression_support      : %d", glitch_suppression_support);
	LOG_I(TAG, "imu_use_legacy                  : %d", apm_imu_use_legacy);
	LOG_I(TAG, "gps_use_legacy                  : %d", apm_gps_use_legacy);
	LOG_I(TAG, "can_use_legacy                  : %d", apm_can_use_legacy);
	LOG_I(TAG, "pwr_use_legacy                  : %d", apm_pwr_use_legacy);
	LOG_I(TAG, "ign_use_legacy                  : %d", apm_ign_use_legacy);
	LOG_I(TAG, "pfi_use_legacy                  : %d", apm_pfi_use_legacy);
	LOG_I(TAG, "adaptive_wom_trigger_count      : %d", adaptive_wom_trigger_count);
	LOG_I(TAG, "engine_voltage_threshold        : %f", power_volt_threshold);
	LOG_I(TAG, "eng_off_crank_v_th              : %f", sensor_cfg.eng_off_crank_v_th);
	LOG_I(TAG, "eng_on_crank_v_th               : %f", sensor_cfg.eng_on_crank_v_th);
	LOG_I(TAG, "idle_crank_v_th                 : %f", sensor_cfg.idle_crank_v_th);
	LOG_I(TAG, "fusion_eng_off_crank_v_th       : %f", sensor_cfg.fusion_eng_off_crank_v_th);
	LOG_I(TAG, "fusion_eng_on_crank_v_th        : %f", sensor_cfg.fusion_eng_on_crank_v_th);
	LOG_I(TAG, "fusion_idle_crank_v_th          : %f", sensor_cfg.fusion_idle_crank_v_th);
	LOG_I(TAG, "engine_voltage_threshold_24V    : %f", power_volt_threshold_24V);
	LOG_I(TAG, "eng_off_crank_v_th_24v          : %f", sensor_cfg.eng_off_crank_v_th_24v);
	LOG_I(TAG, "eng_on_crank_v_th_24v           : %f", sensor_cfg.eng_on_crank_v_th_24v);
	LOG_I(TAG, "idle_crank_v_th_24v             : %f", sensor_cfg.idle_crank_v_th_24v);
	LOG_I(TAG, "fusion_eng_off_crank_v_th_24v   : %f", sensor_cfg.fusion_eng_off_crank_v_th_24v);
	LOG_I(TAG, "fusion_eng_on_crank_v_th_24v    : %f", sensor_cfg.fusion_eng_on_crank_v_th_24v);
	LOG_I(TAG, "fusion_idle_crank_v_th_24v      : %f", sensor_cfg.fusion_idle_crank_v_th_24v);
	LOG_I(TAG, "pwr_volt_fusion_enable          : %d", sensor_cfg.pwr_volt_fusion_enable);
	LOG_I(TAG, "min_voltage_limit_12V           : %f", sensor_cfg.min_v_lim_12v);
	LOG_I(TAG, "min_voltage_limit_24V           : %f", sensor_cfg.min_v_lim_24v);
	LOG_I(TAG, "can_rpm_threshold               : %f", sensor_cfg.can_rpm_threshold);
	LOG_I(TAG, "can_rpm_off                     : %f", sensor_cfg.can_rpm_engine_off_threshold);
	LOG_I(TAG, "can_speed_threshold             : %f", sensor_cfg.can_speed_threshold);
	LOG_I(TAG, "ignition_threshold              : %f", ignition_threshold);
	LOG_I(TAG, "apm_ign_disable_sensor          : %d", apm_ign_disable_sensor);
	LOG_I(TAG, "apm_pfi_disable_sensor          : %d", apm_pfi_disable_sensor);
	LOG_I(TAG, "apm_pwr_disable_sensor          : %d", apm_pwr_disable_sensor);
	LOG_I(TAG, "apm_can_disable_sensor          : %d", apm_can_disable_sensor);
	LOG_I(TAG, "apm_imu_disable_sensor          : %d", apm_imu_disable_sensor);
	LOG_I(TAG, "apm_gps_disable_sensor          : %d", apm_gps_disable_sensor);
	LOG_I(TAG, "easy_install                    : %d", easy_install);
	LOG_I(TAG, "imu_accel_rms_on_threshold      : %f", eng_stat_thr.imu_accel_rms_on_threshold);
	LOG_I(TAG, "imu_accel_rms_off_threshold     : %f", eng_stat_thr.imu_accel_rms_off_threshold);
	LOG_I(TAG, "imu_gyro_rms_on_threshold       : %f", eng_stat_thr.imu_gyro_rms_on_threshold);
	LOG_I(TAG, "imu_gyro_rms_off_threshold      : %f", eng_stat_thr.imu_gyro_rms_off_threshold);
	LOG_I(TAG, "imu_motion_score_on_threshold   : %f", eng_stat_thr.imu_motion_score_on_threshold);
	LOG_I(TAG, "imu_motion_score_off_threshold  : %f", eng_stat_thr.imu_motion_score_off_threshold);
	LOG_I(TAG, "imu_motion_score_alpha          : %f", eng_stat_thr.imu_motion_score_alpha);
	LOG_I(TAG, "imu_percentage_on_threshold     : %f", eng_stat_thr.imu_rms_percentage_threshold);

}

bool is_24V_battery() {
	return (TRUE_STR == apm_attr_util::get_attr_status(vehicle_attributes, USER_24V_STR, FALSE_STR));
}

// This thread will update initial ignition status for gpio ign and pwr volt ign source
// pwr volt ign source:
// 		It will read voltage and if voltage is greater than engine idle threshold.
//		It will set pwr volt ign sysfs to IGNITION_ON
// gpio ign source:
//		It will read gpio ign status and if it is IGNITION_ON, it will set gpio ign sysfs to IGNITION_ON
// This thread will run on 300 ms interval
// Before creating constructor will stop this thread
void update_initial_ign_status_func() {
	float voltage = 0.0;
	float prev_voltage = 0.0;
	sensor_config_t sensor_cfg;

	ignition_status_t ign_status = IGNITION_OFF;
	int status_flag = 0;

	bool update_sysfs_gpio_ign = false;

	bool l_apm_ign_enable = false;
	bool l_apm_power_volt_enable = false;
	{
		bool get_override_val = true;
		bool val_overridden = false;

		Config_parser c(BAGHEERA_CONFIG_INI);
		if (true != c.getParseStatus()) {
			LOG_E(TAG, "Config_parser failed");
		}

		READ_CONFIG_VALUE(bool, l_apm_ign_enable, "apm", "apm_igns_enable", DEF_APM_IGNS, get_override_val, val_overridden);
		READ_CONFIG_VALUE(bool, l_apm_power_volt_enable, "apm", "apm_crank_volt_enable", DEF_APM_POWER_VOLT_ENABLE, get_override_val, val_overridden);
		LOG_I(TAG, "Initial Ignition Status Thread - apm_igns_enable: %d, apm_crank_volt_enable: %d", l_apm_ign_enable, l_apm_power_volt_enable);


		val_overridden = false;
		std::string power_volt_thresholds_str = c.getConfig("apm", "engine_voltage_threshold", DEF_POWER_VOLTAGE_THRESHOLD, get_override_val, val_overridden);

		// 12 V thresholds
		power_volt_threshold = parse_thresholds(power_volt_thresholds_str, power_volt_threshold, APM_Decision::eAPM_Decision_Engine_Off, SENSOR_DECISION_DISABLE_THRESHOLD, nd_factory_utils::get_max_valid_voltage());
		configure_crank_voltage_thresholds(sensor_cfg, power_volt_threshold, power_volt_thresholds_str, false);

		// 24 V thresholds
		val_overridden = false;
		power_volt_thresholds_str = c.getConfig("apm", "engine_voltage_threshold_24V", DEF_POWER_VOLTAGE_THRESHOLD_24V, get_override_val, val_overridden);
		power_volt_threshold_24V = parse_thresholds(power_volt_thresholds_str, power_volt_threshold_24V, APM_Decision::eAPM_Decision_Engine_Off, SENSOR_DECISION_DISABLE_THRESHOLD, nd_factory_utils::get_max_valid_voltage());
		configure_crank_voltage_thresholds(sensor_cfg, power_volt_threshold_24V, power_volt_thresholds_str, true);
	}

	float eng_idle_threshold = (true == is_24V_battery()) ? sensor_cfg.idle_crank_v_th_24v : sensor_cfg.idle_crank_v_th;
	LOG_I(TAG, "Initial Ignition Status Thread - Engine Idle Threshold: %f", eng_idle_threshold);

	while (false == constructor_called.load()) {

		voltage = nd_device_obj->get_voltage_value(eCRANK_VOLT);
		ign_status = nd_device_obj->get_ignition_status();

		if(true == l_apm_power_volt_enable) {
			if((voltage > prev_voltage) && (voltage > eng_idle_threshold) && (voltage < nd_factory_utils::get_max_valid_voltage())) {

				prev_voltage = voltage;

				//set CRANK VOLT as wakeup reason
				write_into_dev_shm_file(nd_factory_utils::get_wake_on_crank_volt_file(), voltage); // writing initial voltage to dev shm file
			}
			else if((voltage > prev_voltage) && (voltage < eng_idle_threshold) && (voltage < nd_factory_utils::get_max_valid_voltage())) {
				prev_voltage = voltage;
				write_into_dev_shm_file(nd_factory_utils::get_initial_crank_volt_file(), voltage); // writing initial voltage to dev shm file
			}
		}

		if((true == l_apm_ign_enable) && (IGNITION_ON == ign_status) && (false == update_sysfs_gpio_ign)) {

			update_sysfs_gpio_ign = true;

			write_into_sysfs_entry(get_sysfs_path_from_enum(PowermonParam::eGPIO_IGN), IGNITION_ON); // set pwr volt ign to IGNITION_ON
			write_into_sysfs_entry(nd_device_obj->gpio_crank_level_info_file(), IGNITION_ON); // Updating pseudo ignition to IGNITION_ON

			// set status flag for CRANK VOLT
			status_flag |= (1 << IGNS_MASK_POS);

			// write initial source status in ign_src_stat file.
			write_into_sysfs_entry(get_sysfs_path_from_enum(PowermonParam::eIGN_SRC_STAT), status_flag, true); // writing initial ignition source status to sysfs entry
		}
		usleep(300 * 1000); // 300 ms
	}
	return;
}

// Read initial voltage for D2XX devices
// As OBD service stating after approx. 10 seconds of APM start.
// So to avoid missing initial voltage reading, reading voltage here.
// This binary will read voltage 5 time with timeout of 3 seconds.
static void read_initial_voltage() {

	if((eKrait_1 == nd_device_obj->getDeviceType()) ||
	   (eKrait_2 == nd_device_obj->getDeviceType()) ) {

		const std::string path = "/home/ubuntu/.nddevice/latest/service/apm/apm_volt_read";
		const std::string volt_read_done = "/dev/shm/volt_read_done_apm";
		constexpr int MAX_UPTIME = 60; // seconds
		int uptime_sec = MS_TO_S(get_system_monotonic_time());

		if(true == file_is_present(path) && (false == file_is_present(volt_read_done)) && (uptime_sec < MAX_UPTIME)) {
			// execute binary to read initial voltage
			system_execute(TAG, path);
			if(false == file_touch(volt_read_done)) {
				LOG_E(TAG, "Failed to create volt_read_done file");
			}
			LOG_I(TAG, "Executed volt_read binary to read initial voltage");
		}
		else {
			LOG_I(TAG, "Volt_read binary not present or volt_read_done file already present or system uptime: %d seconds", uptime_sec);
		}
	}

	// created thread to update initial ign status for gpio ign and pwr volt ign source
	init_ign_status_th = std::thread(update_initial_ign_status_func);
}

/*
 * Func name: main()
 * function is responsible for invoking register and moniter function
 * returns 0 on success and -1 on failure
 */
int main() {

    nd_service_obj = NDService::get_service_obj(TAG);
	int reg_state=0;

	bool status_log = nd_log_init( log_dir.c_str() );
	if(status_log == false) {
		printf("unable to init logger :: Exiting from main");
		return 1;
	}  

#ifdef ROUTE_LOGS
	route_logs( log_dir.c_str() );
#endif

	LOG_I(TAG, "##### Starting APM #####");
#ifdef IGNITION_BROADCAST
	server.create_topic(TOPIC_APM_IGN_STATUS);
#endif
	nd_device_obj_init();
	// setting default based on device type
	set_defaults();

	read_initial_voltage();

	APM::Instance()->apm_msg_q =  nd_msgq_t::get_msgq( APM::Instance()->apm_q_name, nd_msgq_t::ND_MSGQ_SERVER );
	if( APM::Instance()->apm_msg_q == NULL ) {
		LOG_E(TAG, "failed in apm get_msgq ; Exiting from apm");
		return -1;
	}
	svc_util_init(APM::Instance()->apm_q_name,0);

	// send keepalive to SVC at start of apm
	keepalive_status_mutex.lock();
	if( !svc_util_send_keepalive(all_thread_keepalive_status) ) {
		LOG_E(TAG, "Something went wrong in sending keepalive to SVC");
	}
	all_thread_keepalive_status = all_thread_enable_status;    // initialize bit to 1 for all thread
	keepalive_status_mutex.unlock();

#if 0 // Commenting the code for enabling imu sensor at bootup from APM Service
	if(false == file_is_present(apm_bootup_file_path)){
		file_touch(apm_bootup_file_path); // Create file so that the same logic doesn't get triggered multiple times in service restart
		struct imu_config_t imu_init_cfg;
		read_init_imu_config(imu_init_cfg);
		// Enable apm_engine_status worker thread only when engine status detection is enabled
		eng_stat_worker_start_time = get_system_monotonic_time();
		engine_status_worker = new APM_Engine_Status_worker(imu_init_cfg,eng_stat_worker_start_time);
		engine_status_worker->enabled = engine_status_detection_enable;
		if(true == engine_status_detection_enable) {
			int reg_state = APM::Instance()->apm_register((char *)"A_ENG_STAT", engine_status_worker, eEngineStatus);
			if(-1 == reg_state) {
				LOG_W(TAG, "APM Register Failed for Engine Status.");
			}
		}else{
			LOG_I(TAG, "Engine status detection is disabled, not registering engine status worker to APM");
		}
	}else{
		LOG_I(TAG, "APM Bootup file already present, service restarted");
	}
#endif
	get_config();

	string cmd = "lsmod | grep gpio_ignition";
	string status = "";
	if (system_execute_with_resp(TAG,cmd,status) == false) {
		LOG_E(TAG, "failed to execute cmd: %s", cmd.c_str());
	}
	if ("" == status) {
		LOG_E (TAG,"gpio ignition module is not loaded");
		nd_service_obj->send_err_msg(SM_E_APM_FILE_OPEN_FAIL, 0, "gpio ignition module is not loaded" );
	}

	FILE *fp;
	fp = fopen(nd_device_obj->gpio_crank_level_info_file().c_str(), "r");

	if(NULL == fp) {
		LOG_E(TAG, "failed to open ignition sysfs file");
		nd_service_obj->send_err_msg(SM_E_APM_FILE_OPEN_FAIL, 0, "sysfs file open failed" );
	}
	else {
		fclose(fp);
	}

	// If aon is supported, read aon log and retry count
	if (true == nd_device_obj->is_aon_supported()) {

		// Printing AON Firmware Upgrade Status
		if (file_is_present("/dev/shm/aon_log.txt")) {
			ifstream aon_log("/dev/shm/aon_log.txt");
			if (aon_log.is_open()) {
				string aon_log_str("");
				while (getline(aon_log, aon_log_str)) {
					LOG_I(TAG, "AON FW LOG: %s", aon_log_str.c_str());
				}
			}
			else {
				LOG_E(TAG, "AON FW LOG FILE /dev/shm/aon_log.txt OPEN FAILED ");
			}
			aon_log.close();
		}
		else {
			LOG_E(TAG, "AON FW LOG FILE /dev/shm/aon_log.txt NOT PRESENT ");
		}
		if (file_is_present("/etc/init.d/aon_retry_count.txt")) {
			ifstream aon_retry_log("/etc/init.d/aon_retry_count.txt");
			if (aon_retry_log.is_open()) {
				string aon_retry_str("");
				aon_retry_log >> aon_retry_str;
				LOG_I(TAG, "AON FW UPGRADE RETRY COUNT: %s", aon_retry_str.c_str());
			}
			else {
				LOG_E(TAG, "AON FW UPGRADE RETRY COUNT FILE /etc/init.d/aon_retry_count.txt OPEN FAILED ");
			}
			aon_retry_log.close();
		}
		else {
			LOG_E(TAG, "AON FW UPGRADE RETRY COUNT FILE /etc/init.d/aon_retry_count.txt NOT PRESENT ");
		}
	}

	if(false == device_on_off_reason()) {
		LOG_E(TAG,"AON: Read power on off / wake up reason failed");
		nd_service_obj->send_err_msg(SM_E_APM_MSP_FAIL, 0, "aon read power on_off/wakeup reason failed");
	}

	int hw_rev = 1;
	nd_device_obj->get_hw_revision(&hw_rev);
	LOG_I(TAG, "HW revision = %d", hw_rev);

	// Enable Ignition
	if(true == enable_ignition_based_wakeup)   {
		if( false == nd_device_obj->configure_wake_on_ignition(true)) {
			LOG_E(TAG,"IMU AON: Failed to Enable IGN_WAKE_UP ");
			nd_service_obj->send_err_msg(SM_E_APM_MSP_FAIL, 0, "Failed to Enable IGN_WAKE_UP");
		} else {
			LOG_I(TAG, " AON IGN_WAKE_UP Enabled ");
		}
		apm_ign_wake_enable = true;
		LOG_I(TAG, "IGNS_INTR_MASK Enabled");
	} else {
		if( false == nd_device_obj->configure_wake_on_ignition(false)) {
			LOG_E(TAG,"IMU AON: Failed to Disable IGN_WAKE_UP ");
			nd_service_obj->send_err_msg(SM_E_APM_MSP_FAIL, 0, "Failed to Disable IGN_WAKE_UP");
		} else {
			LOG_I(TAG, " AON IGN_WAKE_UP Disabled ");
		}
		apm_ign_wake_enable = false;
		LOG_I(TAG, "IGNS_INTR_MASK Disabled");
	}

	if(false == nd_device_obj->init_imu_wom(apm_wom_enable)) {
		LOG_E(TAG, " AON WOM Init Failed!!!!");
		nd_service_obj->send_err_msg(SM_E_APM_MSP_FAIL, 0, "Failed to Configure WOM on AON");
	}

	if(true == apm_wom_enable) {
		if(true == apm_wom_thr_setting()) {
			LOG_I(TAG, " AON WOM Enabled ");
		}
	}

	/* Temperory code added to make sure IMU side WOM interrupt is always enabled
	 * irrespective of WOM enable from bagheera config
	 * Whenever this change is disabled in future, that future release would be dependant
	 * on the release in which this change is going
	 */
	//  if(true != nd_msp_write_imu_int_enable(WOM_ENABLE)) {
	//      LOG_E(TAG, "msp WOM disable failed");
	//      nd_service_obj->send_err_msg(SM_E_APM_REGISTER_FAIL, 0, "APM WOM Enable Failed");
	//  }

	device_info();

	// Verify WOM and IGN interrupt status
	nd_device_obj->verify_msp_intr_status(enable_ignition_based_wakeup, apm_wom_enable);

	if(eATTR_FILE_CREATED == apm_attr_util::create_attr_file(vehicle_ign_state)) {
		// Initialize all user attributes to STATIONARY
		apm_attr_util::set_attr_status(vehicle_ign_state, USER_IGN, STATIONARY_STR);
		apm_attr_util::set_attr_status(vehicle_ign_state, USER_PWR, STATIONARY_STR);
		apm_attr_util::set_attr_status(vehicle_ign_state, USER_GPS, STATIONARY_STR);
		apm_attr_util::set_attr_status(vehicle_ign_state, USER_IMU, STATIONARY_STR);
		apm_attr_util::set_attr_status(vehicle_ign_state, USER_CAN, STATIONARY_STR);
		apm_attr_util::set_attr_status(vehicle_ign_state, USER_PFI, STATIONARY_STR);

		int64_t curr_time = MS_TO_S(get_system_time());
		apm_attr_util::set_attr_status(vehicle_ign_state, USER_TIME, std::to_string(curr_time).c_str());
	}

	if(eATTR_FILE_CREATED == apm_attr_util::create_attr_file(vehicle_attributes)) {
		// Initialize attribute
		apm_attr_util::set_attr_status(vehicle_attributes, USER_12V_STR, FALSE_STR);
		apm_attr_util::set_attr_status(vehicle_attributes, USER_24V_STR, FALSE_STR);
		apm_attr_util::set_attr_status(vehicle_attributes, USER_IGNITION_EVENT_STR, "0");
		apm_attr_util::set_attr_status(vehicle_attributes, USER_EASY_INSTALL_STR, FALSE_STR);

		int64_t curr_time = MS_TO_S(get_system_time());
		apm_attr_util::set_attr_status(vehicle_attributes, USER_LAST_IGN_EVENT_TIME_STR, std::to_string(curr_time).c_str());
	}

	// to stop read voltage thread
	constructor_called.store(true);


	igns_worker = new IGNS_worker(apm_ign_use_legacy, apm_ign_disable_sensor);

	if (false == apm_ign_enable) {
		LOG_I(TAG,"ignition disabled");
		igns_worker->enabled = false;
	}
	else
	{

		reg_state = APM::Instance()->apm_register((char *)"A_IGNS", igns_worker, eCrankLine);
		if(-1 == reg_state ) {
			LOG_W(TAG, "APM Register Failed for Ign.");
		}
		/* Not setting keep alive bit because of no poll thread for IGNS */
	}

	// Passing engine status worker object to imu worker
	eng_stat_worker = new ENG_STAT_worker(eng_stat_dec_window, eng_stat_thr, apm_eng_stat_disable_sensor);
	eng_stat_worker->enabled = engine_status_detection_enable; // setting engine status worker enable/disable based on engine status detection config

	imu_worker = new  IMU_worker(IMU_AXIS_THRESHOLDS, apm_imu_use_legacy, apm_imu_disable_sensor, imu_dec_window, eng_stat_worker);
	if(false == apm_imu_enable) {
		LOG_I(TAG,"imu disabled");
		imu_worker->enabled = false;

		if(true == apm_wom_enable) {
			reg_state = APM::Instance()->apm_register((char *)"A_IMU", imu_worker, eInertial);
			if(-1 == reg_state) {
				LOG_W(TAG, "APM Register Failed for IMU.");
			}
		}
	}
	else{

		reg_state = APM::Instance()->apm_register((char *)"A_IMU", imu_worker, eInertial);

		bool is_null = (NULL == APM::WorkerInstance(eInertial));

	    if( (false == is_null) && (false == apm_imu_use_legacy) ) {
           APM::WorkerInstance(eInertial)->use_legacy = false;
       	}

		if(-1 == reg_state) {
			LOG_W(TAG, "APM Register Failed for IMU.");
		}

		if( (false == is_null) && (true == apm_motion_detection_enable) ) {
			APM::WorkerInstance(eInertial)->fusion_enabled = true;
			APM::WorkerInstance(eInertial)->set_fusion_activated(imu_fusion_active); // Set this based on threshold
		}

		LOG_I(TAG, "IMU: fusion_enabled: %d, fusion_active: %d", APM::WorkerInstance(eInertial)->fusion_enabled, APM::WorkerInstance(eInertial)->get_fusion_activated());
		all_thread_enable_status |= (1 << IMU_MASK_POS);
	}

	gps_worker = new  GPS_worker(GPS_THRESHOLDS, apm_gps_use_legacy, apm_gps_disable_sensor, gps_dec_window);
	if(false == apm_gps_enable) {
		LOG_I(TAG,"gps disabled");
		gps_worker->enabled = false;
	}
	else{

		reg_state = APM::Instance()->apm_register((char *)"A_GPS", gps_worker, eSatellite);

		if(-1 == reg_state) {
			LOG_W(TAG, "APM Register Failed for GPS.");
		}

		bool is_null = (NULL == APM::WorkerInstance(eSatellite));

		if((false == is_null) && (true == apm_motion_detection_enable)) {
			APM::WorkerInstance(eSatellite)->fusion_enabled = true;
			APM::WorkerInstance(eSatellite)->set_fusion_activated(gps_fusion_active); // Set this based on threshold
		}

		LOG_I(TAG, "GPS: fusion_enabled: %d, fusion_active: %d", APM::WorkerInstance(eSatellite)->fusion_enabled, APM::WorkerInstance(eSatellite)->get_fusion_activated());
		all_thread_enable_status |= (1 << GPS_MASK_POS);
	}

	sc_worker = new SC_worker(apm_pfi_use_legacy, sensor_cfg, apm_pfi_disable_sensor);
	if(false == apm_supercap_enable) {
		LOG_I(TAG,"supercap disabled");
		sc_worker->enabled = false;
	}
	else{

		reg_state = APM::Instance()->apm_register((char *)"A_SUPERCAP", sc_worker, ePowerFail);

		if(-1 == reg_state) {
			LOG_W(TAG, "APM Register Failed for SUPERCAP.");
		}
		/* Not setting keep alive bit because of no poll thread for SC */
	}

	can_worker = new CAN_worker(sensor_cfg, apm_can_use_legacy, apm_can_disable_sensor, can_dec_window);
	if ( false == apm_can_enable) {
		LOG_I(TAG,"can disabled");
		can_worker->enabled = false;
	}
	else
	{

		reg_state = APM::Instance()->apm_register((char *)"A_CAN", can_worker, eCANBus);
		if(-1 == reg_state ) {
			LOG_W(TAG, "APM Register Failed for CAN.");
		}

		bool is_null = (NULL == APM::WorkerInstance(eCANBus));

		if((false == is_null) && (true == apm_motion_detection_enable)) {
			APM::WorkerInstance(eCANBus)->fusion_enabled = true;
			APM::WorkerInstance(eCANBus)->set_fusion_activated(can_fusion_active); // Set this based on threshold
		}

		LOG_I(TAG, "CAN: fusion_enabled: %d, fusion_active: %d", APM::WorkerInstance(eCANBus)->fusion_enabled, APM::WorkerInstance(eCANBus)->get_fusion_activated());

		all_thread_enable_status |= (1 << CAN_MASK_POS);
	}

    power_volt_worker = new POWER_VOLT_worker(
		sensor_cfg,
       	apm_pwr_use_legacy,
		apm_pwr_disable_sensor,
		power_volt_dec_window
    );

    if(false == apm_power_volt_enable) {
		LOG_I(TAG,"power volt disabled");
		power_volt_worker->enabled = false;
	}
	else {
		LOG_I(TAG,"power volt enabled");

		reg_state = APM::Instance()->apm_register((char *)"A_POWER_VOLT", power_volt_worker, eCrankVolt);
		if(-1 == reg_state ) {
			LOG_W(TAG, "APM Register Failed for POWER_VOLT.");
		}

		LOG_I(TAG, "Power Volt: fusion_enabled: %d, fusion_active: %d", power_volt_worker->fusion_enabled, power_volt_worker->get_fusion_activated());
		all_thread_enable_status |= (1 << POWER_VOLT_MASK_POS);
	}


	if(true == apm_worker_utils::is_update_reader_count_supported()) {
		if((true == power_volt_worker->enabled) && (false == can_worker->enabled)) {
			power_volt_worker->update_reader_count = true;
		}

		if((true == can_worker->enabled) && (false == power_volt_worker->enabled)) {
			can_worker->update_reader_count = true;
		}
	}

	thread monitor_thread(&APM::start_monitor, APM::Instance());

	APM::Instance()->apm_unregister_all();
	monitor_thread.join();

	if(init_ign_status_th.joinable()) {
		init_ign_status_th.join();
	}

	nd_service_obj->release_service_obj();
	return 0;
}

