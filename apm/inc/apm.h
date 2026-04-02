/* Copyright (C) 2019 - 2020 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Devendra Yadav <devendra.yadav@netradyne.com>
 */

#ifndef __APM_H_
#define __APM_H_

#include <string>
#include <iostream>
#include <stddef.h>
#include <iomanip>
#include <sstream>
#include <cassert>
#include <vector>
#include <map>
#include <thread>
#include <log.h>
#include "service_utils.h"
#include <nd_factory.h>
#include "system_utils.h"
#include <atomic>
#include "nd_time.h"
#include "nd_file_utils.h"
#include "ndmb/nd_msg_interface.h"

static const unsigned int APM_SLEEP_TIME = 10;// 10 Seconds for SuperCap Polling
static const int MULTIPLY_BY_THOUSAND = 1000;
static const std::string log_dir = "/home/ubuntu/.nddevice/log/apm";
static const std::string BAGHEERA_CONFIG_INI = "/home/ubuntu/.nddevice/latest/bagheera_config.ini";
static const std::string DEVICE_CONFIG_INI = "/home/ubuntu/config/deviceconfig.ini";
bool write_to_sysfs(int );
void reset_bit(int );
void set_bit(int );
static void* APM_ERROR = (void*)-1;

static const float DEF_IMU_MAX_THRESHOLD = 16.0000;
static const float DEF_IMU_MIN_THRESHOLD = 0.001;
static const float DEF_MOVING_CONFIDENCE_PERCENTAGE = 0.51;// 51%

static const int DEF_VEHICLE_IDLE_TIME_IN_SEC =  180;
static const float DEF_IMU_THRESHOLD = 0.25000;
static const float DEF_IMU_ENGINE_OFF_THRESHOLD = 0.2000;
static const float DEF_IMU_IDLE_THRESHOLD = 0.05;
static const float DEF_IMU_SPEED_THRESHOLD = -1;
static const float DEF_IMU_SPEED_ENGINE_OFF_THRESHOLD = -1;
static const float DEF_IMU_DIST_THRESHOLD = -1;
static const float DEF_IMU_DIST_ENGINE_OFF_THRESHOLD = -1;
static const float DEF_IMU_HYSTERESIS_THRESHOLD = 0.0500;

#define DEF_ENGINE_OFF_IMU_THRESHOLD_FUSION  		0.05 // imu threshold below which we consider engine is OFF during crank condition in fusion
#define DEF_ENGINE_ON_IMU_THRESHOLD_FUSION   		0.15 // imu threshold above which we consider engine is ON during crank condition in fusion
#define DEF_ENGINE_IDLING_IMU_THRESHOLD_FUSION  	0.05 // imu threshold below which we consider engine is IDLING during crank condition in fusion

static const int DEF_IMU_ACCEL_WOM_X_THR =  50;
static const int DEF_IMU_ACCEL_WOM_Y_THR =  50;
static const int DEF_IMU_ACCEL_WOM_Z_THR =  50;
static const int DEF_OBD_DATA_RETRY_COUNT = 300;
static const int DEF_OBD_DATA_RETRY_TIME  = 10;
static const int DEF_ENGINE_OFF_COUNTER = 5;

constexpr int DEF_WOM_THRESHOLD = 50;
constexpr int MAX_WOM_THRESHOLD = 255;
constexpr int MIN_WOM_THRESHOLD = 5; // Changing min to 5 from 0, as values from 0 to 5 are highly sensitive and can cause false wakeups
//CLASS 1 - D2XX
constexpr int D2XX_MOVING_CLASS1_WOM_THRES = 135;
constexpr int D2XX_FLOOR_CLASS1_WOM_THRES = 25;
constexpr int D2XX_DOOR_CLASS1_WOM_THRES = 135;

//CLASS 1 - D450
constexpr int D450_MOVING_CLASS1_WOM_THRES = 40;
constexpr int D450_FLOOR_CLASS1_WOM_THRES = 30;
constexpr int D450_DOOR_CLASS1_WOM_THRES = 40;

//CLASS 2 - D2XX
constexpr int D2XX_MOVING_CLASS2_WOM_THRES = 125;
constexpr int D2XX_FLOOR_CLASS2_WOM_THRES = 25;
constexpr int D2XX_DOOR_CLASS2_WOM_THRES = 125;

//CLASS 2 - D450
constexpr int D450_MOVING_CLASS2_WOM_THRES = 35;
constexpr int D450_FLOOR_CLASS2_WOM_THRES = 30;
constexpr int D450_DOOR_CLASS2_WOM_THRES = 35;

enum VehicleClass {
	eVehicleClass0 = 0,
	eVehicleClass1,
	eVehicleClass2,
	eVehicleClass3,
	eVehicleClass4,
	eVehicleClass5,
	eVehicleClass6,
	eVehicleClass7,
	eVehicleClass8,
	eVehicleMax,
	eVehicleUnknown = eVehicleClass0
};

static const std::string diagnostic_q_name = "DIAGNOSTIC";

static const std::string DEF_POWER_VOLTAGE_THRESHOLD = "12.21,12.41,13.01,11.20,15.01,15.01";
static const std::string DEF_POWER_VOLTAGE_THRESHOLD_24V = "25.21,25.81,27.01,21.20,32.01,32.01";
static const int DEF_CAN_RPM_THRESHOLD = 350;
static const int DEF_CAN_RPM_ENGINE_OFF_THRESHOLD = 200;
static const int DEF_CAN_SPEED_THRESHOLD = 5;

static const float MIN_CAN_RPM_THRESHOLD = 0.0;
static const float MAX_CAN_RPM_THRESHOLD = 10000.0;

#define FAILURE -1

#define MIN_ENGINE_OFF_CRANK_VOLTAGE_THRESHOLD_24V  23.81 // Minimum valid Voltage for Engine Off Threshold
#define MAX_ENGINE_ON_CRANK_VOLTAGE_THRESHOLD_24V 	32.01 // Maximum valid Voltage for Engine On Threshold

#define MIN_ENGINE_OFF_CRANK_VOLTAGE_THRESHOLD  11.81 // Minimum valid Voltage for Engine Off Threshold
#define MAX_ENGINE_ON_CRANK_VOLTAGE_THRESHOLD 	15.01 // Maximum valid Voltage for Engine On Threshold

#define ENGINE_OFF_CRANK_VOLTAGE_THRESHOLD  	12.21 // Voltage below which we consider engine is OFF during crank condition
#define ENGINE_ON_CRANK_VOLTAGE_THRESHOLD   	12.41  // Voltage above which we consider engine is ON during crank condition
#define ENGINE_IDLING_CRANK_VOLTAGE_THRESHOLD 	13.01 // Voltage above which we consider engine is IDLING during crank condition

// Can we consider MIN_ENGINE_OFF_CRANK_VOLTAGE_THRESHOLD 11.81 as least for Fusion
#define DEF_ENGINE_OFF_CRANK_VOLTAGE_THRESHOLD_FUSION  		11.20 // Voltage below which we consider engine is OFF during crank condition in fusion
#define DEF_ENGINE_ON_CRANK_VOLTAGE_THRESHOLD_FUSION   		ENGINE_ON_CRANK_VOLTAGE_THRESHOLD // Voltage above which we consider engine is ON during crank condition in fusion
#define DEF_ENGINE_IDLING_CRANK_VOLTAGE_THRESHOLD_FUSION 	ENGINE_ON_CRANK_VOLTAGE_THRESHOLD // Voltage below which we consider engine is IDLING during crank condition in fusion

#define ENGINE_OFF_CRANK_VOLTAGE_THRESHOLD_24V  	25.21 // Voltage below which we consider engine is OFF during crank condition
#define ENGINE_ON_CRANK_VOLTAGE_THRESHOLD_24V   	26.21  // Voltage above which we consider engine is ON during crank condition
#define ENGINE_IDLING_CRANK_VOLTAGE_THRESHOLD_24V 	27.01 // Voltage above which we consider engine is IDLING during crank condition

#define DEF_ENGINE_OFF_CRANK_VOLTAGE_THRESHOLD_24V_FUSION  		21.20 // Voltage below which we consider engine is OFF during crank condition in fusion
#define DEF_ENGINE_ON_CRANK_VOLTAGE_THRESHOLD_24V_FUSION   		ENGINE_ON_CRANK_VOLTAGE_THRESHOLD_24V // Voltage above which we consider engine is ON during crank condition in fusion
#define DEF_ENGINE_IDLING_CRANK_VOLTAGE_THRESHOLD_24V_FUSION 	ENGINE_ON_CRANK_VOLTAGE_THRESHOLD_24V // Voltage above which we consider engine is IDLING during crank condition in fusion

#define DEF_MIN_POWER_VOLTAGE  		 10.01 // default 12V valid Voltage for any threshold
#define DEF_MIN_POWER_VOLTAGE_24V  	 20.01 // default 24V valid Voltage for any threshold
#define MIN_POWER_VOLTAGE_THRESHOLD  		 8.51 // Minimum valid Voltage for any threshold
#define MIN_POWER_VOLTAGE_THRESHOLD_24V  	17.01 // Minimum valid Voltage for any threshold

static const std::vector<std::string> wom_x_status_sys = {"", "", "wom_x_status", "wom_mode_x_status", "wom_mode_x_status"};
static const std::vector<std::string> wom_y_status_sys = {"", "", "wom_y_status", "wom_mode_y_status", "wom_mode_y_status"};
static const std::vector<std::string> wom_z_status_sys = {"", "", "wom_z_status", "wom_mode_z_status", "wom_mode_z_status"};

static const std::vector<std::string> wom_x_thres_sys = {"", "", "wom_x_thres_boot", "wom_mode_x_thres_boot", "wom_mode_x_thres_boot"};
static const std::vector<std::string> wom_y_thres_sys = {"", "", "wom_y_thres_boot", "wom_mode_y_thres_boot", "wom_mode_y_thres_boot"};
static const std::vector<std::string> wom_z_thres_sys = {"", "", "wom_z_thres_boot", "wom_mode_z_thres_boot", "wom_mode_z_thres_boot"};

constexpr int MIN_MISC_WAKEUP_ENABLE_ADAPTIVE_WOM = 1;
constexpr int MAX_MISC_WAKEUP_COUNT = 5;

// Default thresholds not decided need to be finalized based on data analysis
#define DEF_IMU_ACCEL_RMS_ON_THRESHOLD 0.03
#define DEF_IMU_ACCEL_RMS_OFF_THRESHOLD 0.01
#define DEF_IMU_GYRO_RMS_ON_THRESHOLD 0.25
#define DEF_IMU_GYRO_RMS_OFF_THRESHOLD 0.20
#define DEF_IMU_MOTION_SCORE_ON_THRESHOLD 1.0
#define DEF_IMU_MOTION_SCORE_OFF_THRESHOLD 1.0
#define DEF_IMU_MOTION_SCORE_ALPHA 0.6
#define DEF_MOVING_CONFIDENCE_FACTOR 200.0

// Min-Max VALUES for above thresholds
#define MIN_IMU_ENG_STAT_THRESHOLD 0.0001
#define MAX_IMU_ENG_STAT_THRESHOLD 16.0000

// Ratio to calculate off threshold using on
#define DEF_OFF_ON_IMU_RMS_RATIO (DEF_IMU_ACCEL_RMS_OFF_THRESHOLD/DEF_IMU_ACCEL_RMS_ON_THRESHOLD)

struct eng_stat_thresholds{
	float imu_accel_rms_on_threshold = DEF_IMU_ACCEL_RMS_ON_THRESHOLD;
	float imu_accel_rms_off_threshold = DEF_IMU_ACCEL_RMS_OFF_THRESHOLD;
	float imu_gyro_rms_on_threshold = DEF_IMU_GYRO_RMS_ON_THRESHOLD;
	float imu_gyro_rms_off_threshold = DEF_IMU_GYRO_RMS_OFF_THRESHOLD;
	float imu_motion_score_on_threshold = DEF_IMU_MOTION_SCORE_ON_THRESHOLD;
	float imu_motion_score_off_threshold = DEF_IMU_MOTION_SCORE_OFF_THRESHOLD;
	float imu_motion_score_alpha = DEF_IMU_MOTION_SCORE_ALPHA;
	float imu_rms_percentage_threshold = DEF_MOVING_CONFIDENCE_FACTOR;
};

enum wom_axis_t {
    eWOM_X_AXIS = 0,
    eWOM_Y_AXIS,
    eWOM_Z_AXIS,
    eWOM_AXIS_MAX
};

enum wom_event_type_t {
	eWOMPersistEvent = 0,
	eWOMIncreaseEvent,
	eWOMDecreaseEvent,
	eWOMCorrectionEvent,
	eWOMEventMax
};

constexpr int D450_MOVING_AXIS_INC_DEC = 1;
constexpr int D450_DOOR_AXIS_INC_DEC = 1;
constexpr int D450_FLOOR_AXIS_INC_DEC = 2;

constexpr int D2XX_MOVING_AXIS_INC_DEC = 5;
constexpr int D2XX_DOOR_AXIS_INC_DEC = 5;
constexpr int D2XX_FLOOR_AXIS_INC_DEC = 1;

constexpr int DEF_ADAPT_INC_DEC = 0;

static const std::vector<std::vector<int>> adaptive_inc_dec_map = {
	//TODO: Map it to the moving, door, up-down axis. Analytics = (verti,door,moving)
	{DEF_ADAPT_INC_DEC, DEF_ADAPT_INC_DEC, DEF_ADAPT_INC_DEC}, 					 // 0: bagheera
	{DEF_ADAPT_INC_DEC, DEF_ADAPT_INC_DEC, DEF_ADAPT_INC_DEC}, 					 // 1: bagheera2
	{D450_MOVING_AXIS_INC_DEC, D450_DOOR_AXIS_INC_DEC, D450_FLOOR_AXIS_INC_DEC}, // 2: bagheera3 (moving, door, vertical)
	{D2XX_DOOR_AXIS_INC_DEC, D2XX_MOVING_AXIS_INC_DEC, D2XX_FLOOR_AXIS_INC_DEC}, // 3: krait (door, moving, vertical)
	{D2XX_DOOR_AXIS_INC_DEC, D2XX_FLOOR_AXIS_INC_DEC, D2XX_MOVING_AXIS_INC_DEC}  // 4: krait2 (door, vertical, moving)
};

//For SM_E_APM_EVENT_STATUS
enum apm_aux_code_t {
	// Bits (0-15) for ignition source status
	eIGN_GLITCH_AUX_CODE = ( 1 << 16 ),
	eIMU_GLITCH_AUX_CODE = ( 1 << 17 ),
	eGPS_GLITCH_AUX_CODE = ( 1 << 18 ),
	eCAN_GLITCH_AUX_CODE = ( 1 << 19 ),
	ePWR_VOLT_GLITCH_AUX_CODE = ( 1 << 20 )
};

/*
 *https://gis.stackexchange.com/questions/8650/measuring-accuracy-of-latitude-and-longitude
 *decimal
 places  degrees      N/S or E/W     E/W at         E/W at       E/W at
 at equator     lat=23N/S      lat=45N/S    lat=67N/S
 ------- -------      ----------     ----------     ---------    ---------
 0       1            111.32 km      102.47 km      78.71 km     43.496 km
 1       0.1          11.132 km      10.247 km      7.871 km     4.3496 km
 2       0.01         1.1132 km      1.0247 km      787.1 m      434.96 m
 3       0.001        111.32 m       102.47 m       78.71 m      43.496 m
 4       0.0001       11.132 m       10.247 m       7.871 m      4.3496 m
 5       0.00001      1.1132 m       1.0247 m       787.1 mm     434.96 mm
 6       0.000001     11.132 cm      102.47 mm      78.71 mm     43.496 mm
 7       0.0000001    1.1132 cm      10.247 mm      7.871 mm     4.3496 mm
 8       0.00000001   1.1132 mm      1.0247 mm      0.7871mm     0.43496mm

 As an approximation, the length in km of one degree of longitude is cos(latitude in DD ) * 111.321 km,
 where 111.321 is the length of a degree of longitude at the equator and pi/180 converts decimal degrees to radians.
 * */

static const double DEF_LAT_THRESHOLD  = -1; // setting default latitude threshold to -1, so that g_vehicle_state will be updated only based on valid speed.
static const double DEF_LONG_THRESHOLD = -1; // setting default longitude threshold to -1, so that g_vehicle_state will be updated only based on valid speed.
static const double MIN_GPS_SPEED_THRESHOLD = 0.0;
static const double MIN_GPS_LAT_THRESHOLD = DEF_LAT_THRESHOLD;
static const double MIN_GPS_LONG_THRESHOLD = DEF_LONG_THRESHOLD;

static const float DEF_SPEED_THRESHOLD = 5.0;  // 5.0 mph (2.23694 m/s)
static const float DEF_SPEED_ENGINE_OFF_THRESHOLD = 2.0;  // 2.0 mph (0.89408 m/s)
constexpr float DEF_SPEED_FUSION_THRESHOLD = 10.0; // 10.0 mph

static const float DEF_LAT_INVALID = -91.0000; // Invalid Latitude Value
static const float DEF_LONG_INVALID = -181.0000; // Invalid Longitude Value

static bool DEF_APM_MOTION_DET = false;
static bool DEF_APM_IMU = true;
static bool DEF_APM_WOM = true;
static bool DEF_APM_GPS = true;
static bool DEF_APM_CAN = true;
static bool DEF_APM_POWER_VOLT_ENABLE = true;
// static bool DEF_APM_IGN_VOLT_ENABLE = false;
static bool DEF_APM_SUPERCAP = true;
static const bool DEF_APM_IGNS = true;
static const bool DEF_ENABLE_IGNITION_BASED_WAKEUP = true;
static const bool DEF_GLITCH_SUPPRESSION_SUPPORT = true;
static const bool DEF_GLITCH_SUPPRESSION_SUPPORT_VAL = true;
static const int DEF_OBD_RPM_THRESHOLD = 350; // 350 RPM TODO: Need Check With CAN Team

static const int IGNS_MASK_POS = 0;
static const int IMU_MASK_POS = 1;
static const int SC_MASK_POS = 2;
static const int GPS_MASK_POS = 3;
static const int CAN_MASK_POS = 4;
static const int POWER_VOLT_MASK_POS = 5;
static const int IGN_VOLT_MASK_POS = 6;

static const int SVC_SEND_KEEP_ALIVE_TIME = 30; //in seconds

static const float MIN_GPS_ACCURACY = 10.0;

constexpr float DEF_SUPERCAP_UPPER_THRESHOLD = 10.25; // SuperCap upper threshold, not being used as supercap is triggered only at lower threshold
constexpr float DEF_SUPERCAP_LOWER_THRESHOLD = 9.75; // SuperCap lower threshold

constexpr float DEF_SUPERCAP_UPPER_THRESHOLD_24V = 20.51; // SuperCap upper threshold for 24V system, not being used as supercap is triggered only at lower threshold
constexpr float DEF_SUPERCAP_LOWER_THRESHOLD_24V = 19.51; // SuperCap lower threshold for 24V battery system

constexpr float MAX_SUPERCAP_UPPER_THRESHOLD = MAX_VALID_VOLTAGE;
constexpr float MIN_SUPERCAP_LOWER_THRESHOLD = MIN_POWER_VOLTAGE_THRESHOLD;

constexpr float MAX_SUPERCAP_UPPER_THRESHOLD_24V = MAX_VALID_VOLTAGE;
constexpr float MIN_SUPERCAP_LOWER_THRESHOLD_24V = MIN_POWER_VOLTAGE_THRESHOLD_24V;

constexpr uint GET_CURR_DATA = 0xFFFF;//Latest_Data.
constexpr uint DEFAULT_INTERVAL_2_SEC = 2; // 2 seconds buffer size for workers which do not have configuration
constexpr uint64_t DATA_VALIDITY_THRESHOLD = 10; // Number of consecutive invalid data to consider data as invalid
void get_config();

constexpr uint64_t LOGGING_INTERVAL_SEC = 200; // seconds

const std::string vehicle_attributes = ND_ATTR_PATH + std::string("apm/vehicle_info");
constexpr const char* TRUE_STR = "true";
constexpr const char* FALSE_STR = "false";
constexpr const char* USER_24V_STR = "user.V_24";
constexpr const char* USER_12V_STR = "user.V_12";
constexpr const char* USER_IGNITION_EVENT_STR = "user.ign_event";
constexpr const char* USER_EASY_INSTALL_STR = "user.easy_install";
constexpr const char* USER_LAST_IGN_EVENT_TIME_STR = "user.last_ign_event_time";

const std::string vehicle_ign_state = ND_ATTR_PATH + std::string("apm/vehicle_ign_state");
constexpr const char* USER_IGN = "user.gpio_ign";
constexpr const char* USER_PWR = "user.power_volt";
constexpr const char* USER_IMU = "user.imu";
constexpr const char* USER_GPS = "user.gps";
constexpr const char* USER_ENG_STAT = "user.eng_stat";
constexpr const char* USER_CAN = "user.can";
constexpr const char* USER_PFI = "user.pfi";
constexpr const char* USER_TIME = "user.time";
constexpr const char* MOVING_STR = "MOVING";
constexpr const char* STATIONARY_STR = "STATIONARY";

constexpr uint64_t ONE_DAY_SECONDS = 24 * 60 * 60; // Seconds in one day
constexpr uint WEEK_DAYS = 7;

constexpr int EASY_INSTALL_ENABLE = 1;
constexpr int EASY_INSTALL_DISABLE = 0;

enum attr_file_status_t {
	eATTR_FILE_INVALID = 0,
	eATTR_FILE_CREATED,
	eATTR_FILE_PRESENT,
	eATTR_FAIL_TO_CREATE,
	eATTR_STATUS_MAX
};

enum motion_status_t {
	STATIONARY = 0,  // Time to Write pseudo igntion 0
	TRANSITION,      // Moved from MOVING state where we wait for some time before going to STATIONARY
	MOVING,          // Atleast one worker is saying, we are moving
	INVALID, // Invalid, Lower the priority and take decision
	MOTION_STATUS_MAX
};

/*----function Pointer typedef--------*/
typedef void* (poll_func_t)(void);
typedef void* (intr_func_t)(void);
typedef void (filter_func_t)(void);
typedef motion_status_t (read_status_t)(void);
/*------------------------------------*/

enum IgnitonSource {
	ePowerFail = 0,
	eCrankLine,//Physical IGN Line GPIO
	eCrankVolt,//Power Line Voltage/Battery Voltage
	eInertial,//IMU Sensor
	eSatellite,//GNSS(GPS/GLONASS/BEIDOU/GALILEO/QZSS/IRNSS
	eCANBus,   //OBDx/J1939 and Other Vehicle CAN Bus protocols
	eEngineStatus, // Engine Status based on combination of multiple signals and thresholds
	eIgnSrcMax	   //
};

constexpr int DEF_DECISOIN_WINDOW_MIN = 1;
constexpr int DEF_DECISOIN_WINDOW_MAX = 200;

// Default Motion Sensor Windows incase motion detection is disabled.
constexpr int DEF_LONG_WINDOW_MOTION_DETECTION_DISABLED = 10; // 10 seconds
constexpr int DEF_OUTAGE_WINDOW_MOTION_DETECTION_DISABLED = 30; // 30 seconds

enum DecisionWindow {
	eDecisionWindow_Short = 0, // ON Soak
	eDecisionWindow_Long,	   // OFF Soak
	eDecisionWindow_Outage,	   // OUTAGE Soak
	eDecisionWindow_Valid,	   // VALID/DEBOUNCE Soak
	eDecisionWindow_Sleep,     // SLEEP Interval
	eDecisionWindow_Median,    // HYSTERISIS/CONFIDENCE Soak
	eDecisionWindow_Continuous,// PERSISTENCE Soak
	eDecisionWindow_Cache,     // DATA CACHE Size
	eDecisionWindow_Max
};

enum APM_Decision {
	eAPM_Decision_Engine_Off = 0,
	eAPM_Decision_Moving = 1,
	eAPM_Decision_Idling = 2,
	eAPM_Decision_Debounce = eAPM_Decision_Idling, // Debounce used for Interrupts(Ex IGN or PFI or Button) and Idling for motion sources(Ex IMU, GPS, VOLT, CAN, etc)
	eAPM_Decision_Fusion_Engine_Off = 3,
	eAPM_Decision_Fusion_Moving = 4, // For Moving Decision from Fusion
	eAPM_Decision_Fusion_Idling = 5, // For Idling Decision from Fusion
	eAPM_Decision_Max
};

// To store voltage related configurations
struct sensor_config_t {

	// 12/24V thresholds
    float eng_off_crank_v_th;
    float eng_on_crank_v_th;
    float idle_crank_v_th;
    float eng_off_crank_v_th_24v;
    float eng_on_crank_v_th_24v;
    float idle_crank_v_th_24v;

	// min voltage limits
    float min_v_lim_12v;
    float min_v_lim_24v;

	// sc thresholds
    float pfi_off_crank_v_th;
    float pfi_on_crank_v_th;
    float pfi_off_crank_v_th_24v;
    float pfi_on_crank_v_th_24v;

	// fusion thresholds for 12/24V
	float fusion_eng_off_crank_v_th;
	float fusion_eng_on_crank_v_th;
	float fusion_idle_crank_v_th;
	float fusion_eng_off_crank_v_th_24v;
	float fusion_eng_on_crank_v_th_24v;
	float fusion_idle_crank_v_th_24v;

	bool pwr_volt_fusion_enable;

	// CAN Thresholds
	float can_rpm_threshold;
	float can_rpm_engine_off_threshold;
	float can_speed_threshold;

};

// Forward declaration
class APM_worker;

// Including apm_worker.h here
// To avoid circular dependency issues
#include "apm_worker.h"

class APM {
	public:

		~APM() {}

		static APM* Instance() {
			if(!apm_context) {
				apm_context = new APM();
				assert(apm_context != NULL);
				// pWorkers array is already initialized to NULL in apm.cpp
			}
			return apm_context;
		}

		static APM_worker* WorkerInstance(IgnitonSource ignSrc) {

			if( (ignSrc >= ePowerFail) && (ignSrc < eIgnSrcMax) )
				return pWorkers[ignSrc];

			return NULL;
		}

		int apm_register(char*, APM_worker *, IgnitonSource ignSrc);
		void apm_unregister_all();
		void start_monitor();
		//wom thresholds
		void get_wom_thresholds(unsigned int& wom_x_thres, unsigned int& wom_y_thres, unsigned int& wom_z_thres);
		void set_wom_thresholds(unsigned int wom_x_thres, unsigned int wom_y_thres, unsigned int wom_z_thres);
		//vehicle class
		VehicleClass get_veh_class();
		void set_veh_class(VehicleClass& vehi_class);

		string          apm_q_name = "q_apm";
		nd_msgq_t       *apm_msg_q;

	protected:
	private:
		unsigned int wom_x_threshold = DEF_IMU_ACCEL_WOM_X_THR;
		unsigned int wom_y_threshold = DEF_IMU_ACCEL_WOM_Y_THR;
		unsigned int wom_z_threshold = DEF_IMU_ACCEL_WOM_Z_THR;
		VehicleClass veh_class = eVehicleUnknown;

	static APM_worker*  pWorkers[eIgnSrcMax];
	static APM* apm_context;

};

#endif
