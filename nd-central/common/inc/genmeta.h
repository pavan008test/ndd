/* Copyright (C) 2016 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Y Suresh Kumar <suresh.kumar@netradyne.com>, October 2016
 */


#ifndef GENMETA_H
#define GENMETA_H

#include <vector>
#include <string>
#include <iostream>

#include <component.h>
#include <error.h>

#include "nd_msg_types.h"

#include <gps.h>
#include <imu.h>
#include <obd.h>
#include <log.h>
#include <frameinfo.h>
#include <ublox.h>

using namespace std;

#define	MAX_TIME_LEN		128
#define	MAX_ID_LEN 		 16
#define	MAX_FILE_NAME_LEN	256
#define	MAX_VER_LEN		 16
#define MAX_BUTTONS 5 // buttons on driver-i + ble device buttons (3)
#define MAX_USER_ALERT_SOURCE_LEN 32

#ifdef BAGHEERA2
#define	PARTIAL_OBD_DATA         3
#elif KRAIT
#define	PARTIAL_OBD_DATA         5
#endif

typedef struct genmeta_header {
	string time;
	float volts;
	string device_id;
    int cams_enabled;
    string protocol_info;
    string vin;
    string can_firmware_ver;
	string driver_id;
	string driver_id_v2;
	string session_id;
	string privacy_mode;
	string privacy_mode_offduty;
	string privacy_mode_geofence;
	string idle_status;
	string processing_mode;
	string ignition_status;
	string app_ver;
	string metadata_ver;
	int64_t system_uptime;
	int64_t service_uptime;
	double vertical_angle;
	double horizontal_angle;
	string video_name;
	string prevVideoName = "";
	string nextVideoName = "";
	string vehicle_id;
	string speed_unit;
	int trip_no;
	int part_no;
	int64_t start_time;
	int64_t start_time_ld;
	int64_t start_time_dp;
	int64_t start_time_micro;
	int64_t start_time_raw_micro;
	int64_t offset;
	int64_t gps_start_time;
	int64_t gps_end_time;
	int64_t end_time;
	int64_t end_time_micro;
	int64_t end_time_raw_micro;

	int     lpw_no_record;

	string devicetype;
	string devicesubtype;
	string vehclass;
	string network_info;
	int audio_enable;

	int64_t inward_start_time;
	int64_t inward_start_time_ld;
	int64_t ptsStartTime;
	int64_t ptsStartTime_ld;
	int64_t ptsStartTime_dp;
	int64_t inward_ptsStartTime;
	int64_t inward_ptsStartTime_ld;
	int64_t dms_start_time;
	int64_t dms_ptsStartTime;
	int64_t dms_start_time_ld;
	int64_t dms_ptsStartTime_ld;
	string driverInvariantSession;
	string disMode;
	string privacyModeEvents;
	string udid_string;
	string sessionCount_string;
	string rtcValid_string;
	string rtc_jump_from_string;
	string rtc_jump_to_string;
	string can_sn;
	string can_model;
	int can_status;
	int can_src;
	string engine_status;
	string copy_status_cam;
	int outward_cam_privacy;
	int inward_cam_privacy;
	int irled_status;
	string irled_states;

}genmeta_header_t;

typedef struct genmeta_imudata
{
    Imu::imu_sensor_t type;
    Imu::val_t imuData;
}genmeta_imudata_t;

typedef struct genmeta_usralert
{
    int64_t time;
    int     btn;
	char source[MAX_USER_ALERT_SOURCE_LEN];
}genmeta_usralert_t;

typedef struct genmeta_highgalert
{
    int64_t time;
}genmeta_highgalert_t;

typedef struct
{
    bool   status;
    int64_t time;
}ignition_status_records_t;

struct genmeta_ctx;

const std::string usralert_map[32] = {
    "99.0.0", // surface button 0
    "99.0.1", // surface button 1
    "99.1.0", // driver ble button
    "99.1.1", // passenger ble button
    "99.1.2"  // trailer ble button
};

class Genmeta: public Component {

public:
	/** Inherited**/
	bool register_alive_callback(component_alive_cb_t *cb , void *app);

	bool register_error_callback(component_error_cb_t *cb , void *app);

	vector< pair< string, string > > get_capabilities();

	vector< pair< string, string > > dump_stats();

	bool configure(vector< pair< string, string > > &config);

	bool configure(string key, string value);

	string get_config(string key);

	/** Genmeta related functions */

	/**
	 * @brief make the ctx ready to collect data
	 *
	 * @param header :: contains the header info
	 * @return 'true' for success, 'false' otherwise
	 */
	bool start_data_collection(genmeta_header_t &header, int flipflop); // for all sensoer; call from central

	/**
	 * @brief
	 *
	 * @param keyvalue :: contains the kevalue pair seperated by :
	 * @return 'true' for success, 'false' otherwise
	 */
	bool update_genmeta_header(string key, string value, int flipflop);

	/**
	 * @brief push IGNITION data
	 *
	 * @param ignition_data :: contains the ignition_data for one observation
	 * @return 'true' for success, 'false' otherwise
	 */
	bool push_data_ignition(ignition_status_records_t ignition_data, int flipflop);

	/**
	 * @brief push GPS data
	 *
	 * @param gps_data :: contains the gps_data for one observation
	 * @return 'true' for success, 'false' otherwise
	 */
	bool push_data(Gps::gps_metadata_t gps_data, int flipflop); // separate call per sensor

	bool push_data(Gps::gps_pps_data_t gps_pps_data, int flipflop); // separate call per sensor
	bool push_data(Ublox::ublox_data_t ublox_data, int flipflop);

	/**
	 * @brief push UBX-MON-GNSS data
	 *
	 * @param ublox_constell_data :: contains the ublox_constell_data for one observation
	 * @return 'true' for success, 'false' otherwise
	 */
        bool push_data(Ublox::ublox_constellation_data_t ublox_constell_data, int flipflop);

	/**
	 * @brief get size of GPS data
	 *
	 * @return size of gps_data_collection
	 */
	int get_gps_data_size(int flipflop);

	/**
	 * @brief push IMU data
	 *
	 * @param gps_data :: contains the imu data for one observation
	 * @return 'true' for success, 'false' otherwise
	 */
	bool push_data(Imu::imu_sensor_t imu, Imu::Imu::val_t imu_data_collection, int flipflop);

	/**
	 * @brief get size of IMU data
	 *
	 * @return size of imu_data_collection
	 */
	int get_imu_data_size(int flipflop);

	bool push_data(FrameInfo::frameinfo_t frame_data, int flipflop);

	/**
	 * @brief push User alert data
	 *
	 * @param alert :: contains the alert data
	 * @return 'true' for success, 'false' otherwise
	 */
	bool push_data(genmeta_usralert_t alert, int flipflop);

	/**
	 * @brief get size of user alert
	 *
	 * @return size of usr_alert_collection
	 */
	int get_usr_alert_size(int flipflop);

	bool push_data(genmeta_highgalert_t alert, int flipflop);

	bool push_data(Obd::obd_data_t obd_data, int flipflop);
	bool push_data(Obd::fr_data_t freport_data, int flipflop);
	bool push_data(Obd::ir_data_t ireport_data, int flipflop);

	/**
	 * @brief get size of OBD data
	 *
	 * @return size of obd_data_collection
	 */
	int get_obd_data_size(int flipflop);

	/**
	 * @brief make ctx to stop data collection, push data wont work after this call
	 *
	 * @param header :: contains the header info
	 * @return 'true' for success, 'false' otherwise
	 */
	bool stop_data_collection(int flipflop);// for all sensoer; call from central

	/**
	 * @brief convert the data accumulated so far (header, GPS, IMU ) and dump in a string
	 *
	 * @param header :: contains the header info
	 * @return pointer to a string in case of failure string will point to NULL
	 */
	bool get_meta_json(string &json_string, int flipflop, uint64_t endTime, bool hdmaps_mode_enabled, bool imu_data); // for all sensor; call from central

	Genmeta( string name );
	~Genmeta();
private:
	/**
	 * @brief clear the data accumulated obd, imu, usr_alert, gps
	 */
	void clear_data(int flipflop); // for all sensoer; call from central

	/**
	 * @brief convert header, GPS, IMU into Jsons and combine them
	 *
	 * @param header :: contains the header info
	 * @return pointer to a string, in case of failure string will point to NULL
	 */
	bool convert_all_json(genmeta_header_t header, string &json_string, int flipflop, uint64_t endTime, bool hdmaps_mode_enabled, bool imu_data);

	/**
	 * @brief convert header structure into Json
	 *
	 * @param header :: contains the header info
	 * @return pointer to a string, in case of failure string will point to NULL
	 */
	bool convert_header_json(genmeta_header_t header, string &header_string);

	/**
	 * @brief convert GPS structure array into Json
	 *
	 * @param header :: contains the header info
	 * @return pointer to a string, in case of failure string will point to NULL
	 */
	bool convert_gpsarray_json(string &gps_string, int flipflop);

	bool convert_ppsarray_json(string &pps_string, int flipflop);
	bool convert_ubloxarray_json(string &ublox_string, int flipflop);
	bool convert_ublox_constellarray_json(string &ublox_constell_string, int);
	bool convert_gps_pps_start_index_json (string &GPS_to_PPS_startidx, string &PPS_startidx, int flipflop);

	/**
	 * @brief convert IMU structure array into Json
	 *
	 * @param header :: contains the header info
	 * @return pointer to a string, in case of failure string will point to NULL
	 */
	bool convert_imuarray_json(string &imu_data_str, string &sensor_metadata_str, string &imu_temperature, int flipflop);

	bool convert_obdarray_json(string &obd_idms_str, string &obd_hdmaps_str, int flipflop);
	bool convert_fuel_report_json(string &fuel_report_string, int flipflop);
	bool convert_idling_report_json(string &idling_report_string, int flipflop);

	bool convert_frameinfoarray_json(string &frameinfo_string, int flipflop);

	/**
	 * @brief convert User alert structure array into Json
	 *
	 * @param header :: contains the header info
	 * @return pointer to a string, in case of failure string will point to NULL
	 */
	bool convert_usralertarray_json(string &usralert_string, int flipflop, uint64_t end_time);

	bool convert_highgalertarray_json(string &highgalert_string, int flipflop, uint64_t end_time);
	bool convert_ignition_status_json(string &ignition_status_string, int flipflop, uint64_t end_time);
	bool convert_ignition_on_duration_json(string &ignition_on_duration_string, int flipflop);

	genmeta_ctx* ctx;
};

////

#endif
