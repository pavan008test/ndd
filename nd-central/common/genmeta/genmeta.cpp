
/* Copyright (C) 2016 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Y Suresh Kumar <suresh.kumar@netradyne.com>, October 2016
 */

#include <sstream>
#include <fstream>
#include <cstring>

#include <inttypes.h>

#include <genmeta.h>
#include <system_utils.h>
#include <../central/nd_central.h>
#include <../central/device_mode.h>
#include <nd_time.h>
#include <bitset>

#include <iomanip>
#include <jansson/jansson.h>
#include <map>
#include <algorithm>
#include <config_parser.h>

using namespace std;
static const char *TAG="GMETA";
extern int fuel_report;
extern int idling_report;
extern int valid_GPS_entries;
extern can_status_t can_status;
extern can_status_t can_status_updated;
extern nd_central_ctx ctx;
extern char configured_obd_vin[MAX_SIZE_OF_VIN];
#define BAGHEERACONFIG_INI "/home/ubuntu/.nddevice/latest/bagheera_config.ini"

enum    genmeta_state_t {
	INIT_STATE,
	START_STATE,
	STOP_STATE,

	UNKNOWN_STATE
};

struct genmeta_ctx {
    genmeta_header_t genmetaHeader[2];
    vector<Gps::gps_metadata_t> gps_data_collection[2]; // init for all sensors
    vector<Gps::gps_pps_data_t> gps_pps_data_collection[2];
    vector<Ublox::ublox_data_t> ublox_data_collection[2];
    Ublox::ublox_constellation_data_t ublox_constell_data_collection;
    vector<genmeta_imudata_t> imu_data_collection[2];
    vector<genmeta_imudata_t> imu_accel_data_collection[2];
    vector<genmeta_imudata_t> imu_gyro_data_collection[2];
    vector<genmeta_imudata_t> imu_magneto_data_collection[2];
    vector<genmeta_imudata_t> imu_temperature_data_collection[2];
    vector<genmeta_usralert_t> usr_alert_collection[2];
    vector<genmeta_highgalert_t> highg_alert_collection[2];
    vector<Obd::obd_data_t> obddata_collection[2];
    vector<Obd::fr_data_t> fuel_report_collection[2];
    vector<Obd::ir_data_t> idling_report_collection[2];
    vector<ignition_status_records_t> ignition_status_collection[2];
    vector<FrameInfo::frameinfo_t> frame_data_collection[2];
    map <string, vector<Obd::obd_data_t>> obd_map;
    int max_ignition_status = 10;

    void set_state( genmeta_state_t state ) {
        LOG_I(TAG, "state change: %d", state);
        this->state = state;
    }

    genmeta_state_t get_state() {
        return state;
    }

private:
    genmeta_state_t state;
};

//// constructor & destructor
Genmeta::Genmeta( string name ) : Component(name) {
    this->ctx = new genmeta_ctx;
    this->ctx->set_state( INIT_STATE );
    Config_parser c(BAGHEERACONFIG_INI);
    if (c.getParseStatus() != true) {
        ctx->max_ignition_status = 10 ;
    } else {
        string temp;
        bool get_override_val = true;
        bool is_val_overridden = false;
        temp = c.getConfig("power","max_ignition","false", get_override_val, is_val_overridden);
        if(string_to_integer(temp, ctx->max_ignition_status) == false) {
            LOG_E(TAG, "failed to max_ignition_status from config");
            ctx->max_ignition_status = 10;
        }
    }
    LOG_I(TAG, "max_ignition_status after reading config: %d", ctx->max_ignition_status);
}

Genmeta::~Genmeta() {
	delete this->ctx;
}

/** Inherited**/
bool Genmeta::register_alive_callback(component_alive_cb_t *cb , void *app) {
}

bool Genmeta::register_error_callback(component_error_cb_t *cb , void *app) {
}

vector< pair< string, string > > Genmeta::get_capabilities() {
}

vector< pair< string, string > > Genmeta::dump_stats() {
}

bool Genmeta::configure(vector< pair< string, string > > &config) {
}

bool Genmeta::configure(string key, string value) {
}

string Genmeta::get_config(string key) {
}


//// Class related Functions

bool Genmeta::start_data_collection(genmeta_header_t &header, int flipflop) // for all sensoer; call from central
{
	this->ctx->genmetaHeader[flipflop] = header;

	LOG_I(TAG, "Success in start_data_collection");
	return true;
}

bool Genmeta::update_genmeta_header(string key, string value, int flipflop)
{
    string temp_str1 = key;
    string temp_str2 = value;

    if( temp_str1.compare("start_time") == 0 )
    {
        int64_t temp_time;
        stringstream ss(temp_str2);
        ss >> temp_time;
        this->ctx->genmetaHeader[flipflop].start_time = temp_time;
        LOG_I(TAG, "start_time :::%lld:::", this->ctx->genmetaHeader[flipflop].start_time);
        return true;
    }

    if( temp_str1.compare("start_time_ld") == 0 )
    {
        int64_t temp_time;
        stringstream ss(temp_str2);
        ss >> temp_time;
        this->ctx->genmetaHeader[flipflop].start_time_ld = temp_time;
        LOG_I(TAG, "start_time_ld :::%lld:::", this->ctx->genmetaHeader[flipflop].start_time_ld);
        return true;
    }

	if( temp_str1.compare("start_time_dp") == 0 )
	{
		int64_t temp_time;
		stringstream ss(temp_str2);
		ss >> temp_time;
		this->ctx->genmetaHeader[flipflop].start_time_dp = temp_time;
		LOG_I(TAG, "start_time_dp :::%lld:::", this->ctx->genmetaHeader[flipflop].start_time_dp);
		return true;
	}

    if( temp_str1.compare("start_time_micro") == 0 )
    {
        int64_t temp_time;
        stringstream ss(temp_str2);
        ss >> temp_time;
        this->ctx->genmetaHeader[flipflop].start_time_micro = temp_time;
        LOG_I(TAG, "start_time_micro :::%lld:::", this->ctx->genmetaHeader[flipflop].start_time_micro);
        return true;
    }

    if( temp_str1.compare("end_time") == 0 )
    {
        int64_t temp_time;
        stringstream ss(temp_str2);
        ss >> temp_time;
        this->ctx->genmetaHeader[flipflop].end_time = temp_time;
        LOG_I(TAG, "end_time :::%lld:::", this->ctx->genmetaHeader[flipflop].end_time);
        return true;
    }

    if( temp_str1.compare("end_time_micro") == 0 )
    {
        int64_t temp_time;
        stringstream ss(temp_str2);
        ss >> temp_time;
        this->ctx->genmetaHeader[flipflop].end_time_micro = temp_time;
        LOG_I(TAG, "end_time_micro :::%lld:::", this->ctx->genmetaHeader[flipflop].end_time_micro);
        return true;
    }

    if( temp_str1.compare("end_time_raw_micro") == 0 )
    {
        int64_t temp_time;
        stringstream ss(temp_str2);
        ss >> temp_time;
        this->ctx->genmetaHeader[flipflop].end_time_raw_micro = temp_time;
        LOG_I(TAG, "end_time_raw_micro :::%lld:::", this->ctx->genmetaHeader[flipflop].end_time_raw_micro);
        return true;
    }

    if( temp_str1.compare("gps_start_time") == 0 )
    {
        int64_t temp_time;
        stringstream ss(temp_str2);
        ss >> temp_time;
        this->ctx->genmetaHeader[flipflop].gps_start_time = temp_time;
        LOG_I(TAG, "gps_start_time :::%lld:::", this->ctx->genmetaHeader[flipflop].gps_start_time);
        return true;
    }

    if( temp_str1.compare("gps_end_time") == 0 )
    {
        int64_t temp_time;
        stringstream ss(temp_str2);
        ss >> temp_time;
        this->ctx->genmetaHeader[flipflop].gps_end_time = temp_time;
        LOG_I(TAG, "gps_end_time :::%lld:::", this->ctx->genmetaHeader[flipflop].gps_end_time);
        return true;
    }

    if( temp_str1.compare("offset") == 0 )
    {
        int64_t temp_time;
        stringstream ss(temp_str2);
        ss >> temp_time;
        this->ctx->genmetaHeader[flipflop].offset = temp_time;
        LOG_I(TAG, "offset :::%lld:::", this->ctx->genmetaHeader[flipflop].offset);
        return true;
    }

    if( temp_str1.compare("drvId") == 0 )
    {
        this->ctx->genmetaHeader[flipflop].driver_id = temp_str2;
        LOG_I(TAG,"driver id:::%s:::",this->ctx->genmetaHeader[flipflop].driver_id.c_str());
        return true;
    }

    if( temp_str1.compare("drvId_v2") == 0 )
    {
        this->ctx->genmetaHeader[flipflop].driver_id_v2 = temp_str2;
        LOG_I(TAG,"driver id v2:::%s:::",this->ctx->genmetaHeader[flipflop].driver_id_v2.c_str());
        return true;
    }

    if( temp_str1.compare("nextVideoName") == 0 )
    {
        this->ctx->genmetaHeader[flipflop].nextVideoName = temp_str2;
        LOG_I(TAG,"nextVideoName:::%s:::",this->ctx->genmetaHeader[flipflop].nextVideoName.c_str());
        return true;
    }

    if( temp_str1.compare("prevVideoName") == 0 )
    {
        this->ctx->genmetaHeader[flipflop].prevVideoName = temp_str2;
        LOG_I(TAG,"prevVideoName:::%s:::",this->ctx->genmetaHeader[flipflop].prevVideoName.c_str());
        return true;
    }

    if( temp_str1.compare("privacyMode") == 0 )
    {
        this->ctx->genmetaHeader[flipflop].privacy_mode = temp_str2;
        LOG_I(TAG,"privacy mode:::%s:::",this->ctx->genmetaHeader[flipflop].privacy_mode.c_str());
        return true;
    }

    if( temp_str1.compare("privacyModeOffduty") == 0 )
    {
        this->ctx->genmetaHeader[flipflop].privacy_mode_offduty = temp_str2;
        LOG_I(TAG,"privacy mode offduty:::%s:::",this->ctx->genmetaHeader[flipflop].privacy_mode_offduty.c_str());
        return true;
    }

    if( temp_str1.compare("privacyModeGeofence") == 0 )
    {
        this->ctx->genmetaHeader[flipflop].privacy_mode_geofence = temp_str2;
        LOG_I(TAG,"privacy mode geofence:::%s:::",this->ctx->genmetaHeader[flipflop].privacy_mode_geofence.c_str());
        return true;
    }

    if( temp_str1.compare("idleStatus") == 0 )
    {
        this->ctx->genmetaHeader[flipflop].idle_status = temp_str2;
        LOG_I(TAG,"idle status:::%s:::",this->ctx->genmetaHeader[flipflop].idle_status.c_str());
        return true;
    }

    if( temp_str1.compare("processingMode") == 0 )
    {
        this->ctx->genmetaHeader[flipflop].processing_mode = temp_str2;
        LOG_I(TAG,"processing mode:::%s:::",this->ctx->genmetaHeader[flipflop].processing_mode.c_str());
        return true;
    }

    if (temp_str1.compare("lpw_no_record") == 0)
    {
        int temp_lpw_no_record;
        stringstream ss(temp_str2);
        ss >> temp_lpw_no_record;
        this->ctx->genmetaHeader[flipflop].lpw_no_record = temp_lpw_no_record;
        LOG_I(TAG,"lpw_no_record:::%d:::",this->ctx->genmetaHeader[flipflop].lpw_no_record);
        return true;
    }

    if( temp_str1.compare("ignitionStatus") == 0 )
    {
        this->ctx->genmetaHeader[flipflop].ignition_status = temp_str2;
        LOG_I(TAG,"ignition status:::%s:::",this->ctx->genmetaHeader[flipflop].ignition_status.c_str());
        return true;
    }

    if( temp_str1.compare("protocol_info") == 0 )
    {
        this->ctx->genmetaHeader[flipflop].protocol_info = temp_str2;
        LOG_I(TAG,"protocol_info:::%s:::",this->ctx->genmetaHeader[flipflop].protocol_info.c_str());
        return true;
    }

    if( temp_str1.compare("vin") == 0 )
    {
        this->ctx->genmetaHeader[flipflop].vin = temp_str2;
        LOG_I(TAG,"vin:::%s:::",this->ctx->genmetaHeader[flipflop].vin.c_str());
        return true;
    }

    if( temp_str1.compare("can_firmware_ver") == 0 )
    {
        this->ctx->genmetaHeader[flipflop].can_firmware_ver = temp_str2;
        LOG_D(TAG,"can_firmware_ver:::%s:::",this->ctx->genmetaHeader[flipflop].can_firmware_ver.c_str());
        return true;
    }
    if(temp_str1.compare("can_sn") == 0)
    {
        this->ctx->genmetaHeader[flipflop].can_sn = temp_str2;
        LOG_D(TAG,"can_sn:::%s:::",this->ctx->genmetaHeader[flipflop].can_sn.c_str());
        return true;
    }
    if(temp_str1.compare("can_model") == 0)
    {
        this->ctx->genmetaHeader[flipflop].can_model = temp_str2;
        LOG_D(TAG,"can_model:::%s:::",this->ctx->genmetaHeader[flipflop].can_model.c_str());
        return true;
    }
    if( temp_str1.compare("can_status") == 0 )
    {
        int status;
        stringstream ss(temp_str2);
		ss >> status;
        this->ctx->genmetaHeader[flipflop].can_status = status;
        LOG_D(TAG,"can_status:::%d:::",this->ctx->genmetaHeader[flipflop].can_status);
        return true;
    }
    if( temp_str1.compare("can_src") == 0 )
    {
    	int source;
    	stringstream ss(temp_str2);
    	ss >> source;
        this->ctx->genmetaHeader[flipflop].can_src = source;
        LOG_D(TAG,"can_src:::%d:::",this->ctx->genmetaHeader[flipflop].can_src);
    	return true;
    }

    if( temp_str1.compare("engine_status") == 0 )
    {
        this->ctx->genmetaHeader[flipflop].engine_status = temp_str2;
        LOG_I(TAG,"engine_status:::%s:::",this->ctx->genmetaHeader[flipflop].engine_status.c_str());
        return true;
    }

    if (temp_str1.compare("copy_status_cam") == 0)
    {
        this->ctx->genmetaHeader[flipflop].copy_status_cam = temp_str2;
        LOG_I(TAG, "copy_status_cam:::%s:::", this->ctx->genmetaHeader[flipflop].copy_status_cam.c_str());
        return true;
    }

    if( temp_str1.compare("networkInfo") == 0 )
    {
        this->ctx->genmetaHeader[flipflop].network_info = temp_str2;
        LOG_I(TAG,"network info:::%s:::",this->ctx->genmetaHeader[flipflop].network_info.c_str());
        return true;
    }

    if( temp_str1.compare("inwardStartTime") == 0 )
    {
        int64_t temp_time;
        stringstream ss(temp_str2);
        ss >> temp_time;
        this->ctx->genmetaHeader[flipflop].inward_start_time = temp_time;
        LOG_I(TAG,"inwardStartTime info:::%lld:::",this->ctx->genmetaHeader[flipflop].inward_start_time);
        return true;
    }

    if( temp_str1.compare("inwardStartTimeLd") == 0 )
    {
        int64_t temp_time;
        stringstream ss(temp_str2);
        ss >> temp_time;
        this->ctx->genmetaHeader[flipflop].inward_start_time_ld = temp_time;
        LOG_I(TAG,"inwardStartTimeLd info:::%lld:::",this->ctx->genmetaHeader[flipflop].inward_start_time_ld);
        return true;
    }

    if( temp_str1.compare("ptsStartTime") == 0 )
    {
        int64_t temp_time;
        stringstream ss(temp_str2);
        ss >> temp_time;
        this->ctx->genmetaHeader[flipflop].ptsStartTime = temp_time;
        LOG_I(TAG,"ptsStartTime info:::%lld:::",this->ctx->genmetaHeader[flipflop].ptsStartTime);
        return true;
    }

    if( temp_str1.compare("ptsStartTimeLd") == 0 )
    {
        int64_t temp_time;
        stringstream ss(temp_str2);
        ss >> temp_time;
        this->ctx->genmetaHeader[flipflop].ptsStartTime_ld = temp_time;
        LOG_I(TAG,"ptsStartTimeLd info:::%lld:::",this->ctx->genmetaHeader[flipflop].ptsStartTime_ld);
        return true;
    }

    if( temp_str1.compare("ptsStartTimeDP") == 0 )
	{
		int64_t temp_time;
		stringstream ss(temp_str2);
		ss >> temp_time;
		this->ctx->genmetaHeader[flipflop].ptsStartTime_dp = temp_time;
		LOG_I(TAG,"ptsStartTimeDP info:::%lld:::",this->ctx->genmetaHeader[flipflop].ptsStartTime_dp);
		return true;
    }

    if( temp_str1.compare("inwardPtsStartTime") == 0 )
    {
        int64_t temp_time;
        stringstream ss(temp_str2);
        ss >> temp_time;
        this->ctx->genmetaHeader[flipflop].inward_ptsStartTime = temp_time;
        LOG_I(TAG,"inwardPtsStartTime info:::%lld:::",this->ctx->genmetaHeader[flipflop].inward_ptsStartTime);
        return true;
    }

    if( temp_str1.compare("inwardPtsStartTimeLd") == 0 )
    {
        int64_t temp_time;
        stringstream ss(temp_str2);
        ss >> temp_time;
        this->ctx->genmetaHeader[flipflop].inward_ptsStartTime_ld = temp_time;
        LOG_I(TAG,"inwardPtsStartTimeLd info:::%lld:::",this->ctx->genmetaHeader[flipflop].inward_ptsStartTime_ld);
        return true;
    }

    if( temp_str1.compare("dmsStartTime") == 0 )
    {
        int64_t temp_time;
        stringstream ss(temp_str2);
        ss >> temp_time;
        this->ctx->genmetaHeader[flipflop].dms_start_time = temp_time;
        LOG_I(TAG,"dmsStartTime info:::%lld:::",this->ctx->genmetaHeader[flipflop].dms_start_time);
        return true;
    }

    if( temp_str1.compare("dmsPtsStartTime") == 0 )
    {
        int64_t temp_time;
        stringstream ss(temp_str2);
        ss >> temp_time;
        this->ctx->genmetaHeader[flipflop].dms_ptsStartTime = temp_time;
        LOG_I(TAG,"dmsPtsStartTime info:::%lld:::",this->ctx->genmetaHeader[flipflop].dms_ptsStartTime);
        return true;
    }

    if( temp_str1.compare("dmsStartTimeLd") == 0 )
    {
        int64_t temp_time;
        stringstream ss(temp_str2);
        ss >> temp_time;
        this->ctx->genmetaHeader[flipflop].dms_start_time_ld = temp_time;
        LOG_I(TAG,"dmsStartTimeLd info:::%lld:::",this->ctx->genmetaHeader[flipflop].dms_start_time_ld);
        return true;
    }

    if( temp_str1.compare("dmsPtsStartTimeLd") == 0 )
    {
        int64_t temp_time;
        stringstream ss(temp_str2);
        ss >> temp_time;
        this->ctx->genmetaHeader[flipflop].dms_ptsStartTime_ld = temp_time;
        LOG_I(TAG,"dmsPtsStartTimeLd info:::%lld:::",this->ctx->genmetaHeader[flipflop].dms_ptsStartTime_ld);
        return true;
    }

    if (temp_str1.compare("Voltage in Volts") == 0)
    {
        float temp_volt;
        stringstream ss(temp_str2);
        ss >> temp_volt;
        this->ctx->genmetaHeader[flipflop].volts = temp_volt;
        LOG_I(TAG,"Voltage in Volts:::%f:::",this->ctx->genmetaHeader[flipflop].volts);
        return true;
    }

    if( temp_str1.compare("driverInvariantSession") == 0 )
    {
        this->ctx->genmetaHeader[flipflop].driverInvariantSession = temp_str2;
        LOG_I(TAG,"driverInvariantSession info:::%s:::",this->ctx->genmetaHeader[flipflop].driverInvariantSession.c_str());
        return true;
    }

    if( temp_str1.compare("disMode") == 0 )
    {
        this->ctx->genmetaHeader[flipflop].disMode = temp_str2;
        LOG_I(TAG,"disMode info:::%s:::",this->ctx->genmetaHeader[flipflop].disMode.c_str());
        return true;
    }

    if( temp_str1.compare("privacyModeEvents") == 0 )
    {
        this->ctx->genmetaHeader[flipflop].privacyModeEvents = temp_str2;
        LOG_I(TAG,"privacyModeEvents info:::%s:::",this->ctx->genmetaHeader[flipflop].privacyModeEvents.c_str());
        return true;
    }

    if( temp_str1.compare("rtcValid") == 0 )
    {
        this->ctx->genmetaHeader[flipflop].rtcValid_string = temp_str2;
        LOG_I(TAG,"rtcValid:::%s:::",this->ctx->genmetaHeader[flipflop].rtcValid_string.c_str());
        return true;
    }

    if( temp_str1.compare("rtc_jump_from") == 0 )
    {
        this->ctx->genmetaHeader[flipflop].rtc_jump_from_string = temp_str2;
        LOG_I(TAG,"rtc_jump_from:::%s:::",this->ctx->genmetaHeader[flipflop].rtc_jump_from_string.c_str());
        return true;
    }

    if( temp_str1.compare("rtc_jump_to") == 0 )
    {
        this->ctx->genmetaHeader[flipflop].rtc_jump_to_string = temp_str2;
        LOG_I(TAG,"rtc_jump_to:::%s:::",this->ctx->genmetaHeader[flipflop].rtc_jump_to_string.c_str());
        return true;
    }

    if( temp_str1.compare("outward_cam_privacy") == 0 )
    {
        int64_t temp_privacy;
        stringstream ss(temp_str2);
        ss >> temp_privacy;
        this->ctx->genmetaHeader[flipflop].outward_cam_privacy = temp_privacy;
        LOG_I(TAG,"outward_cam_privacy :::%d:::",this->ctx->genmetaHeader[flipflop].outward_cam_privacy);
        return true;
    }

    if( temp_str1.compare("inward_cam_privacy") == 0 )
    {
        int64_t temp_privacy;
        stringstream ss(temp_str2);
        ss >> temp_privacy;
        this->ctx->genmetaHeader[flipflop].inward_cam_privacy = temp_privacy;
        LOG_I(TAG,"inward_cam_privacy :::%d:::",this->ctx->genmetaHeader[flipflop].inward_cam_privacy);
        return true;
    }

    if( temp_str1.compare("irled_status") == 0 )
    {
        int irled_status;
        stringstream ss(temp_str2);
        ss >> irled_status;
        this->ctx->genmetaHeader[flipflop].irled_status = irled_status;
        LOG_I(TAG, "irled_status :::%d:::", this->ctx->genmetaHeader[flipflop].irled_status);
        return true;
    }

    if( temp_str1.compare("irled_states") == 0 )
    {
        this->ctx->genmetaHeader[flipflop].irled_states = temp_str2;
        LOG_I(TAG,"irled_states :::%s:::", this->ctx->genmetaHeader[flipflop].irled_states.c_str());
        return true;
    }

    return false;
}

bool Genmeta::push_data(Gps::gps_metadata_t gps_data, int flipflop) // seperate call per sensor; callback
{
	this->ctx->gps_data_collection[flipflop].push_back(gps_data);
	LOG_D(TAG, "Success in push_data");
	return true;
}

bool Genmeta::push_data(Gps::gps_pps_data_t gps_pps_data, int flipflop) // seperate call per sensor; callback
{
    this->ctx->gps_pps_data_collection[flipflop].push_back(gps_pps_data);
    LOG_D(TAG, "Success in push_data");
    return true;
}

bool Genmeta::push_data(Ublox::ublox_constellation_data_t ublox_constell_data, int flipflop) // seperate call per sensor; callback
{
    this->ctx->ublox_constell_data_collection = ublox_constell_data;
    LOG_I(TAG, "Success in UBX-MON-GNSS push_data");
    return true;
}

bool Genmeta::push_data(Ublox::ublox_data_t ublox_data, int flipflop)
{
    this->ctx->ublox_data_collection[flipflop].push_back(ublox_data);
    LOG_D(TAG, "Success in push_data");
    return true;
}

bool Genmeta::push_data_ignition(ignition_status_records_t ignition_data, int flipflop)
{
	this->ctx->ignition_status_collection[flipflop].push_back(ignition_data);
	LOG_D(TAG, "Success in push_data");
	return true;
}

int Genmeta::get_gps_data_size(int flipflop)
{
    return this->ctx->gps_data_collection[flipflop].size();
}

bool Genmeta::push_data(Obd::obd_data_t obd_data, int flipflop) // seperate call per sensor; callback
{
	this->ctx->obddata_collection[flipflop].push_back(obd_data);
	LOG_D(TAG, "Success in obd push_data");
	return true;
}

bool Genmeta::push_data(Obd::fr_data_t freport_data, int flipflop) // seperate call per sensor; callback
{
	this->ctx->fuel_report_collection[flipflop].push_back(freport_data);
	LOG_D(TAG, "Success in fuel report push_data");
	return true;
}

bool Genmeta::push_data(Obd::ir_data_t ireport_data, int flipflop) // seperate call per sensor; callback
{
	this->ctx->idling_report_collection[flipflop].push_back(ireport_data);
	LOG_D(TAG, "Success in idling report push_data");
	return true;
}

bool Genmeta::push_data(FrameInfo::frameinfo_t frame_data, int flipflop)
{
    this->ctx->frame_data_collection[flipflop].push_back(frame_data);
    LOG_D(TAG, "Success in push_data");
    return true;
}

int Genmeta::get_obd_data_size(int flipflop)
{
    return this->ctx->obddata_collection[flipflop].size();
}

bool Genmeta::push_data(Imu::imu_sensor_t imu, Imu::val_t imuData, int flipflop) // seperate call per sensor; callback
{
    genmeta_imudata_t getmeta_imuData;
    getmeta_imuData.type = imu;
    getmeta_imuData.imuData = imuData;

#ifdef BAGHEERA2
    switch (imu) {

        case Imu::IMU_ACCEL:
            this->ctx->imu_accel_data_collection[flipflop].push_back(getmeta_imuData);
            break;
        case Imu::IMU_GYRO:
            this->ctx->imu_gyro_data_collection[flipflop].push_back(getmeta_imuData);
            break;
        case Imu::IMU_MAGNETO:
            this->ctx->imu_magneto_data_collection[flipflop].push_back(getmeta_imuData);
            break;
        case Imu::IMU_TEMP:
            this->ctx->imu_temperature_data_collection[flipflop].push_back(getmeta_imuData);
            break;
        default:
            break;
    }
#elif KRAIT
    this->ctx->imu_data_collection[flipflop].push_back(getmeta_imuData);
#endif

    LOG_D(TAG, "Success in push_data");
    return true;
}

int Genmeta::get_imu_data_size(int flipflop)
{
#ifdef BAGHEERA2
    return this->ctx->imu_accel_data_collection[flipflop].size() +
           this->ctx->imu_gyro_data_collection[flipflop].size();
#else
    return this->ctx->imu_data_collection[flipflop].size();
#endif
}

bool Genmeta::push_data(genmeta_usralert_t alert, int flipflop) // seperate call per sensor; callback
{
    if( ! (alert.btn >= 0 && alert.btn < MAX_BUTTONS)  ) {
	    LOG_E(TAG, "Wrong button number");
        return false;
    }

	this->ctx->usr_alert_collection[flipflop].push_back(alert);
	LOG_D(TAG, "Success in push_data");
	return true;
}

int Genmeta::get_usr_alert_size(int flipflop)
{
    return this->ctx->usr_alert_collection[flipflop].size();
}

bool Genmeta::push_data(genmeta_highgalert_t alert, int flipflop) // seperate call per sensor; callback
{
	this->ctx->highg_alert_collection[flipflop].push_back(alert);
	LOG_D(TAG, "Success in push_data");
	return true;
}

bool Genmeta::stop_data_collection(int flipflop) // for all sensoer; call from central
{
    LOG_I(TAG, "Success in stop_data_collection");
    int imu_arr_len;
#ifdef BAGHEERA2
    imu_arr_len = this->ctx->imu_accel_data_collection[flipflop].size() + this->ctx->imu_gyro_data_collection[flipflop].size();
#elif KRAIT
    imu_arr_len = this->ctx->imu_data_collection[flipflop].size();
#endif
    LOG_I(TAG, "GPS arr lenght %d, IMU arr lenght %d, User Alert arr length %d",
            this->ctx->gps_data_collection[flipflop].size(),
            imu_arr_len,
            this->ctx->usr_alert_collection[flipflop].size() );

    return true;
}


#define PRI_Q <<"\""
#define PRI_COMMA ","
#define PRI_OPEN_CURLY "{"
#define PRI_CLOSE_CURLY "}"
#define PRI_COL ":"
#define PRI_Q_COND(str,len) ((len) ? ("[" + str + "]") : ("null"))
//static const string PRI_Q = "\"";

enum genmeta_header_var {
	HEADER_TIME,
	HEADER_VOLT,
	HEADER_DEVICEID,
	HEADER_CAMSENABLED,
	HEADER_PROTOCOL_INFO,
	HEADER_VIN,
	HEADER_CAN_FW_VER,
	HEADER_DRIVERID,
    HEADER_DRIVERID_V2,
	HEADER_SESSIONID,
	HEADER_DEVICEMODES,
	HEADER_APPVRE,
	HEADER_METADATAVER,
	HEADER_SYSTEMUPTIME,
	HEADER_SERVICEUPTIME,
	HEADER_VERTANGL,
	HEADER_HORZANGL,
	HEADER_VIDNAME,
	HEADER_PREVVIDNAME,
	HEADER_NEXTVIDNAME,
	HEADER_VEHCLIC,
	HEADER_SPEEDUNIT,
	HEADER_TRIPNUM,
	HEADER_PARTNUM,
	HEADER_STRTTIME,
	HEADER_STRTTIME_LD,
	HEADER_STRTTIME_DP,
	HEADER_STRTTIME_MICRO,
	HEADER_STRTTIMERAW_MICRO,
	HEADER_OFFSET,
	HEADER_GPSSTARTTIME,
	HEADER_GPSENDTIME,
	HEADER_ENDTIME,
	HEADER_ENDTIME_MICRO,
	HEADER_ENDTIME_RAW_MICRO,
	HEADER_LPW_NO_RECORD,

	HEADER_DEVICETYPE,
	HEADER_DEVICESUBTYPE,
	HEADER_VEHCLASS,

	HEADER_NETWORKINFO,
	HEADER_AUDIOENABLE,

	HEADER_INWARD_STARTTIME,
	HEADER_INWARD_STARTTIME_LD,
	HEADER_PTSSTARTTIME,
	HEADER_PTSSTARTTIME_LD,
	HEADER_PTSSTARTTIME_DP,
	HEADER_INWARD_PTSSTARTTIME,
	HEADER_INWARD_PTSSTARTTIME_LD,

	HEADER_DMS_STARTTIME,
	HEADER_DMS_PTSSTARTTIME,
    HEADER_DMS_STARTTIME_LD,
	HEADER_DMS_PTSSTARTTIME_LD,

	HEADER_driverInvariantSession,
	HEADER_disMode,
	HEADER_UDID,
	HEADER_SESSIONCOUNT,
	HEADER_RTCVALID,
	HEADER_RTCJUMPFROM,
	HEADER_RTCJUMPTO,
	HEADER_VALID_GPS_COUNT,
    HEADER_CAN_SN,
    HEADER_CAN_MODEL,
    HEADER_CAN_STATUS,
	HEADER_CAN_SRC,
    HEADER_ENGINE_STATUS,
    HEADER_COPY_STATUS_CAM,
    HEADER_OUTWARD_CAM_PRIVACY,
    HEADER_INWARD_CAM_PRIVACY,

    HEADER_IRLED_STATUS,
    HEADER_IRLED_STATES,

    HEADER_UNKNOWN,
};

static const string head[96] =
{
	"\"Time\": ",
	", \"Voltage in Volts\": ",
	", \"deviceId\": ",
	", \"cameras\": ",
	", \"protocol_info\": ",
	", \"vin\": ",
	", \"can_firmware_ver\": ",
	", \"driverId\": ",
    ", \"driverId_v2\": ",
	", \"sessionId\": ",
	", \"deviceModes\": ",
	", \"app_ver\": ",
	", \"ver\": ",
	", \"systemUpTime\": ",
	", \"serviceUpTime\": ",
	", \"verticalAngle\": ",
	", \"horizontalAngle\": ",
	", \"videoName\": ",
	", \"prevVideoName\": ",
	", \"nextVideoName\": ",
	", \"vehicleId\": ",
	", \"speedUnit\": ",
	", \"tripNo\": ",
	", \"partNo\": ",
	", \"startTime\": ",
	", \"startTimeLd\": ",
	", \"startTimeDP\": ",
	", \"startTimeMicro\": ",
	", \"startTimeRawMicro\": ",
	", \"offset\": ",
	", \"gpsStartTime\": ",
	", \"gpsEndTime\": ",
	", \"endTime\": ",
	", \"endTimeMicro\": ",
	", \"endTimeRawMicro\": ",
	", \"lpw_no_record\": ",

	", \"deviceType\": ",
	", \"deviceSubType\": ",
	", \"vehClass\": ",
	", \"networkInfo\": ",
	", \"audioEnable\": ",

	", \"inwardStartTime\": ",
	", \"inwardStartTimeLd\": ",
	", \"ptsStartTime\": ",
	", \"ptsStartTimeLd\": ",
	", \"ptsStartTimeDP\": ",
	", \"inwardPtsStartTime\": ",
	", \"inwardPtsStartTimeLd\": ",

	", \"dmsStartTime\": ",
	", \"dmsPtsStartTime\": ",
	", \"dmsStartTimeLd\": ",
	", \"dmsPtsStartTimeLd\": ",

	", \"driverInvariantSession\": ",
	", \"disMode\": ",
	", \"udid\": ",
	", \"sessionCount\": ",
	", \"rtcValid\": ",
	", \"rtc_jump_from\": ",
	", \"rtc_jump_to\": ",
	", \"validGPSEntries\": ",
    ", \"can_sn\": ",
    ", \"can_model\": ",
    ", \"can_status\": ",
	", \"can_src\": ",
    ", \"engine_status\": ",
    ", \"copy_status_cam\": ",
    ", \"outward_cam_privacy\": ",
    ", \"inward_cam_privacy\": ",

    ", \"irled_status\": ",
    ", \"irled_states\": ",
};

const string privacy_metadata_entry = "privacymode";
const string privacy_offduty_metadata_entry = "privacymode_offduty";
const string privacy_geofence_metadata_entry = "privacymode_geofence";
const string engine_idle_metadata_entry = "idle";
const string processing_mode_metadata_entry = "processing_mode";
const string ignition_status_metadata_entry = "ignition_status";
const string privacy_events = ", \"privacyModeEvents\": ";

enum genmeta_gpsstr_var {
	GPSSTR_LAT,
	GPSSTR_LONG,
	GPSSTR_ALTI,
	GPSSTR_ALTI_MSL,
	GPSSTR_ACCU,
	GPSSTR_BEAR,
	GPSSTR_SPEED,
	GPSSTR_TIME,
	GPSSTR_RAW_TIME,
	GPSSTR_VALIDITY,

	GPSSTR_UNKNOWN
};

static const string gpsstr[32] =
{
	"\"lat\": ",
	", \"long\": ",
	", \"altitude\": ",
	", \"altitudeMSL\": ",
	", \"accuracy\": ",
	", \"bearing\": ",
	", \"speed\": ",
	", \"timestamp\": ",
	", \"raw_timestamp\": ",
	", \"valid\": "
};

enum genmeta_ubloxstr_var {
    UBLOXSTR_SYSTEM_TIME,
    UBLOXSTR_CLOCK_TIME,
    UBLOXSTR_CLASS,
    UBLOXSTR_ID,
    UBLOXSTR_LENGTH,
    UBLOXSTR_PAYLOAD,
    UBLOXSTR_UNKNOWN
};

enum genmeta_ublox_constellstr_var {
    UBLOXSTR_EN_GPS,
    UBLOXSTR_EN_SBAS,
    UBLOXSTR_EN_GALILEO,
    UBLOXSTR_EN_BEIDOU,
    UBLOXSTR_EN_IMES,
    UBLOXSTR_EN_QZSS,
    UBLOXSTR_EN_GLONASS,
    UBLOXSTR_CONSTELL_UNKNOWN
};

static const string ublox_constellstr[32] =
{
    "\"enableGPS\": ",
    ", \"enableSBAS\": ",
    ", \"enableGalileo\": ",
    ", \"enableBeiDou\": ",
    ", \"enableIMES\": ",
    ", \"enableQZSS\": ",
    ", \"enableGLONASS\": ",
};

static const string ubloxstr[32] =
{
    "\"system_time\": ",
    ", \"class\": ",
    ", \"id\": ",
    ", \"length\": ",
    ", \"payload\": "
};

enum genmeta_frameinfostr_var {
    FRAMEINFOSTR_CAPTURE_TIME,
    FRAMEINFOSTR_EPOCH_TIME,
    FRAMEINFOSTR_FRAME_NUMBER
};

static const string frameinfostr[32] =
{
    "\"capture_time\": ",
    ", \"epoch\": ",
    ", \"frame_number\": "
};

enum genmeta_usralertstr_var {
    USRALERTSTR_TIME,
    USRALERTSTR_EVENTCODE,
    USRALERTSTR_SOURCE
};

enum genmeta_highgalertstr_var {
    HIGHGALERTSTR_TIME,
};

static const string usralertstr[32] =
{
	"\"timestamp\": ",
	"\"event_code\": ",
    "\"source\": ",
};

enum genmeta_ignitionstatusstr_var {
    IGNITIONSTR_STATUS,
    IGNITIONSTR_TIME
};

static const string ignitionstatusstr[32] =
{
	"\"status\": ",
	"\"ts\": ",
};

static const string highgalertstr[32] =
{
	"\"timestamp\": ",
};

void append_if_json_string(std::stringstream &output_stream, std::stringstream &input_stream)
{
	json_error_t error;
	string input_string = input_stream.str() ;
	input_string = "{" + input_string.substr(input_string.find(",") + 1)   + "}" ;
	json_t* root = json_loads(input_string.c_str(), 0, &error);
	if(!root){
		LOG_E(TAG, "%d:Not a valid json string: %s", __LINE__, input_string.c_str() );
		LOG_E(TAG, "Error: %s", error.text );
	}
	else{
		output_stream << input_stream.rdbuf() ;
	}
	input_stream.str("");
    json_decref(root);
}
void append_if_json_string_no_comma(std::stringstream &output_stream, std::stringstream &input_stream)
{
	json_error_t error;
	string input_string = input_stream.str() ;
	input_string = "{" + input_string   + "}" ;
	json_t* root = json_loads(input_string.c_str(), 0, &error);
	if(!root){
		LOG_E(TAG, "%d: Not a valid json string: %s", __LINE__, input_stream.str().c_str() );
		LOG_E(TAG, "Error: %s", error.text );
	}
	else{
		output_stream << input_stream.rdbuf() ;
	}
	input_stream.str("");
    json_decref(root);
}

bool csv_to_json(string source_path, string destination_path,
        device_mode_t &device_mode_global_partial, int &partial_video_len_sec)
{
    ifstream inputFile(source_path);
    std::stringstream str_stream, obd_str_stream, obd_status;
    string delim = "";
    string final_delim = "";
    map<string,stringstream> data;
    int64_t start_time, present_time = 0;
    bool first_data = true;
    str_stream << std::fixed  << std::setprecision(7); // if needed set it to IMU aswell
    string gps_string = " ";
    string pps_index_string = " ";
    string pps_start_index_string = " ";
    string gps_start_index_string = " ";
    string frameinfo_string = " ";
    string imu_string = " ";
    string obd_string = " ";
    string other_string = " ";
    string usralert_string = " ";
    string imu_temp_string = " ";
    string imu_data_string = " ";
    string ublox_string = " ";
    string ublox_constellation_string = " ";
    string gps_pps_string = " ";
    string irled_states_string = " ";
    int current_offduty_privacy = -1;
    int current_geofence_privacy = -1;
    int current_enhanced_privacy = -1;
    int current_fused_privacy = -1;
    int privacy_sec_count = 0;
    int64_t start_time_privacy = 0;
    bool has_usr_alert = false;
    int current_irled_status = -1;
    int overall_irled_status = -1;
    int num_of_irled_states = 0;
    int irled_record_duration = 0;
    int64_t new_irled_state_start_time = 0;

    int ignition_on_duration = 0;
    string line;
    int64_t startTime = 0, endTime = 0, endTime_temp = 0;
    int64_t startTimeRawMicro = 0, startTimeRawMilli = 0;
    float volt = 0.0;
    int line_count = 0 ;
    bool valid_file = false;

    device_mode_global_partial.individual_states_len = 0;
    device_mode_global_partial.engine_idle = false;
    /* increased by 1 for DMS camera support */
    device_mode_global_partial.ref_count = 5;
    device_mode_global_partial.privacy_status = PRIVACY_OFF;
    device_mode_global_partial.privacy_status_offduty = PRIVACY_OFF;
    device_mode_global_partial.privacy_status_geofence = PRIVACY_OFF;

    getline(inputFile, line, '\n');
    while (getline(inputFile, line, '\n')) {
        if (line.length() == 0) {
            LOG_D(TAG, "Empty line  ");
            continue;
        }
        str_stream.str("");
        string metaDataType = line.substr(0, line.find(","));
        line = line.substr(line.find(", ")+2);
        stringstream ss(line);
        float lat, longitude, altitude, accuracy, bearing, speed;
        int64_t timestamp;
        string field = "";
        if (metaDataType == "gps") {
            string temp[10];
            int i = 0 ;
            while (std::getline(ss, field, ',')) {
                if (i <= GPSSTR_VALIDITY)
                    temp[i] = field;
                ++i;
            }

            if (i < (GPSSTR_VALIDITY + 1)) {
                LOG_I(TAG, "Incomplete gps data: %s", line.c_str());
                continue;
            } else if (i > (GPSSTR_VALIDITY + 1)) {
                LOG_I(TAG, "gps data entry has more than 10 fields, hence not considering this gps data entry, i: %d, entry: %s", i, line.c_str());
                continue;
            }

            str_stream << "{" \
                            << gpsstr[GPSSTR_LAT]           << temp[GPSSTR_LAT] \
                            << gpsstr[GPSSTR_LONG]          << temp[GPSSTR_LONG] \
                            << gpsstr[GPSSTR_ALTI]          << temp[GPSSTR_ALTI] \
                            << gpsstr[GPSSTR_ALTI_MSL]      << temp[GPSSTR_ALTI_MSL] \
                            << gpsstr[GPSSTR_ACCU]          << temp[GPSSTR_ACCU] \
                            << gpsstr[GPSSTR_BEAR]          << temp[GPSSTR_BEAR] \
                            << gpsstr[GPSSTR_SPEED]         << temp[GPSSTR_SPEED] \
                            << gpsstr[GPSSTR_TIME]          << temp[GPSSTR_TIME] \
                            << gpsstr[GPSSTR_RAW_TIME]      << temp[GPSSTR_RAW_TIME] \
                            << gpsstr[GPSSTR_VALIDITY]      << temp[GPSSTR_VALIDITY] << "} ,";

            gps_string += str_stream.str();
            if(is_number(temp[GPSSTR_TIME])) {
                string_to_int64(temp[GPSSTR_TIME], endTime_temp);
                if(endTime_temp > endTime ) { endTime = endTime_temp; }
                if(startTime == 0) {
                    string_to_int64(temp[GPSSTR_TIME], startTime);
                }
            }
            valid_file = true;

        } else if (metaDataType == "gps_pps") {
            string temp[3];
            int i = 0 ;
            while (std::getline(ss, field, ',')) {
                if (i <= 2)
                    temp[i] = field;
                ++i;
            }

            if (i < 3) {
                LOG_I(TAG, "Incomplete gps_pps data: %s", line.c_str());
                continue;
            } else if (i > 3) {
                LOG_I(TAG, "gps_pps data entry has more than 3 fields, hence not considering this gps_pps data entry, i: %d, entry: %s", i, line.c_str());
                continue;
            }

            str_stream <<  temp[1] << ",";
            gps_pps_string += str_stream.str();

            if (is_number(temp[2])) {
                string_to_int64(temp[2], endTime_temp);
                endTime_temp /= 1000; //store endTime in millisecs
                if (endTime_temp > endTime)
                    endTime = endTime_temp;

                if (startTime == 0)
                    startTime = endTime_temp;
            }

	        str_stream.str("");
	        str_stream << temp[0];
            pps_index_string += str_stream.str();

            valid_file = true;

        } else if (metaDataType == "gps_start_idx") {
            string temp = "";
            std::getline(ss, temp, ',');

            str_stream.str("");
            str_stream << temp;
            gps_start_index_string = str_stream.str();

            // json invalid if val start with zero, for zero val convert to string
	        if (std::stoi(gps_start_index_string) == 0)
		        gps_start_index_string = std::to_string(0);

            valid_file = true;

        } else if (metaDataType == "pps_start_idx") {
            string temp = "";
            std::getline(ss, temp, ',');

            str_stream.str("");
            str_stream << temp;
            pps_start_index_string = str_stream.str();

            // json invalid if val start with zero, for zero val convert to string
            if (std::stoi(pps_start_index_string) == 0)
                pps_start_index_string = std::to_string(0);

            valid_file = true;

        } else if ((metaDataType == "Acel")  ||  (metaDataType == "Gyro")) {
            string temp[4];
            string imu_type_str = "\"accelerometer\": ";
            if (metaDataType == "Gyro") {
                imu_type_str = "\"gyro\": ";
            }
            int i = 0 ;
            while (std::getline(ss, field, ',')) {
                if (i <= 3)
                    temp[i] = field;
                ++i;
            }
            if (i < 4 ) {
                LOG_I(TAG, "Incomplete %s data: %s", imu_type_str.c_str(), line.c_str());
                continue;
            } else if (i > 4) {
                LOG_I(TAG, "%s entry has more than 4 fields, hence not considering this %s data entry, i: %d, entry: %s", imu_type_str.c_str(), imu_type_str.c_str(), i, line.c_str());
                continue;
            }
            str_stream << "{" \
                       << imu_type_str \
                       << "\"  "
                       <<  temp[0] << "  "
                       <<  temp[1] << "  "
                       <<  temp[2] << "  "
                       <<  temp[3] << "\"" \
                       << "}," ;

            imu_string += str_stream.str();
            if(is_number(temp[3])) {
                string_to_int64(temp[3], endTime_temp);
                if(endTime_temp > endTime ) { endTime = endTime_temp; }
                if(startTime == 0) {
                    string_to_int64(temp[3], startTime);
                }
            }
            valid_file = true;

        } else if (metaDataType == "ImuData") {
            string temp[7];
            int i = 0 ;
            while (std::getline(ss, field, ',')) {
                if (i <= 6)
                    temp[i] = field;
                ++i;
            }

            if (i < 7) {
                LOG_I(TAG, "Incomplete ImuData: %s", line.c_str());
                continue;
            } else if (i > 7) {
                LOG_I(TAG, "ImuData entry has more than 7 fields, hence not considering this ImuData entry, i: %d, entry: %s", i, line.c_str());
                continue;
            }

            str_stream << "{"
                       <<  "\"a_x\":" << temp[0] << "," \
                       <<  "\"a_y\":" << temp[1] << "," \
                       <<  "\"a_z\":" << temp[2] << "," \
                       <<  "\"g_x\":" << temp[3] << "," \
                       <<  "\"g_y\":" << temp[4] << "," \
                       <<  "\"g_z\":" << temp[5] << "," \
                       <<  "\"ts\":"  << temp[6] \
                       << "}," ;

            imu_data_string += str_stream.str();
            valid_file = true;

        } else if (metaDataType == "obd_status") {
            string temp = "";
            std::getline(ss, temp, ',');
            obd_status.str("");
            obd_status << temp;

        } else if (metaDataType == "imutemp") {
            string temp[4];
            string imu_type_str = "\"imutemp\": ";
            int i = 0 ;
            while (std::getline(ss, field, ',')) {
                if (i <= 3)
                    temp[i] = field;
                ++i;
            }
            if (i < 4) {
                LOG_I(TAG, "Incomplete imutemp data: %s", line.c_str());
                continue;
            } else if (i > 4) {
                LOG_I(TAG, "imutemp entry has more than 4 fields, hence not considering this imutemp entry, i: %d, entry: %s", i, line.c_str());
                continue;
            }
            str_stream << "["
                       <<  temp[0] << "," \
                       <<  temp[3] \
                       << "]," ;

            imu_temp_string += str_stream.str();

            valid_file = true;

        } else if (metaDataType == "ublox") {
            string temp[6];
            int i = 0 ;
            while (std::getline(ss, field, ',')) {
                if (i <= 5)
                    temp[i] = field;
                ++i;
            }
            if (i < 6) {
                LOG_I(TAG, "Incomplete ublox data: %s", line.c_str());
                continue;
            } else if (i > 6) {
                LOG_I(TAG, "ublox entry has more than 6 fields, hence not considering this ublox entry, i: %d, entry: %s", i, line.c_str());
                continue;
            }
            str_stream << "{" \
                            << ubloxstr[UBLOXSTR_SYSTEM_TIME]           << temp[UBLOXSTR_SYSTEM_TIME] \
                            << ubloxstr[UBLOXSTR_CLASS]                 << temp[UBLOXSTR_CLASS] \
                            << ubloxstr[UBLOXSTR_ID]                    << temp[UBLOXSTR_ID] \
                            << ubloxstr[UBLOXSTR_LENGTH]                << temp[UBLOXSTR_LENGTH] \
                            << ubloxstr[UBLOXSTR_PAYLOAD]               << temp[UBLOXSTR_PAYLOAD] << "},";

            ublox_string += str_stream.str();

#if 0 /* Turning this code off as we are not confident on the units in which epoch time is recorded */
            if(is_number(temp[UBLOXSTR_SYSTEM_TIME])) {
                string_to_int64(temp[UBLOXSTR_SYSTEM_TIME], endTime_temp) ;
                if(endTime_temp > endTime ) { endTime = endTime_temp; }
                if(startTime == 0) {
                    string_to_int64(temp[UBLOXSTR_SYSTEM_TIME], startTime);
                }
            }
#endif
            valid_file = true;

        } else if (metaDataType == "ublox_constellation") {
            ublox_constellation_string.clear();
            string temp[7];
            int i = 0 ;
            while(std::getline(ss, field, ',')) {
                if (i <= 6)
                    temp[i] = field;
                ++i;
            }
            if(i < 7){
                LOG_I(TAG, "Incomplete ublox_constellation data: %s", line.c_str());
                continue;
            } else if (i > 7) {
                LOG_I(TAG, "ublox_constellation entry has more than 7 fields, hence not considering this ublox_constellation entry, i: %d, entry: %s", i, line.c_str());
                continue;
            }
            str_stream << "{"\
                       << ublox_constellstr[UBLOXSTR_EN_GPS]     << temp[UBLOXSTR_EN_GPS] \
                       << ublox_constellstr[UBLOXSTR_EN_SBAS]    << temp[UBLOXSTR_EN_SBAS] \
                       << ublox_constellstr[UBLOXSTR_EN_GALILEO] << temp[UBLOXSTR_EN_GALILEO] \
                       << ublox_constellstr[UBLOXSTR_EN_BEIDOU]  << temp[UBLOXSTR_EN_BEIDOU] \
                       << ublox_constellstr[UBLOXSTR_EN_IMES]    << temp[UBLOXSTR_EN_IMES] \
                       << ublox_constellstr[UBLOXSTR_EN_QZSS]    << temp[UBLOXSTR_EN_QZSS] \
                       << ublox_constellstr[UBLOXSTR_EN_GLONASS] << "\"" << temp[UBLOXSTR_EN_GLONASS] <<"\""\
                       << "} ";

            ublox_constellation_string += str_stream.str();
            valid_file = true;

        } else if (metaDataType == "frameinfo") {
            string temp[3];
            int i = 0 ;
            while (std::getline(ss, field, ',')) {
                if (i <= 2)
                    temp[i] = field;
                ++i;
            }

            if (i < 3) {
                LOG_I(TAG, "Incomplete frameinfo data: %s", line.c_str());
                continue;
            } else  if (i > 3) {
                LOG_I(TAG, "frameinfo entry has more than 3 fields, hence not considering this frameinfo entry, i: %d, entry: %s", i, line.c_str());
                continue;
            }

            if (is_number(temp[FRAMEINFOSTR_EPOCH_TIME])) {
                string_to_int64(temp[FRAMEINFOSTR_EPOCH_TIME], endTime_temp) ;
                endTime_temp /= 1000; //store endTime in millisecs
                if(endTime_temp > endTime)
                    endTime = endTime_temp;

                temp[FRAMEINFOSTR_EPOCH_TIME] = std::to_string(endTime_temp);

                if (startTime == 0) {
                    string_to_int64(temp[FRAMEINFOSTR_EPOCH_TIME], startTime);
                    startTime /= 1000; //store startTime in millisecs
                }
            }
	    str_stream << "{" \
		       << frameinfostr[FRAMEINFOSTR_CAPTURE_TIME] << temp[FRAMEINFOSTR_CAPTURE_TIME] \
		       << frameinfostr[FRAMEINFOSTR_EPOCH_TIME]   << temp[FRAMEINFOSTR_EPOCH_TIME] \
		       << frameinfostr[FRAMEINFOSTR_FRAME_NUMBER] << temp[FRAMEINFOSTR_FRAME_NUMBER] \
		       << "} ,";

            frameinfo_string += str_stream.str();

            valid_file = true;

        } else if (metaDataType == "obd") {
            string temp[10];
            int i = 0 ;
            while(std::getline(ss, temp[i], ',')) {
            ++i;
            }
            if (i < PARTIAL_OBD_DATA) {
                LOG_I(TAG, "Incomplete data: %s",line.c_str() );
                continue;
            }

            // remove the space at the start of string
            temp[1].erase(0,1);
            temp[2].erase(0,1);
#ifdef KRAIT
            temp[3].erase(0,1);
            temp[4].erase(0,1);
//                LOG_I(TAG, "DEBUG before : temp3 : %s : temp4 : %s", temp[3].c_str(), temp[4].c_str() );
#endif

            if(data[temp[1]].str().size() <= 0){
                delim = "";
            }
            else{
                delim = ",";
            }

            //convert timestamp to integer
            if(first_data){
                if(is_number(temp[0])) {
                    string_to_int64(temp[0], start_time);
                }
                first_data = false;
            }
            if(is_number(temp[0])) {
                string_to_int64(temp[0], present_time);
            }

#ifdef BAGHEERA2
            data[temp[1]] << delim << "\"" << (present_time - start_time) << " " << fixed << setprecision(OBD_DATA_PRECISION) <<  temp[2] << "\"";
#elif KRAIT
            if (temp[4] == "1") {
                LOG_I(TAG, "DEBUG : temp3 : %s : temp4 : %s", temp[3].c_str(), temp[4].c_str());
                data[temp[3]] << delim << "\"" << (present_time - start_time) << " " << fixed << setprecision(OBD_DATA_PRECISION) <<  temp[2] << "\"";
            } else {
                data[temp[1]] << delim << "\"" << (present_time - start_time) << " " << fixed << setprecision(OBD_DATA_PRECISION) <<  temp[2] << "\"";
            }
#endif
            valid_file = true;

#if 0 /* Turning this code off as we are not confident on the units in which epoch time is recorded */
            if(is_number(temp[0])) {
                string_to_int64(temp[0], endTime_temp);
                if(endTime_temp > endTime ) { endTime = endTime_temp; }
                if(startTime == 0) {
                    string_to_int64(temp[0], startTime);
                }
            }
#endif
        } else if (metaDataType == "udid") {
            string temp = "";
            std::getline(ss, temp, ',');

            if (is_number(temp)) {
                str_stream << "\"" << metaDataType << "\"" <<": "  << temp << PRI_COMMA ;
                ctx.udid_string = temp;
                other_string += str_stream.str();
                LOG_I(TAG, "udid form partial file : %s", ctx.udid_string.c_str() );
            }

        } else if (metaDataType == "sessionCount") {
            string temp = "";
            std::getline(ss, temp, ',');

            if (is_number(temp)) {
                str_stream << "\"" << metaDataType << "\"" <<": "  << temp << PRI_COMMA ;
                ctx.sessionCount_string = temp;
                other_string += str_stream.str();
                LOG_I(TAG, "sessionCount from partial file: %s", ctx.sessionCount_string.c_str() );
            }

        } else if (metaDataType == "usrAlert") {
            string temp[3];
            int i = 0 ;
            while (std::getline(ss, field, ',')) {
                if (i <= 2) {
                    temp[i] = field;
                }
                ++i;
            }

            if (i < 3) {
                LOG_I(TAG, "Incomplete user alert data : %s", line.c_str());
                continue;
            } else if (i > 3) {
                LOG_I(TAG, "usrAlert entry has more than 2 fields, hence not considering this usrAlert entry, i: %d, entry: %s", i, line.c_str());
                continue;
            }
            int btn;
            string_to_integer(temp[1], btn);

            if (!((0 <= btn) && (MAX_BUTTONS > btn))) {
                LOG_E(TAG, "Invalid button value %d, hence not processing the user alert", btn);
                continue;
            }
            str_stream << "{" \
                       << usralertstr[USRALERTSTR_TIME]      << temp[0] << ", " \
                       << usralertstr[USRALERTSTR_EVENTCODE] << "\"" << usralert_map[btn] << "\", " \
                       << usralertstr[USRALERTSTR_SOURCE]    << "\"" << temp[2] << "\" " \
                       << "} ,";

            usralert_string += str_stream.str();

            has_usr_alert = true;
            valid_file = true;
            device_mode_global_partial.partial_privacy_params.has_user_alert = true;
        } else if (metaDataType == "irled_state") {
            string temp = "";
            std::getline(ss, temp, ',');
            int new_irled_status;
            if (string_to_integer(temp, new_irled_status) == false) {
                LOG_E(TAG, "csv_to_json:: failed to parse new_irled_status");
                new_irled_status = current_irled_status;
            }
            if (new_irled_status == current_irled_status) {
                //No change in irled state
                irled_record_duration++;
            } else {
                if (current_irled_status != -1) {
                    // For every IR LED state change except the very first state of the session
                    irled_record_duration++;
                }
                new_irled_state_start_time = startTime + (irled_record_duration * 1000); // in millisecs

                str_stream << "{"
                           <<  "\"status\":" << new_irled_status << "," \
                           <<  "\"time\":"   << new_irled_state_start_time \
                           << "}," ;

                irled_states_string += str_stream.str();
                current_irled_status = new_irled_status;
                num_of_irled_states++;
            }
        } else if (metaDataType == "privacy") {
            if (start_time_privacy == 0) {
                start_time_privacy = startTime + 1;
            }
            string temp = "";
            std::getline(ss, temp, ',');
            int offduty_privacy;
            if (string_to_integer(temp, offduty_privacy) == false) {
                LOG_E(TAG, "csv_to_json:: failed to parse offduty privacy state");
                offduty_privacy = current_offduty_privacy;
            }

            std::getline(ss, temp, ',');
            int enhanced_privacy;
            if (string_to_integer(temp, enhanced_privacy) == false) {
                LOG_E(TAG, "csv_to_json:: failed to parse enhanced privacy state");
                enhanced_privacy = current_enhanced_privacy;
            }

            std::getline(ss, temp, ',');
            int fused_privacy;
            if (string_to_integer(temp, fused_privacy) == false) {
                LOG_E(TAG, "csv_to_json:: failed to parse fused privacy state");
                fused_privacy = current_fused_privacy;
            }

            std::getline(ss, temp, ',');
            int geofence_privacy;
            if (string_to_integer(temp, geofence_privacy) == false) {
                LOG_D(TAG, "csv_to_json:: failed to parse geofence privacy state, using current value");
                geofence_privacy = current_geofence_privacy;
            }

            if (offduty_privacy == current_offduty_privacy && enhanced_privacy == current_enhanced_privacy && 
                fused_privacy == current_fused_privacy && geofence_privacy == current_geofence_privacy) {
                //No change in privacy state
                privacy_sec_count++;
                continue;
            }

            // store privacy states for the partial video files
            // Priority: geofence > offduty > enhanced > fused
            privacy_state_t priv_state;
            if (geofence_privacy == PRIVACY_ON) {
                priv_state.event_state = PRIVACY_ON;
                priv_state.privacy_reason = REASON_GEOFENCE;
            } else if (offduty_privacy == PRIVACY_ON) {
                priv_state.event_state = PRIVACY_ON;
                priv_state.privacy_reason = REASON_OFFDUTY;
            } else if (enhanced_privacy == PRIVACY_ON) {
                priv_state.event_state = PRIVACY_OFF;
                priv_state.privacy_reason = REASON_ENHANCED;
            } else if (fused_privacy == PRIVACY_ON) {
                priv_state.event_state = PRIVACY_ON;
                priv_state.privacy_reason = REASON_SPEED; // For partial files, considering all speed/ignition/button based privacy as REASON_SPEED
            } else {
                priv_state.event_state = PRIVACY_OFF;
                priv_state.privacy_reason = REASON_NO_PRIVACY;
            }

            privacy_sec_count++;
            int64_t event_time = 0;
            if (device_mode_global_partial.individual_states_len == 0) {
                event_time = start_time_privacy;
            } else {
                event_time = start_time_privacy + (privacy_sec_count*1000);
            }

            priv_state.event_time_epoch = event_time;
            priv_state.event_time_monotonic = event_time;

            int privacy_switch_count = device_mode_global_partial.individual_states_len;
            device_mode_global_partial.individual_states[privacy_switch_count] = priv_state;
            device_mode_global_partial.individual_states_len++;

            if (current_offduty_privacy == -1) {
                device_mode_global_partial.privacy_status_offduty = offduty_privacy;
            } else if (device_mode_global_partial.privacy_status_offduty != offduty_privacy) {
                device_mode_global_partial.privacy_status_offduty = PRIVACY_MIXED;
            }

            if (current_geofence_privacy == -1) {
                device_mode_global_partial.privacy_status_geofence = geofence_privacy;
            } else if (device_mode_global_partial.privacy_status_geofence != geofence_privacy) {
                device_mode_global_partial.privacy_status_geofence = PRIVACY_MIXED;
            }

            if (current_fused_privacy == -1) {
                device_mode_global_partial.privacy_status = priv_state.event_state;
            } else if (device_mode_global_partial.privacy_status != priv_state.event_state) {
                device_mode_global_partial.privacy_status = PRIVACY_MIXED;
            }

            current_offduty_privacy = offduty_privacy;
            current_geofence_privacy = geofence_privacy;
            current_enhanced_privacy = enhanced_privacy;
            current_fused_privacy = fused_privacy;
            
        } else if (metaDataType == "outward_privacy") {
            string temp = "";
            std::getline(ss, temp, ',');

            if (is_number(temp)) {
                int privacy;
                string_to_integer(temp, privacy);
                device_mode_global_partial.partial_privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_FRONT] = privacy;
                LOG_I(TAG, "outward cam privacy from partial file : %d", privacy);
            }
        } else if (metaDataType == "inward_privacy") {
            string temp = "";
            std::getline(ss, temp, ',');

            if (is_number(temp)) {
                int privacy;
                string_to_integer(temp, privacy);
                device_mode_global_partial.partial_privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_BACK] = privacy;
                LOG_I(TAG, "inward cam privacy from partial file : %d", privacy);
            }
        } else if (metaDataType == "left_privacy") {
            string temp = "";
            std::getline(ss, temp, ',');

            if (is_number(temp)) {
                int privacy;
                string_to_integer(temp, privacy);
                device_mode_global_partial.partial_privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_LEFT] = privacy;
                LOG_I(TAG, "left cam privacy from partial file : %d", privacy);
            }
        } else if (metaDataType == "right_privacy") {
            string temp = "";
            std::getline(ss, temp, ',');

            if (is_number(temp)) {
                int privacy;
                string_to_integer(temp, privacy);
                device_mode_global_partial.partial_privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_RIGHT] = privacy;
                LOG_I(TAG, "right cam privacy from partial file : %d", privacy);
            }
        } else if (metaDataType == "ext_cam_privacy") {
            string temp = "";
            std::getline(ss, temp, ',');

            if (is_number(temp)) {
                int privacy;
                string_to_integer(temp, privacy);
                device_mode_global_partial.partial_privacy_params.ext_cam_privacy = privacy;
                LOG_I(TAG, "ext cam privacy from partial file : %d", privacy);
            }
        } else if (metaDataType == "ext_cam_audio_privacy") {
            string temp = "";
            std::getline(ss, temp, ',');

            if (is_number(temp)) {
                int privacy;
                string_to_integer(temp, privacy);
                device_mode_global_partial.partial_privacy_params.ext_cam_audio_privacy = privacy;
                LOG_I(TAG, "ext_cam_audio privacy from partial file : %d", privacy);
            }
        } else if (metaDataType == "driveri_audio_privacy") {
            string temp = "";
            std::getline(ss, temp, ',');

            if (is_number(temp)) {
                int privacy;
                string_to_integer(temp, privacy);
                device_mode_global_partial.partial_privacy_params.driveri_audio_privacy = privacy;
                LOG_I(TAG, "driveri_audio privacy from partial file : %d", privacy);
            }
        } else if (metaDataType == "gps_privacy") {
            string temp = "";
            std::getline(ss, temp, ',');

            if (is_number(temp)) {
                int privacy;
                string_to_integer(temp, privacy);
                device_mode_global_partial.partial_privacy_params.gps_privacy = privacy;
                LOG_I(TAG, "gps privacy from partial file : %d", privacy);
            }
        } else if (metaDataType == "personal_privacy") {
            string temp = "";
            std::getline(ss, temp, ',');

            if (is_number(temp)) {
                int privacy;
                string_to_integer(temp, privacy);
                device_mode_global_partial.partial_privacy_params.off_duty_privacy = privacy;
                LOG_I(TAG, "personal privacy from partial file : %d", privacy);
            }
        } else if (metaDataType == "geofence_privacy") {
            string temp = "";
            std::getline(ss, temp, ',');

            if (is_number(temp)) {
                int privacy;
                string_to_integer(temp, privacy);
                device_mode_global_partial.partial_privacy_params.geofence_privacy = privacy;
                LOG_I(TAG, "geofence privacy from partial file : %d", privacy);
            }
        } else if (metaDataType == "enhanced_privacy") {
            string temp = "";
            std::getline(ss, temp, ',');

            if (is_number(temp)) {
                int privacy;
                string_to_integer(temp, privacy);
                device_mode_global_partial.partial_privacy_params.enhanced_privacy = privacy;
                LOG_I(TAG, "enhanced privacy from partial file : %d", privacy);
            }
        } else if (metaDataType == "save_user_alert_video") {
            string temp = "";
            std::getline(ss, temp, ',');

            if (is_number(temp)) {
                int save_user_alert_video;
                string_to_integer(temp, save_user_alert_video);
                device_mode_global_partial.partial_privacy_params.save_user_alert_video = save_user_alert_video;
                LOG_I(TAG, "save_user_alert_video from partial file : %d", save_user_alert_video);
            }
        }  else if (metaDataType == "upload_video_outward") {
            string temp = "";
            std::getline(ss, temp, ',');

            if (is_number(temp)) {
                int upload_vid;
                string_to_integer(temp, upload_vid);
                device_mode_global_partial.partial_privacy_params.upload_video[DEVICE_CAMERA_POSITION_FRONT] = upload_vid;
                LOG_I(TAG, "upload_video_outward from partial file : %d", upload_vid);
            }
        } else if (metaDataType == "upload_video_inward") {
            string temp = "";
            std::getline(ss, temp, ',');

            if (is_number(temp)) {
                int upload_vid;
                string_to_integer(temp, upload_vid);
                device_mode_global_partial.partial_privacy_params.upload_video[DEVICE_CAMERA_POSITION_BACK] = upload_vid;
                LOG_I(TAG, "upload_video_inward from partial file : %d", upload_vid);
            }
        } else if (metaDataType == "upload_video_left") {
            string temp = "";
            std::getline(ss, temp, ',');

            if (is_number(temp)) {
                int upload_vid;
                string_to_integer(temp, upload_vid);
                device_mode_global_partial.partial_privacy_params.upload_video[DEVICE_CAMERA_POSITION_LEFT] = upload_vid;
                LOG_I(TAG, "upload_video_left from partial file : %d", upload_vid);
            }
        } else if (metaDataType == "upload_video_right") {
            string temp = "";
            std::getline(ss, temp, ',');

            if (is_number(temp)) {
                int upload_vid;
                string_to_integer(temp, upload_vid);
                device_mode_global_partial.partial_privacy_params.upload_video[DEVICE_CAMERA_POSITION_RIGHT] = upload_vid;
                LOG_I(TAG, "upload_video_right from partial file : %d", upload_vid);
            }
        } else if (metaDataType == "upload_video_ext_cam") {
            string temp = "";
            std::getline(ss, temp, ',');

            if (is_number(temp)) {
                int upload_vid;
                string_to_integer(temp, upload_vid);
                device_mode_global_partial.partial_privacy_params.upload_video_ext_cam = upload_vid;
                LOG_I(TAG, "upload_video_ext_cam from partial file : %d", upload_vid);
            }
        } else if (metaDataType == "ignition_status") {
           string temp = "";
           std::getline(ss,temp,',');
           if (temp == "1") {
               ignition_on_duration++;
           }
        } else {
            string temp = "";
            std::getline(ss, temp, ',');
            if (metaDataType == "Voltage in Volts" || metaDataType == "systemUpTime" || metaDataType == "serviceUpTime" ||
            metaDataType == "audioEnable" || metaDataType == "startTime" || metaDataType == "cameras" || metaDataType == "startTimeRawMicro") {
                if (metaDataType == "Voltage in Volts") {
                    float volt_temp = 0.0;
                    string_to_float(temp, volt_temp);
                    if (volt_temp > 0)
                        volt = volt_temp;
                    LOG_I(TAG, "Voltage in Volts came, and volt_temp is %f", volt_temp);
                    continue;
                }
                if (!is_number(temp)){
                    continue;
                }
                if (metaDataType == "startTime") {
                    int64_t startTime_temp = 0;
                    string_to_int64(temp, startTime_temp);
                    if(startTime_temp > 0 ) { startTime = startTime_temp; }
                    continue;
                }
                if (metaDataType == "startTimeRawMicro") {
                    int64_t startTimeRawMicro_temp = 0;
                    string_to_int64(temp, startTimeRawMicro_temp);
                    if (startTimeRawMicro_temp > 0 ) { startTimeRawMicro = startTimeRawMicro_temp; }
                    startTimeRawMilli = startTimeRawMicro / 1000;
                    continue;
                }
                str_stream << "\"" << metaDataType << "\"" <<": "  << temp << PRI_COMMA ;
            }
            else {
                if(metaDataType == "driverId") {
                    str_stream << "\"" << metaDataType << "\"" <<": " << PRI_Q_COND (temp, temp.length()) << PRI_COMMA;
                }
                else if (metaDataType == "driverId_v2") {
                    str_stream << "\"" << metaDataType << "\"" << ": " << ss.str() << PRI_COMMA;
                }
                else {
                    str_stream << "\"" << metaDataType << "\"" <<": " << "\"" << temp << "\"" << PRI_COMMA ;
                }
            }
            size_t meta_found = other_string.find(metaDataType);
            if (meta_found == string::npos) {
                other_string += str_stream.str();
            }
        }
        line_count++ ;
    }

    device_mode_global_partial.privacy_status = device_mode_global_partial.individual_states[0].event_state;
    for (int i = 1; i < device_mode_global_partial.individual_states_len; i++) {
        if (device_mode_global_partial.individual_states[i].event_state != device_mode_global_partial.privacy_status) {
            device_mode_global_partial.privacy_status = PRIVACY_MIXED;
            break;
        }
    }
    /*

    if (has_usr_alert && device_mode_global_partial.partial_privacy_params.save_user_alert_video && \
        device_mode_global_partial.privacy_status_geofence == PRIVACY_OFF) {
        //Ignore privacy. keep the video.
        device_mode_global_partial.privacy_status = PRIVACY_OFF;
        device_mode_global_partial.privacy_status_offduty = PRIVACY_OFF;

    }
        */
    partial_video_len_sec = privacy_sec_count+1;
    LOG_I(TAG, "privacy: PrivacyStatus: %d", device_mode_global_partial.privacy_status);

    str_stream.str("");
    str_stream << "\"" << "privacymode" << "\"" << ": \"" << to_string(device_mode_global_partial.privacy_status) << "\"" << PRI_COMMA;
    other_string += str_stream.str();

    if (num_of_irled_states > 1) {
        overall_irled_status = IRLED_MIXED;
    } else if (num_of_irled_states == 1){
        overall_irled_status = current_irled_status ? IRLED_ON : IRLED_OFF;
    }
    LOG_I(TAG, "irled_status: %d", overall_irled_status);

    if (valid_file == false) {
        return valid_file;
    }
    str_stream.str("");
    str_stream << "\"" << "Voltage in Volts" << "\"" <<": "  << volt << PRI_COMMA ;
    other_string += str_stream.str();

    /*This is to update the timestamps for event_time_monotonic for all individual_states[i]
    which was previously coming 1 which was not correct. */
    if (start_time_privacy == 1) {
        for (int i = 0 ; i < device_mode_global_partial.individual_states_len ; i++) {
            device_mode_global_partial.individual_states[i].event_time_monotonic += startTimeRawMilli;
        }
    }

    str_stream.str("");
    str_stream << "\"" << "ignition_duration" << "\"" << ": \"" << to_string(ignition_on_duration) << "\"" << PRI_COMMA;
    other_string += str_stream.str();
    str_stream.str("");
    str_stream << "\"" << "startTime" << "\"" <<": "  << startTime << PRI_COMMA ;
    other_string += str_stream.str();
    str_stream.str("");
    if(startTime > endTime ) {
        LOG_I(TAG, "startTime %lld: > endTime: %lld", startTime, endTime);
        endTime = startTime;
    }
    str_stream << "\"" <<"endTime" << "\"" <<": "  << endTime << ',' ;
    other_string += str_stream.str();

    str_stream.str("");
    str_stream << "\"" <<"metadataStatus" << "\"" <<": "  << "\"" <<"partial" << "\""  << ',' ;
    other_string += str_stream.str();
    if(gps_string != " " ){
        gps_string.pop_back();
    }
    if(imu_string != " " ){
        imu_string.pop_back();
    }
    if (usralert_string != " " ) {
        usralert_string.pop_back();
    }

    if (irled_states_string != " " ) {
        irled_states_string.pop_back();
    }

    obd_str_stream.str("");
    if(data.empty()){
        obd_str_stream << "\"status\": \"" << obd_status.str() << "\"" << endl;
    }
    else
    {
        str_stream.str("");
        for (auto it = data.begin(); it != data.end(); ++it)
        {
            int spn = 0;
            stringstream spn_id(it->first);
            spn_id >> spn;
            str_stream << final_delim << endl
                       << "\"0x" << std::hex << spn << "\":[" << it->second.str() << "]";
            final_delim = ",";
        }
        obd_str_stream << "\"service_up_time\":" << start_time << "," << endl
                       << "\"status\":" << CAN_STATUS_OK << "," << str_stream.str();
    }
    obd_string = obd_str_stream.str();
    if(ublox_string != " " ){
        ublox_string.pop_back();
    }
    if(gps_pps_string != " " ){
        gps_pps_string.pop_back();
    }
    if(pps_start_index_string == " " ){
	    pps_start_index_string = std::to_string(-1);
    }
    if(gps_start_index_string == " " ){
	    gps_start_index_string = std::to_string(-1);
    }
    if(ublox_constellation_string != " " ){
        ublox_constellation_string.pop_back();
    }
    if(frameinfo_string != " " ){
        frameinfo_string.pop_back();
    }
    if(imu_temp_string != " " ){
        imu_temp_string.pop_back();
    }
    if(imu_data_string != " " ){
        imu_data_string.pop_back();
    }
    inputFile.close();

    str_stream.str("");
    str_stream << "{" << endl \
               <<  other_string << endl \
               << "\"videoMetaData\" : ["        << endl << gps_string                 << "], " << endl \
               << "\"vehicle_data\" : {"         << endl << obd_string                 << "}, " << endl \
               << "\"sensorMetaData\" : ["       << endl << imu_string                 << "], " << endl;

    if (overall_irled_status != -1) {
        str_stream << "\"irled_status\" : "              << overall_irled_status       << ","   << endl \
                   << "\"irled_states\" : ["     << endl << irled_states_string        << "], " << endl;
    }

    str_stream << "\"user_generated_alert\" : [" << endl << usralert_string            << "], " << endl \
               << "\"ppsData\" : ["              << endl << gps_pps_string             << "], " << endl \
               << "\"GPS_to_PPS_startidx\" : "           << gps_start_index_string     << ","   << endl \
               << "\"PPS_startidx\" : "                  << pps_start_index_string     << ", "  << endl \
               << "\"ublox_constellation\" : ["  << endl << ublox_constellation_string << "], " << endl \
               << "\"frameData\" : ["            << endl << frameinfo_string           << "], " << endl \
               << "\"imuTemp\" : ["              << endl << imu_temp_string            << "], " << endl \
               << "\"imuData\" : ["              << endl << imu_data_string            << "], " << endl \
               << "\"ubloxData\" : ["            << endl << ublox_string               << "] "  << endl \
               << "}" << endl;

    //load and see if the given file is partial file or not
    json_error_t error;
    json_t* root = json_loads(str_stream.str().c_str(), 0, &error);
    if(!root){
        LOG_E(TAG, "%d: Not a valid json string:%s", __LINE__, str_stream.str().c_str());
        LOG_E(TAG, "Error: %s", error.text );
        return false;
    }
    json_decref(root);

    ofstream outputFile(destination_path, std::ofstream::out);
    outputFile << str_stream.str();
    outputFile.close();
    LOG_I(TAG, "Total rows in %s: %d", source_path.c_str(), line_count);
    printf("Final json from partial file: \n %s \n", str_stream.str().c_str());
    return valid_file;
}

bool Genmeta::convert_header_json(genmeta_header_t header, string &header_string)
{
	std::stringstream str_stream;
	std::stringstream str_stream_tmp;

	LOG_I(TAG, "start_time %lld, inwardStartTime %lld, ptsStartTime %lld, inwardPtsStartTime %lld, dmsStartTime %lld, dmsPtsStartTime %lld",
	   header.start_time, header.inward_start_time, header.ptsStartTime, header.inward_ptsStartTime, header.dms_start_time, header.dms_ptsStartTime);

	LOG_I(TAG, "start_time_ld %lld, inwardStartTime_ld %lld, dmsStartTime_ld %lld, ptsStartTime_ld %lld, inwardPtsStartTime_ld %lld, dmsPtsStartTime_ld %lld",
	   header.start_time_ld, header.inward_start_time_ld, header.dms_start_time_ld, header.ptsStartTime_ld, header.inward_ptsStartTime_ld, header.dms_ptsStartTime_ld);

	str_stream_tmp	<< head[HEADER_TIME]         PRI_Q  << header.time  PRI_Q ;
	append_if_json_string_no_comma(str_stream, str_stream_tmp);

	str_stream  << head[HEADER_VOLT]     << header.volts ;

	str_stream_tmp  << head[HEADER_DEVICEID]     PRI_Q  << header.device_id  PRI_Q ;
	append_if_json_string(str_stream, str_stream_tmp);

	str_stream << head[HEADER_CAMSENABLED] << header.cams_enabled;

	str_stream_tmp	<< head[HEADER_PROTOCOL_INFO] PRI_Q << header.protocol_info PRI_Q ;
	append_if_json_string(str_stream, str_stream_tmp);

	str_stream_tmp  << head[HEADER_VIN] PRI_Q << header.vin PRI_Q ;
	append_if_json_string(str_stream, str_stream_tmp);

	str_stream_tmp  << head[HEADER_CAN_FW_VER] PRI_Q << header.can_firmware_ver PRI_Q ;
	append_if_json_string(str_stream, str_stream_tmp);

    str_stream_tmp  << head[HEADER_CAN_SN] PRI_Q << header.can_sn PRI_Q ;
    append_if_json_string(str_stream, str_stream_tmp);

    str_stream_tmp  <<head[HEADER_CAN_MODEL] PRI_Q << header.can_model PRI_Q ;
    append_if_json_string(str_stream, str_stream_tmp);

	str_stream_tmp  << head[HEADER_CAN_STATUS] << header.can_status;

	str_stream_tmp  << head[HEADER_CAN_SRC] << header.can_src;

    str_stream_tmp  << head[HEADER_ENGINE_STATUS] PRI_Q << header.engine_status PRI_Q ;
	append_if_json_string(str_stream, str_stream_tmp);
	str_stream_tmp  << head[HEADER_DRIVERID]     << PRI_Q_COND (header.driver_id, header.driver_id.length()) ;
	append_if_json_string(str_stream, str_stream_tmp);

    if ((header.driver_id_v2) != "") {
        str_stream_tmp  << head[HEADER_DRIVERID_V2]<< header.driver_id_v2 ;
        append_if_json_string(str_stream, str_stream_tmp);
    }

	str_stream_tmp  <<  head[HEADER_SESSIONID]    PRI_Q  << header.session_id  PRI_Q ;
	append_if_json_string(str_stream, str_stream_tmp);

	str_stream_tmp  << head[HEADER_DEVICEMODES]  << PRI_OPEN_CURLY PRI_Q << privacy_metadata_entry PRI_Q << PRI_COL << header.privacy_mode << PRI_COMMA\
         PRI_Q << privacy_offduty_metadata_entry PRI_Q << PRI_COL << header.privacy_mode_offduty << PRI_COMMA\
         PRI_Q << privacy_geofence_metadata_entry PRI_Q << PRI_COL << header.privacy_mode_geofence << PRI_COMMA\
    	 PRI_Q << engine_idle_metadata_entry  PRI_Q << PRI_COL << header.idle_status << PRI_COMMA\
         PRI_Q << processing_mode_metadata_entry PRI_Q << PRI_COL << header.processing_mode;
	str_stream_tmp  << head[HEADER_disMode]  PRI_Q   << header.disMode  PRI_Q  << PRI_COMMA\
    	 PRI_Q << ignition_status_metadata_entry PRI_Q << PRI_COL << header.ignition_status \
         << privacy_events  << header.privacyModeEvents << PRI_CLOSE_CURLY ;

	printf("json############## \n %s \n", str_stream_tmp.str().c_str());

	append_if_json_string(str_stream, str_stream_tmp);

	str_stream_tmp  << head[HEADER_DEVICETYPE]   PRI_Q  << header.devicetype  PRI_Q ;
	append_if_json_string(str_stream, str_stream_tmp);

	str_stream_tmp  << head[HEADER_UDID]   PRI_Q  << header.udid_string  PRI_Q ;
	append_if_json_string(str_stream, str_stream_tmp);

	str_stream_tmp  << head[HEADER_SESSIONCOUNT]   PRI_Q  << header.sessionCount_string  PRI_Q ;
	append_if_json_string(str_stream, str_stream_tmp);

	str_stream_tmp  << head[HEADER_RTCVALID]   PRI_Q  << header.rtcValid_string  PRI_Q ;
	LOG_I (TAG,"rtcValid: %s", header.rtcValid_string.c_str() );
	append_if_json_string(str_stream, str_stream_tmp);

	str_stream_tmp  << head[HEADER_RTCJUMPFROM]   PRI_Q  << header.rtc_jump_from_string  PRI_Q ;
	append_if_json_string(str_stream, str_stream_tmp);

	str_stream_tmp  << head[HEADER_RTCJUMPTO]   PRI_Q  << header.rtc_jump_to_string  PRI_Q ;
	append_if_json_string(str_stream, str_stream_tmp);

	str_stream_tmp  << head[HEADER_DEVICESUBTYPE]    PRI_Q  << header.devicesubtype  PRI_Q ;
	append_if_json_string(str_stream, str_stream_tmp);

	str_stream_tmp  << head[HEADER_VEHCLASS]     PRI_Q  << header.vehclass  PRI_Q ;
	append_if_json_string(str_stream, str_stream_tmp);

    str_stream_tmp << head[HEADER_COPY_STATUS_CAM] PRI_Q << header.copy_status_cam PRI_Q;
    append_if_json_string(str_stream, str_stream_tmp);

	str_stream  << head[HEADER_AUDIOENABLE]         << header.audio_enable ;

	str_stream_tmp  << head[HEADER_APPVRE]       PRI_Q  << header.app_ver  PRI_Q ;
	append_if_json_string(str_stream, str_stream_tmp);

	str_stream  << head[HEADER_SYSTEMUPTIME]        << header.system_uptime ;

	str_stream  << head[HEADER_SERVICEUPTIME]       << header.service_uptime ;

	str_stream  << head[HEADER_VERTANGL]     PRI_Q  << header.vertical_angle PRI_Q ;

	str_stream  << head[HEADER_HORZANGL]     PRI_Q  << header.horizontal_angle PRI_Q ;

	str_stream_tmp  << head[HEADER_PREVVIDNAME]      PRI_Q  << header.prevVideoName  PRI_Q ;
	append_if_json_string(str_stream, str_stream_tmp);

	str_stream_tmp  << head[HEADER_VIDNAME]          PRI_Q  << header.video_name  PRI_Q ;
	append_if_json_string(str_stream, str_stream_tmp);

	str_stream_tmp  << head[HEADER_NEXTVIDNAME]      PRI_Q  << header.nextVideoName  PRI_Q ;
	append_if_json_string(str_stream, str_stream_tmp);

	str_stream_tmp  << head[HEADER_VEHCLIC]      PRI_Q  << header.vehicle_id  PRI_Q ;
	append_if_json_string(str_stream, str_stream_tmp);

	str_stream_tmp  << head[HEADER_SPEEDUNIT]    PRI_Q  << header.speed_unit  PRI_Q ;
	append_if_json_string(str_stream, str_stream_tmp);

	str_stream  << head[HEADER_TRIPNUM]      PRI_Q  << header.trip_no PRI_Q ;

	str_stream  << head[HEADER_PARTNUM]      PRI_Q  << header.part_no PRI_Q ;

	str_stream  << head[HEADER_STRTTIME]            << header.start_time ;

	str_stream  << head[HEADER_STRTTIME_LD]         << header.start_time_ld ;

	str_stream  << head[HEADER_STRTTIME_DP]         << header.start_time_dp ;

	str_stream  << head[HEADER_STRTTIME_MICRO]      << header.start_time_micro;

	str_stream  << head[HEADER_STRTTIMERAW_MICRO]   << header.start_time_raw_micro;

	str_stream  << head[HEADER_OFFSET]              << header.offset ;

	str_stream  << head[HEADER_GPSSTARTTIME]        << header.gps_start_time ;

	str_stream  << head[HEADER_GPSENDTIME]          << header.gps_end_time ;

	str_stream  << head[HEADER_ENDTIME]             << header.end_time ;

	str_stream  << head[HEADER_ENDTIME_MICRO]       << header.end_time_micro ;

	str_stream  << head[HEADER_ENDTIME_RAW_MICRO]   << header.end_time_raw_micro ;

	str_stream << head[HEADER_LPW_NO_RECORD]        << header.lpw_no_record ;

	str_stream  << head[HEADER_INWARD_STARTTIME]    << header.inward_start_time ;

	str_stream  << head[HEADER_INWARD_STARTTIME_LD] << header.inward_start_time_ld ;

	str_stream  << head[HEADER_PTSSTARTTIME]        << header.ptsStartTime ;

	str_stream  << head[HEADER_PTSSTARTTIME_LD]     << header.ptsStartTime_ld;

	str_stream  << head[HEADER_PTSSTARTTIME_DP]     << header.ptsStartTime_dp;

	str_stream  << head[HEADER_INWARD_PTSSTARTTIME] << header.inward_ptsStartTime ;

	str_stream << head[HEADER_INWARD_PTSSTARTTIME_LD] << header.inward_ptsStartTime_ld;

	str_stream  << head[HEADER_DMS_STARTTIME]       << header.dms_start_time ;

	str_stream  << head[HEADER_DMS_PTSSTARTTIME]    << header.dms_ptsStartTime ;

	str_stream  << head[HEADER_DMS_STARTTIME_LD]       << header.dms_start_time_ld ;

	str_stream  << head[HEADER_DMS_PTSSTARTTIME_LD]    << header.dms_ptsStartTime_ld ;

	str_stream  << head[HEADER_OUTWARD_CAM_PRIVACY] << header.outward_cam_privacy ;

	str_stream  << head[HEADER_INWARD_CAM_PRIVACY]  << header.inward_cam_privacy ;

	str_stream  << head[HEADER_VALID_GPS_COUNT]     << valid_GPS_entries ;

	str_stream_tmp  << head[HEADER_driverInvariantSession] PRI_Q << header.driverInvariantSession PRI_Q ;

	str_stream  << head[HEADER_IRLED_STATUS]  << header.irled_status ;

	str_stream  << head[HEADER_IRLED_STATES]  << header.irled_states ;

	append_if_json_string(str_stream, str_stream_tmp);

	if ((header.network_info) != "")
	{
		str_stream_tmp  << head[HEADER_NETWORKINFO]<< header.network_info ;
		append_if_json_string(str_stream, str_stream_tmp);
	}

	header_string = str_stream.str();

	printf("final json ############## \n %s \n", header_string.c_str());
	return true;
}

bool Genmeta::convert_gpsarray_json(string &gps_string, int flipflop)
{
	std::stringstream str_stream;
	str_stream << std::fixed  << std::setprecision(7); // if needed set it to IMU aswell

	int i = 0;
	int gps_arr_len = this->ctx->gps_data_collection[flipflop].size();
	valid_GPS_entries = 0;

	if(gps_arr_len == 0)
	{
		LOG_D(TAG, "gps_arr_len is 0");
		return false;
	}

	while (i < (gps_arr_len - 1)) {

		Gps::gps_metadata_t &gpsdata = this->ctx->gps_data_collection[flipflop][i];

		str_stream << "{" \
			<< gpsstr[GPSSTR_LAT]       << gpsdata.gps_data.latitude \
			<< gpsstr[GPSSTR_LONG]      << gpsdata.gps_data.longitude\
			<< gpsstr[GPSSTR_ALTI]      << gpsdata.gps_data.altitude \
			<< gpsstr[GPSSTR_ALTI_MSL]  << gpsdata.altitudeMSL \
			<< gpsstr[GPSSTR_ACCU]      << gpsdata.gps_data.accuracy \
			<< gpsstr[GPSSTR_BEAR]      << gpsdata.gps_data.bearing \
			<< gpsstr[GPSSTR_SPEED]     << gpsdata.gps_data.speed \
			<< gpsstr[GPSSTR_TIME]      << gpsdata.gps_data.timestamp \
			<< gpsstr[GPSSTR_RAW_TIME]  << gpsdata.raw_time_micro \
			<< gpsstr[GPSSTR_VALIDITY]  << gpsdata.gps_data.valid << "} ,";

		valid_GPS_entries += gpsdata.gps_data.valid;
		i++;
	}

	Gps::gps_metadata_t &gpsdata = this->ctx->gps_data_collection[flipflop][i];
	str_stream << "{" \
		<< gpsstr[GPSSTR_LAT]		<< gpsdata.gps_data.latitude \
		<< gpsstr[GPSSTR_LONG]		<< gpsdata.gps_data.longitude\
		<< gpsstr[GPSSTR_ALTI]		<< gpsdata.gps_data.altitude \
		<< gpsstr[GPSSTR_ALTI_MSL]	<< gpsdata.altitudeMSL \
		<< gpsstr[GPSSTR_ACCU]		<< gpsdata.gps_data.accuracy \
		<< gpsstr[GPSSTR_BEAR]		<< gpsdata.gps_data.bearing \
		<< gpsstr[GPSSTR_SPEED]	<< gpsdata.gps_data.speed \
		<< gpsstr[GPSSTR_TIME]		<< gpsdata.gps_data.timestamp \
		<< gpsstr[GPSSTR_RAW_TIME]	<< gpsdata.raw_time_micro \
		<< gpsstr[GPSSTR_VALIDITY]	<< gpsdata.gps_data.valid << "} " ;

	valid_GPS_entries += gpsdata.gps_data.valid;

	gps_string = str_stream.str();
	return true;
}

bool Genmeta::convert_ppsarray_json(string &gps_pps_string, int flipflop)
{

    std::stringstream str_stream;
    str_stream << std::fixed  << std::setprecision(7); // if needed set it to IMU aswell

    int i = 0;
    int pps_arr_len = this->ctx->gps_pps_data_collection[flipflop].size();

    if(pps_arr_len == 0)
    {
        LOG_D(TAG, "pps_arr_len is 0");
        return false;
    }

    while(i<(pps_arr_len-1))
    {

        Gps::gps_pps_data_t &ppsdata = this->ctx->gps_pps_data_collection[flipflop][i];
        str_stream <<  ppsdata.pps_raw_time << ",";
        i++;
    }

    Gps::gps_pps_data_t &ppsdata = this->ctx->gps_pps_data_collection[flipflop][i];
    str_stream <<  ppsdata.pps_raw_time ;
    gps_pps_string = str_stream.str();
    return true;
}

bool Genmeta::convert_ubloxarray_json(string &ublox_string, int flipflop)
{
    std::stringstream str_stream;
    int i = 0;
    int ublox_arr_len = this->ctx->ublox_data_collection[flipflop].size();

    if(ublox_arr_len == 0)
    {
        LOG_D(TAG, "ublox_arr_len is 0");
        return false;
    }

    while(i<(ublox_arr_len-1))
    {

        Ublox::ublox_data_t &ubloxdata = this->ctx->ublox_data_collection[flipflop][i];
        str_stream << "{"\
            << ubloxstr[UBLOXSTR_SYSTEM_TIME] << ubloxdata.system_time \
            << ubloxstr[UBLOXSTR_CLASS] << ubloxdata.Class \
            << ubloxstr[UBLOXSTR_ID] << ubloxdata.id \
            << ubloxstr[UBLOXSTR_LENGTH] << ubloxdata.length \
            << ubloxstr[UBLOXSTR_PAYLOAD] << "\"" << ubloxdata.payload <<"\""\
            << "} ,";
        i++;
    }

    Ublox::ublox_data_t &ubloxdata = this->ctx->ublox_data_collection[flipflop][i];
    str_stream << "{"\
        << ubloxstr[UBLOXSTR_SYSTEM_TIME] << ubloxdata.system_time \
        << ubloxstr[UBLOXSTR_CLASS] << ubloxdata.Class \
        << ubloxstr[UBLOXSTR_ID] << ubloxdata.id \
        << ubloxstr[UBLOXSTR_LENGTH] << ubloxdata.length \
        << ubloxstr[UBLOXSTR_PAYLOAD] << "\"" << ubloxdata.payload <<"\""\
        << "} ";
    ublox_string = str_stream.str();
    return true;
}

bool Genmeta::convert_ublox_constellarray_json(string &ublox_constell_string, int flipflop)
{
    std::stringstream str_stream;
    int i = 0;
    Ublox::ublox_constellation_data_t &ubloxdata = this->ctx->ublox_constell_data_collection;

    str_stream << "{"\
        << ublox_constellstr[UBLOXSTR_EN_GPS] << ubloxdata.enable_GPS \
        << ublox_constellstr[UBLOXSTR_EN_SBAS] << ubloxdata.enable_SBAS \
        << ublox_constellstr[UBLOXSTR_EN_GALILEO] << ubloxdata.enable_Galileo \
        << ublox_constellstr[UBLOXSTR_EN_BEIDOU] << ubloxdata.enable_BeiDou \
        << ublox_constellstr[UBLOXSTR_EN_IMES] << ubloxdata.enable_IMES \
        << ublox_constellstr[UBLOXSTR_EN_QZSS] << ubloxdata.enable_QZSS \
        << ublox_constellstr[UBLOXSTR_EN_GLONASS] << "\"" << ubloxdata.enable_GLONASS <<"\""\
        << "} ";
    ublox_constell_string = str_stream.str();
    return true;

}

bool Genmeta::convert_gps_pps_start_index_json (string &GPS_to_PPS_startidx, string &PPS_startidx, int flipflop)
{

    int gps_arr_len = this->ctx->gps_data_collection[flipflop].size();
    int pps_arr_len = this->ctx->gps_pps_data_collection[flipflop].size();
    if (gps_arr_len == 0 || pps_arr_len == 0) {
        LOG_E (TAG, "Error: gps_arr_len: %d, pps_arr_len: %d", gps_arr_len, pps_arr_len);
        GPS_to_PPS_startidx = std::to_string (-1);
        PPS_startidx = std::to_string (-1);
        return false;
    }

    //Since we have synchronised recording of first PPS and GPS data (giving both same index of 0), gps_index of the first GPS data entry
    //will be same as corresponding PPS entry index.
    //Similarly, index of first PPS in the file will be same as pps_index of that entry.
    Gps::gps_metadata_t &gpsdata = this->ctx->gps_data_collection[flipflop][0];
    Gps::gps_pps_data_t &ppsdata = this->ctx->gps_pps_data_collection[flipflop][0];

    GPS_to_PPS_startidx = std::to_string (gpsdata.gps_index);
    PPS_startidx = std::to_string (ppsdata.pps_index);
    return true;
}

bool Genmeta::convert_imuarray_json(string &imu_data_str, string &sensor_metadata_str, string &imu_temperature, int flipflop)
{
    std::stringstream str_stream_imu_data, str_stream_sensor_metadata, str_stream_temperature;
    std::stringstream str_stream_temperory;
    std::string imu_type_str = "";
    int imu_idx = 0;
    int num_imu_entries = 0;

#ifdef BAGHEERA2
    int accel_arr_len = this->ctx->imu_accel_data_collection[flipflop].size();
    int gyro_arr_len = this->ctx->imu_gyro_data_collection[flipflop].size();
    int temp_arr_len = this->ctx->imu_temperature_data_collection[flipflop].size();

    if (accel_arr_len == 0 || gyro_arr_len == 0 || temp_arr_len == 0 ||
            (accel_arr_len != gyro_arr_len)) {
        LOG_E (TAG, "Error in IMU data collection sizes: accel: %d, gyro: %d, temperature: %d",
                accel_arr_len, gyro_arr_len, temp_arr_len);

        if ((accel_arr_len == 0) || (gyro_arr_len == 0)) {
            LOG_E (TAG, "No IMU data: Returning");
            return true;
        }
    }

    num_imu_entries = accel_arr_len;
    if (accel_arr_len != gyro_arr_len) {
        LOG_E (TAG, "Unexpected. Accel and Gyro has different number of entries: accel: %d\t gyro: %d", accel_arr_len, gyro_arr_len);
        num_imu_entries = min (num_imu_entries, gyro_arr_len);
    }

    while (imu_idx < (num_imu_entries-1)) {

        Imu::val_t &accel_data = (this->ctx->imu_accel_data_collection[flipflop][imu_idx]).imuData;
        Imu::val_t &gyro_data = (this->ctx->imu_gyro_data_collection[flipflop][imu_idx]).imuData;

        str_stream_temperory << "\"a_x\": " << accel_data.x << ", "
            << "\"a_y\": " << accel_data.y << ", "
            << "\"a_z\": " << accel_data.z << ", "
            << "\"g_x\": " << gyro_data.x << ", "
            << "\"g_y\": " << gyro_data.y << ", "
            << "\"g_z\": " << gyro_data.z << ", "
            << "\"ts\": " << accel_data.raw_time;

        str_stream_imu_data << "{" << str_stream_temperory.str() << "}, ";

        str_stream_temperory.clear();
        str_stream_temperory.str("");

        imu_type_str = "\"accelerometer\": ";
        str_stream_temperory << "\"  "
                        << accel_data.x << "  "
                        << accel_data.y << "  "
                        << accel_data.z << "   "
                        << static_cast <int64_t>(accel_data.clock_time / 1000) << "\"";

        str_stream_sensor_metadata
                    << "{" \
                    << imu_type_str
                    << str_stream_temperory.str() << "} ,";

        str_stream_temperory.clear();
        str_stream_temperory.str("");

        imu_type_str = "\"gyro\": ";
        str_stream_temperory << "\"  "
                        << gyro_data.x << "  "
                        << gyro_data.y << "  "
                        << gyro_data.z << "   "
                        << static_cast <int64_t>(gyro_data.clock_time / 1000) << "\"";

        str_stream_sensor_metadata
                    << "{" \
                    << imu_type_str
                    << str_stream_temperory.str() << "} ,";

        str_stream_temperory.clear();
        str_stream_temperory.str("");
        imu_idx++;
    }

    Imu::val_t &accel_data = (this->ctx->imu_accel_data_collection[flipflop][imu_idx]).imuData;
    Imu::val_t &gyro_data = (this->ctx->imu_gyro_data_collection[flipflop][imu_idx]).imuData;

    str_stream_temperory << "\"a_x\": " << accel_data.x << ", "
        << "\"a_y\": " << accel_data.y << ", "
        << "\"a_z\": " << accel_data.z << ", "
        <<  "\"g_x\": " << gyro_data.x << ", "
        <<  "\"g_y\": " << gyro_data.y << ", "
        <<  "\"g_z\": " << gyro_data.z << ", "
        << "\"ts\": " << accel_data.raw_time;

    str_stream_imu_data << "{" << str_stream_temperory.str() << "}";
    imu_data_str = str_stream_imu_data.str();

    str_stream_temperory.clear();
    str_stream_temperory.str("");

    imu_type_str = "\"accelerometer\": ";
    str_stream_temperory << "\"  "
        << accel_data.x << "  "
        << accel_data.y << "  "
        << accel_data.z << "   "
        << static_cast <int64_t>(accel_data.clock_time / 1000) << "\"";

    str_stream_sensor_metadata
        << "{" \
        << imu_type_str
        << str_stream_temperory.str() << "} ,";

    str_stream_temperory.clear();
    str_stream_temperory.str("");

    imu_type_str = "\"gyro\": ";
    str_stream_temperory << "\"  "
        << gyro_data.x << "  "
        << gyro_data.y << "  "
        << gyro_data.z << "   "
        << static_cast <int64_t>(gyro_data.clock_time / 1000) << "\"";

    str_stream_sensor_metadata
        << "{" \
        << imu_type_str
        << str_stream_temperory.str() << "}" ;

    sensor_metadata_str = str_stream_sensor_metadata.str();

    str_stream_temperature.clear();
    str_stream_temperature.str ("");
    str_stream_temperory.clear();
    str_stream_temperory.str("");

    int temp_idx = 0;
    while (temp_idx < (temp_arr_len - 1)) {

        Imu::val_t &imu_temp_data = (this->ctx->imu_temperature_data_collection[flipflop][temp_idx]).imuData;
        temp_idx++;

        str_stream_temperory << imu_temp_data.x << "," << imu_temp_data.raw_time;

        str_stream_temperature << "[" << str_stream_temperory.str() << "], " ;

        str_stream_temperory.clear();
        str_stream_temperory.str("");
    }
    if (temp_idx == (temp_arr_len-1)) {

        Imu::val_t &imu_temp_data = (this->ctx->imu_temperature_data_collection[flipflop][temp_idx]).imuData;

        str_stream_temperory << imu_temp_data.x << "," << imu_temp_data.raw_time;
        str_stream_temperature << "[" << str_stream_temperory.str() << "]" ;
    }
    imu_temperature = str_stream_temperature.str();
#elif KRAIT
	int imu_arr_len = this->ctx->imu_data_collection[flipflop].size();
	int imu_type = 0;

	if (imu_arr_len == 0) {
		LOG_E(TAG, "imu_arr_len is 0");
		return false;
	}

	while (imu_idx < imu_arr_len) {
		genmeta_imudata &imudata = this->ctx->imu_data_collection[flipflop][imu_idx];

		imu_type = imudata.type;
		if (imu_type == Imu::IMU_ACCEL) {
			imu_type_str = "\"accelerometer\": ";
		} else if (imu_type == Imu::IMU_GYRO) {
			imu_type_str = "\"gyro\": ";
		}

		str_stream_temperory << "\"  "
				<< imudata.imuData.x << "  "
				<< imudata.imuData.y << "  "
				<< imudata.imuData.z << "   "
				<< static_cast <int64_t>(imudata.imuData.clock_time / 1000) << "\"" ;

		str_stream_sensor_metadata
			<< "{" \
			<< imu_type_str
			<< str_stream_temperory.str() << "} ,";

		str_stream_temperory.clear();
		str_stream_temperory.str("");

		imu_idx++;
	}

	sensor_metadata_str = str_stream_sensor_metadata.str();
	sensor_metadata_str.pop_back();
#endif

	return true;
}

bool Genmeta::convert_fuel_report_json(string &fuel_report_string, int flipflop)
{
    if(fuel_report == 1){
        std::stringstream str_stream;
        string delim = "";
        int freport_arr_len = this->ctx->fuel_report_collection[flipflop].size();
        if(freport_arr_len == 0){
            LOG_I(TAG, "freport_arr_len is 0");
            can_status_t status = (can_status_updated == CAN_UNKNOWN_ERROR) ? CAN_STATUS_OK : can_status_updated;
            str_stream << "{ \"status\":" << status << "}" << endl;
        }
        else{
            for (int i=0; i<freport_arr_len; i++){
                Obd::fr_data_t &freportdata = this->ctx->fuel_report_collection[flipflop][i];
                uint64_t time = (freportdata.fuel_ts > freportdata.odo_ts) ? freportdata.fuel_ts : freportdata.odo_ts;
                str_stream << delim << endl << "{ \"time\":" << std::dec << time;
                str_stream << ",\"status\":" << CAN_STATUS_OK;
                str_stream << ",\"0x" << std::hex << freportdata.fuel_param << "\": [\"" << std::dec << time - freportdata.fuel_ts << " " << fixed << setprecision(OBD_DATA_PRECISION) << freportdata.fuel_value << "\"]";
                str_stream << ",\"0x" << std::hex << freportdata.odo_param << "\": [\"" << std::dec << time - freportdata.odo_ts << " " << fixed << setprecision(OBD_DATA_PRECISION) << freportdata.odo_value << "\"]";
                str_stream << ",\"engine_state\":" << freportdata.engine_state << "}";
                delim = ",";
            }
        }
        fuel_report_string = str_stream.str();
        return true;
    }
}

bool Genmeta::convert_idling_report_json(string &idling_report_string, int flipflop)
{

    if(idling_report){
        std::stringstream str_stream;
        string delim = "";
        int ireport_arr_len = this->ctx->idling_report_collection[flipflop].size();
        can_status_t status = (can_status_updated == CAN_UNKNOWN_ERROR) ? CAN_STATUS_OK : can_status_updated;
        if(ireport_arr_len == 0){
            LOG_I(TAG, "ireport_arr_len is 0");
            str_stream << "{ \"status\":" << status << "}" << endl;
        }
        else{
            for (int i=0; i<ireport_arr_len; i++){
                Obd::ir_data_t &ireportdata = this->ctx->idling_report_collection[flipflop][i];
                str_stream << delim << endl << "{\"start_time\":" << ireportdata.start_ts;
                str_stream << ",\"end_time\":" << ireportdata.end_ts;
                str_stream << ",\"duration\":" << ireportdata.duration;
                if (ireportdata.fuel_param != -1 && ireportdata.fuel_start != -1 && ireportdata.fuel_end != -1){
                    str_stream << ",\"0x" << std::hex << ireportdata.fuel_param << "\": [\"" << std::dec << fixed << setprecision(OBD_DATA_PRECISION) << ireportdata.fuel_start << " " << ireportdata.fuel_end << "\"]";
                }
                else{
                    status = CAN_UNKNOWN_ERROR;
                }
                if (ireportdata.odo_param != -1 && ireportdata.odo_start != -1 && ireportdata.odo_end != -1){
                    str_stream << ",\"0x" << std::hex << ireportdata.odo_param << "\": [\"" << std::dec << fixed << setprecision(OBD_DATA_PRECISION) << ireportdata.odo_start << " " << ireportdata.odo_end << "\"]";
                }
                else{
                    status = CAN_UNKNOWN_ERROR;
                }
                str_stream << ",\"status\":" << status << "}";
                delim = ",";
            }

        }
        idling_report_string = str_stream.str();
        return true;
    }
}

#if 0
static bool insert_obd_map (map <string, vector <Obd::obd_data_t>> &obd_map, Obd::obd_data_t &obddata) {

    string key = string(obddata.spn_string);

    if (obd_map.find (key) == obd_map.end()) {
        vector <Obd::obd_data_t> v;
        v.push_back (obddata);

        obd_map.insert (make_pair (key, v));
        return true;
    }

    obd_map[key].push_back (obddata);
    return true;
}
#endif

bool Genmeta::convert_obdarray_json(string &obd_string, string &obd_hdmaps_str, int flipflop)
{
    std::stringstream str_stream, final_str_stream, ss;
    int i = 0;
    int obd_arr_len = this->ctx->obddata_collection[flipflop].size();
    string delim = "";
    string final_delim = "";
    map<int,stringstream> data;
    map<string, stringstream> data_str;

    if (obd_arr_len == 0) {
        LOG_I(TAG, "obd_arr_len is 0");
        final_str_stream << "\"status\":" << can_status_updated << endl;
    } else {
        LOG_I(TAG, "obd_arr_len %d", obd_arr_len);
        int64_t start_time = this->ctx->obddata_collection[flipflop][0].time;

        for (int i=0; i<obd_arr_len; i++) {
            Obd::obd_data_t &obddata = this->ctx->obddata_collection[flipflop][i];
            if (obddata.is_prop_param == 1) {
                if (data_str[obddata.spn_string].str().size() <= 0)
                    delim = "";
                else
                    delim = ",";
                data_str[obddata.spn_string] << delim << "\"" << (obddata.time - start_time) << " " << fixed << setprecision(OBD_DATA_PRECISION) <<  obddata.value << "\"";
            } else {
                if (data[obddata.spn_id].str().size() <= 0)
                    delim = "";
                else
                    delim = ",";
                data[obddata.spn_id] << delim << "\"" << (obddata.time - start_time) << " " << fixed << setprecision(OBD_DATA_PRECISION) <<  obddata.value << "\"";
            }
        }
        for (auto it = data_str.begin(); it != data_str.end(); ++it) {
            str_stream << final_delim << endl << "\"" << it->first << "\":["<< it->second.str()<< "]";
            final_delim = ",";
        }

        for (auto it = data.begin(); it != data.end(); ++it) {
            str_stream << final_delim << endl << "\"0x" << std::hex << it->first << "\":["<< it->second.str()<< "]";
            final_delim = ",";
        }
        string conf_vin = "";
        ss << configured_obd_vin;
        ss >> conf_vin;
        if (conf_vin == "")
            final_str_stream << "\"service_up_time\":" << start_time << "," << endl << "\"status\":" << CAN_STATUS_OK << "," << str_stream.str();
        else
            final_str_stream << "\"service_up_time\":" << start_time << "," << endl << "\"status\":" << CAN_STATUS_OK << "," << endl << "\"vin_configured\":\"" << conf_vin.c_str() << "\""  << "," << str_stream.str();
    }
    obd_string = final_str_stream.str();
    return true;
}

static bool frame_number_sort(FrameInfo::frameinfo_t &arg1, FrameInfo::frameinfo_t arg2)
{
    return (arg1.frame_number < arg2.frame_number);
}

bool Genmeta::convert_frameinfoarray_json(string &frameinfo_string, int flipflop)
{
    std::stringstream str_stream;
    int i = 0;
    int frameinfo_arr_len = this->ctx->frame_data_collection[flipflop].size();


    if(frameinfo_arr_len == 0)
    {
        LOG_D(TAG, "frameinfo_arr_len is 0");
        return false;
    }

    sort(this->ctx->frame_data_collection[flipflop].begin(), this->ctx->frame_data_collection[flipflop].end(), frame_number_sort);

    while(i<(frameinfo_arr_len-1))
    {

        FrameInfo::frameinfo_t &frame_data = this->ctx->frame_data_collection[flipflop][i];
        str_stream << "{"\
            << frameinfostr[FRAMEINFOSTR_CAPTURE_TIME] << frame_data.raw_time_micro \
            << frameinfostr[FRAMEINFOSTR_EPOCH_TIME] << (frame_data.epoch_time_micro/1000) \
            << frameinfostr[FRAMEINFOSTR_FRAME_NUMBER] << frame_data.frame_number \
            << "} ,";
        i++;
    }
    FrameInfo::frameinfo_t &frame_data = this->ctx->frame_data_collection[flipflop][i];
    str_stream << "{"\
        << frameinfostr[FRAMEINFOSTR_CAPTURE_TIME] << frame_data.raw_time_micro \
        << frameinfostr[FRAMEINFOSTR_EPOCH_TIME] << (frame_data.epoch_time_micro/1000) \
        << frameinfostr[FRAMEINFOSTR_FRAME_NUMBER] << frame_data.frame_number \
        << "} ";
    frameinfo_string = str_stream.str();
    return true;
}

bool Genmeta::convert_usralertarray_json(string &usralert_string, int flipflop , uint64_t end_time)
{
    std::stringstream str_stream;
    str_stream << std::fixed  << std::setprecision(7); // if needed set it to IMU aswell

    int i = 0;
    int usralert_arr_len = this->ctx->usr_alert_collection[flipflop].size();

    if (usralert_arr_len == 0) {
        LOG_D(TAG, "usralert_arr_len is 0");
        return false;
    }

    while (i < usralert_arr_len) {
        genmeta_usralert_t &usralertdata = this->ctx->usr_alert_collection[flipflop][i];
        if (usralertdata.time > end_time) {
            LOG_I(TAG, "usralertdata.time: %lld > end_time %lld " , usralertdata.time , end_time );
            this->ctx->usr_alert_collection[1 - flipflop].push_back(usralertdata);
            i++;
            continue;
        }

        str_stream << "{ " \
            << usralertstr[USRALERTSTR_TIME]	 	<< usralertdata.time << ", "\
            << usralertstr[USRALERTSTR_EVENTCODE]	<< "\"" << usralert_map[usralertdata.btn] << "\", "
            << usralertstr[USRALERTSTR_SOURCE]      << "\"" << usralertdata.source << "\" "
            << "} ,";

        i++;
    }

    usralert_string = str_stream.str();
    if (!usralert_string.empty()) {
        usralert_string.pop_back();
    }
    return true;
}

bool Genmeta::convert_ignition_status_json(string &ignition_status_string, int flipflop, uint64_t end_time)
{
	std::stringstream str_stream;
	str_stream << std::fixed  << std::setprecision(7); // if needed set it to IMU aswell
    int i = 0;
	int ignition_status_len = this->ctx->ignition_status_collection[flipflop].size();
    while(i < ignition_status_len && i < ctx->max_ignition_status-1){
        ignition_status_records_t ignitionData = this->ctx->ignition_status_collection[flipflop][i];
		str_stream << "{ " \
			<< ignitionstatusstr[IGNITIONSTR_STATUS]	 	<< ignitionData.status << ", "\
			<< ignitionstatusstr[IGNITIONSTR_TIME]	<< "\"" << ignitionData.time << "\" " << "} ,"	;

        i++;
    }
    if(i < ignition_status_len){
        ignition_status_records_t ignitionData = this->ctx->ignition_status_collection[flipflop][ignition_status_len - 1];
		str_stream << "{ " \
			<< ignitionstatusstr[IGNITIONSTR_STATUS]	 	<< ignitionData.status << ", "\
			<< ignitionstatusstr[IGNITIONSTR_TIME]	<< "\"" << ignitionData.time << "\" " << "} ,"	;

    }

	ignition_status_string = str_stream.str();
    if(!ignition_status_string.empty()){
        ignition_status_string.pop_back();
    }
    return true;
}

bool Genmeta::convert_ignition_on_duration_json(string &ignition_on_duration_string, int flipflop)
{
       int64_t start_session_time = this->ctx->genmetaHeader[flipflop].start_time;
       int64_t end_session_time = this->ctx->genmetaHeader[flipflop].end_time;
       int ignition_status_arr_len = this->ctx->ignition_status_collection[flipflop].size();
       if (ignition_status_arr_len == 0) {
           if (this->ctx->genmetaHeader[flipflop].ignition_status == "0") {
               ignition_on_duration_string = to_string(0);
           } else {
               int64_t ignition_on_duration_value =  end_session_time - start_session_time;
               ignition_on_duration_value = ignition_on_duration_value/1000;
               ignition_on_duration_string = to_string(ignition_on_duration_value);
           }
           return true;
       }
       int ignition_on_duration = 0;
       if (this->ctx->ignition_status_collection[flipflop][0].status == 0)
       {
           ignition_on_duration += (this->ctx->ignition_status_collection[flipflop][0].time - start_session_time);
       }
       for (int i = 1; i < ignition_status_arr_len; i++) {
           if (this->ctx->ignition_status_collection[flipflop][i].status == 0) {
               ignition_on_duration += (this->ctx->ignition_status_collection[flipflop][i].time - this->ctx->ignition_status_collection[flipflop][i - 1].time);
           }
       }
       if (this->ctx->ignition_status_collection[flipflop][ignition_status_arr_len - 1].status == 1) {
           ignition_on_duration += (end_session_time - this->ctx->ignition_status_collection[flipflop][ignition_status_arr_len - 1].time);
       }
       ignition_on_duration = ignition_on_duration / 1000;
       ignition_on_duration_string = to_string(ignition_on_duration);
       LOG_I(TAG, "final ignition_on_duration_string is  %s, ", ignition_on_duration_string.c_str());
       return true;
}

bool Genmeta::convert_highgalertarray_json(string &highgalert_string, int flipflop, uint64_t end_time)
{

	std::stringstream str_stream;
	str_stream << std::fixed  << std::setprecision(7); // if needed set it to IMU aswell

	int i = 0;
	int highgalert_arr_len = this->ctx->highg_alert_collection[flipflop].size();

	if(highgalert_arr_len == 0)
	{
		LOG_D(TAG, "highgalert_arr_len is 0");
		return false;
	}

	while(i<(highgalert_arr_len))
	{

	    genmeta_highgalert_t &highgalertdata = this->ctx->highg_alert_collection[flipflop][i];
        if(highgalertdata.time > end_time){
            LOG_I(TAG, "highgalertdata.time: %lld > end_time %lld  " , highgalertdata.time , end_time );
            this->ctx->highg_alert_collection[1 - flipflop].push_back(highgalertdata);
            i++;
            continue;
        }
		str_stream << "{ " \
			<< highgalertstr[HIGHGALERTSTR_TIME]	 	<< highgalertdata.time << "\" " << "} ,"     ;
		i++;
	}

	highgalert_string = str_stream.str();
    if(!highgalert_string.empty()){
        highgalert_string.pop_back();
    }
	return true;
}

bool Genmeta::convert_all_json(genmeta_header_t header, string &json_string,int flipflop, uint64_t end_time, bool hdmaps_mode_enabled, bool imu_data)
{
    string header_string = "", ignition_status_string = "", ignition_on_duration_string = "";
    string gps_string = "", pps_string = "";
    string usralert_string = "", highgalert_string = "";
    string obd_string = "", obd_idms_str = "", obd_hdmaps_str = "";
    string frameinfo_string = "", ublox_string = "";
    string imu_data_str = "", sensor_metadata_str = "", imu_temperature_string = "";
    string fuel_report_string = "", idling_report_string = "";
    string GPS_to_PPS_startidx = "", PPS_startidx = "";
    string ublox_constellstring = "";
    std::stringstream str_stream;

    Genmeta::convert_header_json(header, header_string);
    Genmeta::convert_gpsarray_json(gps_string, flipflop);
    Genmeta::convert_ppsarray_json(pps_string, flipflop);
    Genmeta::convert_gps_pps_start_index_json(GPS_to_PPS_startidx, PPS_startidx, flipflop);
    Genmeta::convert_obdarray_json(obd_string, obd_hdmaps_str, flipflop);
    Genmeta::convert_fuel_report_json(fuel_report_string, flipflop);
    Genmeta::convert_idling_report_json(idling_report_string, flipflop);
    Genmeta::convert_imuarray_json(imu_data_str, sensor_metadata_str, imu_temperature_string, flipflop);
    Genmeta::convert_usralertarray_json(usralert_string, flipflop, end_time);
    Genmeta::convert_highgalertarray_json(highgalert_string, flipflop, end_time);
    Genmeta::convert_ignition_status_json(ignition_status_string, flipflop, end_time);
    Genmeta::convert_ignition_on_duration_json(ignition_on_duration_string, flipflop);
    Genmeta::convert_frameinfoarray_json(frameinfo_string, flipflop);
    Genmeta::convert_ubloxarray_json(ublox_string, flipflop);
    Genmeta::convert_ublox_constellarray_json(ublox_constellstring, flipflop);

    if (hdmaps_mode_enabled) {
        if (imu_data) {
            str_stream  << "{" << endl << header_string << "," \
                        << "\"user_generated_alert\" : [" << endl <<  usralert_string << "], " << endl \
                        << "\"ignitions\" : [" << endl <<  ignition_status_string << "], " << endl \
                        << "\"ignition_on_duration\" : " << endl <<  ignition_on_duration_string << ", " << endl \
                        << "\"highg_generated_alert\" : [" << endl <<  highgalert_string << "], " << endl \
                        << "\"videoMetaData\" : [" << endl <<  gps_string << "], " << endl \
                        << "\"vehicle_data\" : {" << endl << obd_string << "}, " << endl \
                        << "\"ppsData\" : [" << endl <<  pps_string << "], " << endl \
                        << "\"GPS_to_PPS_startidx\" : " << GPS_to_PPS_startidx << "," << endl \
                        << "\"PPS_startidx\" : " << PPS_startidx << "," << endl \
                        << "\"canData\" : {" << endl <<  obd_hdmaps_str << "}, " << endl \
                        << "\"ubloxData\" : [" << endl <<  ublox_string << "], " << endl \
                        << "\"gnssConstellation\" : [" << endl << ublox_constellstring << "], " << endl \
                        << "\"frameData\" : [" << endl <<  frameinfo_string << "], " << endl \
                        << "\"sensorMetaData\" : [" << endl <<  sensor_metadata_str << "], " << endl \
                        << "\"imuTemp\" : [" << endl <<  imu_temperature_string << "]," << endl \
                        << "\"imuData\" : [" << endl <<  imu_data_str << "]" << endl \
                        << "}" << endl;
        } else {
            str_stream  << "{" << endl << header_string << "," \
                        << "\"user_generated_alert\" : [" << endl <<  usralert_string << "], " << endl \
                        << "\"ignitions\" : [" << endl <<  ignition_status_string << "], " << endl \
                        << "\"ignition_on_duration\" : " << endl <<  ignition_on_duration_string << ", " << endl \
                        << "\"highg_generated_alert\" : [" << endl <<  highgalert_string << "], " << endl \
                        << "\"videoMetaData\" : [" << endl <<  gps_string << "], " << endl \
                        << "\"vehicle_data\" : {" << endl << obd_string << "}, " << endl \
                        << "\"ppsData\" : [" << endl <<  pps_string << "], " << endl \
                        << "\"GPS_to_PPS_startidx\" : " << GPS_to_PPS_startidx << "," << endl \
                        << "\"PPS_startidx\" : " << PPS_startidx << "," << endl \
                        << "\"canData\" : {" << endl <<  obd_hdmaps_str << "}, " << endl \
                        << "\"ubloxData\" : [" << endl <<  ublox_string << "], " << endl \
                        << "\"gnssConstellation\" : [" << endl << ublox_constellstring << "], " << endl \
                        << "\"frameData\" : [" << endl <<  frameinfo_string << "], " << endl \
                        << "\"sensorMetaData\" : [" << endl <<  sensor_metadata_str << "]" << endl \
                        << "}" << endl;
        }
    }
    else {
        str_stream  << "{" << endl << header_string << "," \
                    << "\"user_generated_alert\" : [" << endl <<  usralert_string << "], " << endl \
                    << "\"ignitions\" : [" << endl <<  ignition_status_string << "], " << endl \
                    << "\"ignition_on_duration\" : " << endl <<  ignition_on_duration_string << ", " << endl \
                    << "\"highg_generated_alert\" : [" << endl <<  highgalert_string << "], " << endl \
                    << "\"videoMetaData\" : [" << endl <<  gps_string << "], " << endl \
                    << "\"ppsData\" : [" << endl <<  pps_string << "], " << endl \
                    << "\"GPS_to_PPS_startidx\" : " << GPS_to_PPS_startidx << "," << endl \
                    << "\"PPS_startidx\" : " << PPS_startidx << "," << endl \
                    << "\"frameData\" : [" << endl <<  frameinfo_string << "], " << endl \
                    << "\"imuTemp\" : [" << endl <<  imu_temperature_string << "]," << endl \
                    << "\"imuData\" : [" << endl <<  imu_data_str << "]," << endl \
                    << "\"vehicle_data\" : {" << endl << obd_string << "}, " << endl \
                    << "\"fuel_report\" : [" << endl << fuel_report_string << "], " << endl \
                    << "\"idling_report\" : [" << endl << idling_report_string << "], " << endl \
                    << "\"sensorMetaData\" : [" << endl <<  sensor_metadata_str << "]" << endl \
                    << "}" << endl;
    }

    json_string = str_stream.str();
    return true;
}

bool Genmeta::get_meta_json(string &json_string, int flipflop, uint64_t end_time, bool hdmaps_mode_enabled, bool imu_data) // for all sensoer; call from central
{
	Genmeta::convert_all_json(ctx->genmetaHeader[flipflop], json_string, flipflop, end_time, hdmaps_mode_enabled, imu_data);
	Genmeta::clear_data(flipflop);
	return true;
}

void Genmeta::clear_data(int flipflop)
{
       LOG_I(TAG, "Clearing GPS collection of length %d", this->ctx->gps_data_collection[flipflop].size());
       LOG_I(TAG, "Clearing OBD collection of length %d", this->ctx->obddata_collection[flipflop].size());
       LOG_I(TAG, "Clearing PPS collection of length %d", this->ctx->gps_pps_data_collection[flipflop].size());
       LOG_I(TAG, "Clearing Ublox collection of length %d", this->ctx->ublox_data_collection[flipflop].size());
       LOG_I(TAG, "Clearing IMU temp collection of length %d", this->ctx->imu_temperature_data_collection[flipflop].size());
       LOG_I(TAG, "Clearing Frameinfo collection of length %d", this->ctx->frame_data_collection[flipflop].size());

#ifdef BAGHEERA2
       LOG_I(TAG, "Clearing Accel collection of length %d", this->ctx->imu_accel_data_collection[flipflop].size());
       LOG_I(TAG, "Clearing Gyro collection of length %d", this->ctx->imu_gyro_data_collection[flipflop].size());
#elif KRAIT
       LOG_I(TAG, "Clearing IMU collection of length %d", this->ctx->imu_data_collection[flipflop].size());
#endif
       LOG_I(TAG, "Clearing fuel report collection of length %d", this->ctx->fuel_report_collection[flipflop].size());
       LOG_I(TAG, "Clearing idling report collection of length %d", this->ctx->idling_report_collection[flipflop].size());
       LOG_I(TAG, "Clearing USR ALERT collection of length %d", this->ctx->usr_alert_collection[flipflop].size());

       LOG_I(TAG, "GPS collection of length %d", this->ctx->gps_data_collection[!flipflop].size());
       LOG_I(TAG, "OBD collection of length %d", this->ctx->obddata_collection[!flipflop].size());
       LOG_I(TAG, "PPS collection of length %d", this->ctx->gps_pps_data_collection[!flipflop].size());
       LOG_I(TAG, "Frameinfo collection of length %d", this->ctx->frame_data_collection[!flipflop].size());
       LOG_I(TAG, "Ublox collection of length %d", this->ctx->ublox_data_collection[!flipflop].size());
#ifdef BAGHEERA2
       LOG_I(TAG, "IMU collection of length %d", this->ctx->imu_accel_data_collection[!flipflop].size() + this->ctx->imu_gyro_data_collection[!flipflop].size());
#elif KRAIT
       LOG_I(TAG, "IMU collection of length %d", this->ctx->imu_data_collection[!flipflop].size());
#endif
       LOG_I(TAG, "Fuel report collection of length %d", this->ctx->fuel_report_collection[!flipflop].size());
       LOG_I(TAG, "Idling report collection of length %d", this->ctx->idling_report_collection[!flipflop].size());
       LOG_I(TAG, "Usr alert collection of length %d", this->ctx->usr_alert_collection[!flipflop].size());
       LOG_I(TAG, "Highg alert collection of length %d", this->ctx->highg_alert_collection[!flipflop].size());
       LOG_I(TAG, "Ignition status collection of length %d", this->ctx->ignition_status_collection[!flipflop].size());

       this->ctx->gps_data_collection[flipflop].clear();
       this->ctx->obddata_collection[flipflop].clear();
       this->ctx->gps_pps_data_collection[flipflop].clear();
       this->ctx->ublox_data_collection[flipflop].clear();
       this->ctx->imu_temperature_data_collection[flipflop].clear();
       this->ctx->frame_data_collection[flipflop].clear();
#ifdef BAGHEERA2
       this->ctx->imu_accel_data_collection[flipflop].clear();
       this->ctx->imu_gyro_data_collection[flipflop].clear();
#elif KRAIT
       this->ctx->imu_data_collection[flipflop].clear();
#endif
       this->ctx->fuel_report_collection[flipflop].clear();
       this->ctx->idling_report_collection[flipflop].clear();
       this->ctx->usr_alert_collection[flipflop].clear();
       this->ctx->highg_alert_collection[flipflop].clear();
       this->ctx->ignition_status_collection[flipflop].clear();
}
