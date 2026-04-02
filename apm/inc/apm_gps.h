/* Copyright (C) 2020 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Devendra Yadav <devendra.yadav@netradyne.com>
 */

#ifndef __APM_GPS_H_
#define __APM_GPS_H_

#include <math.h>
#include <apm.h>

#define TAG_GPS "A_GPS"

constexpr uint GPS_SHORT_WINDOW = 10; // seconds
constexpr uint GPS_FULL_WINDOW = 200; // seconds
constexpr uint GPS_READ_INTERVAL_SEC = 1; // seconds
constexpr uint GPS_DATA_OUTAGE_THRESHOLD = 60; // seconds
constexpr uint GPS_MEDIAN_DEC_WINDOW = 70; // seconds. 1/3 rd of GPS_FULL_WINDOW
constexpr uint GPS_CONTINUOUS_DEC_WINDOW = (GPS_SHORT_WINDOW / 2); // seconds, continuous decision window is half of short window

constexpr double DEF_GPS_SPEED_MAX_THRESHOLD = 200.00; //mph
constexpr double DEF_GPS_DISTANCE_MAX_THRESHOLD = 100.00; // 200mph = ~90m/s-> so taking ~ 100 meters as the max distance covered in a second.
constexpr float DEF_GPS_ACCURACY_MAX_THRESHOLD = 25.00; // in meters
constexpr float MIN_GPS_ACCURACY_THRESHOLD = 5.00; // in meters

constexpr double GPS_MAX_LATITUDE =  90.0000; // Check between range -090 < lat < 090 for VALIDITY
constexpr double GPS_MAX_LONGITUDE = 180.0000; // Check between range -180 < lat < 180 for VALIDITY

// Early Detection Window
constexpr uint GPS_FULL_WINDOW_ED = 60; // seconds

constexpr uint GPS_CACHE_WINDOW = GPS_FULL_WINDOW; // seconds, cache window to store GPS data

enum GPSParameters {
    eGPS_Speed = 0,
    eGPS_Latitude,
    eGPS_Longitude,
    eGPS_ParamMax
};


struct GpsData {
    uint decision[APM_Decision::eAPM_Decision_Max] = {0};
    double param[eGPS_ParamMax] = {static_cast<double>(SysfsErrorCodes::eSYSFS_INVALID_DATA)}; // param data represents speed, lat, long
    bool valid = false;
    float accuracy = static_cast<float>(SysfsErrorCodes::eSYSFS_INVALID_DATA);
};
class GpsBinData : public WorkerBinData {
    public:
    GpsBinData(float param_thresholds[eGPS_ParamMax][eAPM_Decision_Max]) {
        for(int i = 0; i < eGPS_ParamMax; ++i) {
            for(int j = 0; j < APM_Decision::eAPM_Decision_Max; ++j) {
                threshold[i][j] = param_thresholds[i][j];
            }
        }

        validate_thresholds();
    }

    inline GpsData get_data(uint index ) {
        if(apm_worker_utils::is_index_valid(index, GPS_CACHE_WINDOW) == false) {
            LOG_E(TAG_GPS, "Invalid index requested: %u", index);
            return GpsData();
        }
        //is index in  valid range
        return data[index];
    }

    // By default return the latest data
    inline uint get_decision_count(APM_Decision decision, uint index ) {
        if(false == apm_worker_utils::is_decision_valid(decision)) {
            return 0;
        }
        return get_data(index).decision[decision];
    }

    inline void set_decision_count(APM_Decision decision, uint index, uint count) {

        if(false == apm_worker_utils::is_decision_valid(decision)) {
            return;
        }
        data[index].decision[decision] = count;
    }

    void init_decision_counts(uint index) {
        for(uint i = 0; i < APM_Decision::eAPM_Decision_Max; ++i) {
            data[index].decision[i] = 0;
        }
    }

    inline bool is_above_threshold(APM_Decision decision, uint index) {

        if(false == apm_worker_utils::is_decision_valid(decision)) {
            return false;
        }

        if( (get_data(index).accuracy <= SysfsErrorCodes::eSYSFS_RET_ZERO) || (get_data(index).accuracy > gps_accuracy_threshold ) ) {
            return false;
        }

        // If decision is for fusion, then check if fusion is activated or not before applying threshold
        // So whoever is calling this API for fusion decision, should get status based on fusion activation.
        if( (decision >= APM_Decision::eAPM_Decision_Fusion_Engine_Off) && (decision <= APM_Decision::eAPM_Decision_Fusion_Idling) ) {
            if(false == APM::WorkerInstance(eSatellite)->get_fusion_activated()) {
                return false;
            }
        }

        return ((get_data(index).param[eGPS_Speed]) >= get_threshold(eGPS_Speed, decision) && ((get_data(index).param[eGPS_Speed]) < DEF_GPS_SPEED_MAX_THRESHOLD) );
    }

    inline bool is_data_outage(uint index) {

	if( (get_data(index).accuracy  < SysfsErrorCodes::eSYSFS_RET_ZERO) || (get_data(index).accuracy > DEF_GPS_ACCURACY_MAX_THRESHOLD) ) {
	        return true;
	}

        if( (get_data(index).param[eGPS_Speed] < SysfsErrorCodes::eSYSFS_RET_ZERO) || (get_data(index).param[eGPS_Speed] > DEF_GPS_SPEED_MAX_THRESHOLD) ) {
	        return true;
	}
	if( ( fabs(get_data(index).param[eGPS_Latitude] ) > GPS_MAX_LATITUDE ) ||
			( fabs(get_data(index).param[eGPS_Longitude] ) > GPS_MAX_LONGITUDE) ) {
		return true;
	}

	return false;
    }

    inline float get_threshold(APM_Decision decision) {
        if(false == apm_worker_utils::is_decision_valid(decision)) {
            return DEF_SPEED_THRESHOLD;
        }

        return threshold[eGPS_Speed][decision];
    }

    inline float get_threshold(GPSParameters param, APM_Decision decision) {

        if(false == apm_worker_utils::is_decision_valid(decision)) {
            return DEF_SPEED_THRESHOLD;
        }

        return threshold[param][decision];
    }

    void set_bin_data(uint index, void* gps_data ) {

        if(apm_worker_utils::is_index_valid(index, GPS_CACHE_WINDOW) == false) {
            LOG_E(TAG_GPS, "Invalid index requested: %u", index);
            return;
        }

        GpsData* gps_data_ptr = static_cast<GpsData*>(gps_data);

        if( false == gps_data_ptr->valid) {

            uint last_index = ( 0 == index ) ?  (GPS_CACHE_WINDOW - 1) : (index - 1) ;
            GpsData lGps = get_data(last_index);

            //We will copy the last lat/long values if valid..
            if( true == lGps.valid ) {

                gps_data_ptr->valid = true;
                gps_data_ptr->accuracy = gps_accuracy_threshold;
                //Not Updating Speed.
                gps_data_ptr->param[eGPS_Latitude] = lGps.param[eGPS_Latitude];
                gps_data_ptr->param[eGPS_Longitude] = lGps.param[eGPS_Longitude];
            }
        }
        data[index] = *gps_data_ptr;
    }

    string get_valid_sysfs_path() {
        return get_sysfs_path_from_enum(PowermonParam::eGPS_VALID);
    }

    // Check if last data is valid or not
    bool is_data_valid(uint index) {
        if(apm_worker_utils::is_index_valid(index, GPS_CACHE_WINDOW) == false) {
            LOG_E(TAG_GPS, "Invalid index requested: %u", index);
            return false;
        }
        if(get_data(index).valid == false) {
            return false;
        }
        float accuracy = get_data(index).accuracy;
        if( (accuracy < SysfsErrorCodes::eSYSFS_RET_ZERO) || (accuracy > gps_accuracy_threshold) ) {
            return false;
        }
        return true;
    }

    void update_calibration_data(uint index) {
        
        uint prev_index = ( 0 == index ) ? (GPS_CACHE_WINDOW - 1) : (index - 1) ;
        int prev_decision_count = get_decision_count(eAPM_Decision_Fusion_Moving, prev_index);
        if(is_above_threshold(eAPM_Decision_Fusion_Moving, index)){
            if(prev_decision_count < GPS_MEDIAN_DEC_WINDOW){
               set_decision_count(eAPM_Decision_Fusion_Moving, index, prev_decision_count + 1);
            }
            else{
                set_decision_count(eAPM_Decision_Fusion_Moving, index, prev_decision_count);
            }    
        }else {
            if(prev_decision_count > 0){
                set_decision_count(eAPM_Decision_Fusion_Moving, index, get_decision_count(eAPM_Decision_Fusion_Moving, prev_index) - 1);
            } else {
                set_decision_count(eAPM_Decision_Fusion_Moving, index, 0);
            }

            if(is_data_valid(index)){
                set_decision_count(eAPM_Decision_Fusion_Engine_Off, index, prev_decision_count + 1);
            }else{
                set_decision_count(eAPM_Decision_Fusion_Engine_Off, index, 0);
            }
        }
        return;
    }

    string get_data_string(uint index) {
        string data_str = "";
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(3);
        oss << "GPS: Sp: " << data[index].param[eGPS_Speed]
            << ", Lt: " << data[index].param[eGPS_Latitude]
            << ", Lg: " << data[index].param[eGPS_Longitude]
            << ", Accuracy: " << data[index].accuracy;
        data_str = oss.str();
        return data_str;
    }

    bool is_idling(uint index){
        // Idling not applicable for GPS currently
        return false;
    }

    bool is_stationary (uint index){
	    // if STATIONARY for last duration interval...and Decison is Engine_Off.... 
	    //We check the actual distance covered/moved based on valid oldest and latest lat/long data
	    uint old_index = ( 0 == index ) ? (GPS_CACHE_WINDOW - 1) : (index + 1);
	    GpsData oldGps =  get_data(old_index);
	    GpsData newGps =  get_data(index);

	    if( (false == oldGps.valid) || (false == newGps.valid) ){
		    return true;

		    float lat_diff  =  fabs(newGps.param[eGPS_Latitude] - oldGps.param[eGPS_Latitude]);
		    float long_diff =  fabs(newGps.param[eGPS_Longitude] - oldGps.param[eGPS_Longitude]);

            if(get_threshold(eGPS_Latitude, eAPM_Decision_Engine_Off) <= 0.0f || get_threshold(eGPS_Longitude, eAPM_Decision_Engine_Off) <= 0.0f) {
                LOG_D(TAG_GPS, "Latitude/Longitude threshold is non-positive, cannot determine STATIONARY state based on GPS data");
                return true;
            }

		    if( (lat_diff  > get_threshold(eGPS_Latitude, eAPM_Decision_Engine_Off) ) ||
				    (long_diff > get_threshold(eGPS_Longitude, eAPM_Decision_Engine_Off) ) ) {

			    // Vehicle has moved greater than than lat / long thresholds over the LONG WINDOW continue in MOVING State.
			    return false;
		    }
	    }
        return false;
    }

    void mark_data_invalid(uint index) {
        if(apm_worker_utils::is_index_valid(index, GPS_CACHE_WINDOW) == false) {
            LOG_E(TAG_GPS, "Invalid index requested: %u", index);
            return;
        }
        data[index].param[eGPS_Speed] = static_cast<float>(SysfsErrorCodes::eSYSFS_INVALID_DATA);
        data[index].param[eGPS_Latitude] = DEF_LAT_INVALID;
        data[index].param[eGPS_Longitude] = DEF_LONG_INVALID;
        data[index].accuracy = static_cast<float>(SysfsErrorCodes::eSYSFS_INVALID_DATA);
    }

    void validate_thresholds() {

        for(int i = 0; i < eGPS_ParamMax; ++i) {
            // validate speed thresholds
            if(i == eGPS_Speed) {
                for(int j = 0; j < APM_Decision::eAPM_Decision_Max; ++j) {
                    if((threshold[i][j] < MIN_GPS_SPEED_THRESHOLD) || (threshold[i][j] > DEF_GPS_SPEED_MAX_THRESHOLD)) {
                        LOG_E(TAG_GPS, "Invalid speed threshold for decision %d: %f. Resetting to default %f", j, threshold[i][j], DEF_SPEED_THRESHOLD);
                        threshold[i][j] = DEF_SPEED_THRESHOLD;
                    }
                }
            }
            // validate latitude thresholds
            else if(i == eGPS_Latitude) {
                for(int j = 0; j < APM_Decision::eAPM_Decision_Max; ++j) {
                    if((threshold[i][j] < MIN_GPS_LAT_THRESHOLD) || (threshold[i][j] > DEF_GPS_DISTANCE_MAX_THRESHOLD)) {
                        LOG_E(TAG_GPS, "Invalid latitude threshold for decision %d: %f. Resetting to default %f", j, threshold[i][j], 0.0001f);
                        threshold[i][j] = DEF_LAT_THRESHOLD;
                    }
                }
            }
            // validate longitude thresholds
            else if(i == eGPS_Longitude) {
                for(int j = 0; j < APM_Decision::eAPM_Decision_Max; ++j) {
                    if((threshold[i][j] < MIN_GPS_LONG_THRESHOLD) || (threshold[i][j] > DEF_GPS_DISTANCE_MAX_THRESHOLD)) {
                        LOG_E(TAG_GPS, "Invalid longitude threshold for decision %d: %f. Resetting to default %f", j, threshold[i][j], 0.0001f);
                        threshold[i][j] = DEF_LONG_THRESHOLD;
                    }
                }
            }
        }

        // validate accuracy threshold
        if((gps_accuracy_threshold < MIN_GPS_ACCURACY_THRESHOLD) || (gps_accuracy_threshold > DEF_GPS_ACCURACY_MAX_THRESHOLD)) {
            LOG_E(TAG_GPS, "Invalid GPS accuracy threshold: %f. Resetting to default %f", gps_accuracy_threshold, MIN_GPS_ACCURACY);
            gps_accuracy_threshold = MIN_GPS_ACCURACY;
        }
    }

    std::string get_thres_string() {
        std::ostringstream oss;
        oss << "GPS_Thres[";
        oss << " Speed (on: " << std::fixed << std::setprecision(3) << threshold[eGPS_Speed][eAPM_Decision_Moving] << " off: " << std::fixed << std::setprecision(3) << threshold[eGPS_Speed][eAPM_Decision_Engine_Off] << ")";
        oss << ", Lat (on: " << std::fixed << std::setprecision(3) << threshold[eGPS_Latitude][eAPM_Decision_Moving] << " off: " << std::fixed << std::setprecision(3) << threshold[eGPS_Latitude][eAPM_Decision_Engine_Off] << ")";
        oss << ", Lon (on: " << std::fixed << std::setprecision(3) << threshold[eGPS_Longitude][eAPM_Decision_Moving] << " off: " << std::fixed << std::setprecision(3) << threshold[eGPS_Longitude][eAPM_Decision_Engine_Off] << ")";
        oss << ", Acc:" << std::fixed << std::setprecision(3) << gps_accuracy_threshold;
        oss << " ]";
        return oss.str();
    }

private:
    GpsData data[GPS_CACHE_WINDOW];
    float threshold[eGPS_ParamMax][APM_Decision::eAPM_Decision_Max] = {0.0};
    float gps_accuracy_threshold = MIN_GPS_ACCURACY;// 10 meters
};



class GPS_worker : public APM_worker {
public:

    GPS_worker(float param_thresholds[eGPS_ParamMax][eAPM_Decision_Max], bool legacy_mode, bool disable_sensor, int gps_dec_window[eDecisionWindow_Max]) {
        setWorkerBinDataObj(new GpsBinData(param_thresholds));

        if(getWorkerBinDataObj() != NULL) {
            // Initialize decision intervals to avoid division by zero
            set_decison_interval(eDecisionWindow_Short, gps_dec_window[eDecisionWindow_Short]);
            set_decison_interval(eDecisionWindow_Long,  gps_dec_window[eDecisionWindow_Long]);
            set_decison_interval(eDecisionWindow_Valid, gps_dec_window[eDecisionWindow_Short]);
            if(gps_dec_window[eDecisionWindow_Outage] > GPS_CACHE_WINDOW) {
                LOG_W(get_TAG(), "Outage decision window %u is greater than cache window %u. Resetting outage decision window to data outage threshold %u", gps_dec_window[eDecisionWindow_Outage], GPS_CACHE_WINDOW, GPS_DATA_OUTAGE_THRESHOLD);
                gps_dec_window[eDecisionWindow_Outage] = GPS_DATA_OUTAGE_THRESHOLD;
            }
            set_decison_interval(eDecisionWindow_Outage, gps_dec_window[eDecisionWindow_Outage]);
            set_decison_interval(eDecisionWindow_Sleep,  GPS_READ_INTERVAL_SEC);
            set_decison_interval(eDecisionWindow_Median, gps_dec_window[eDecisionWindow_Median]);
            if(gps_dec_window[eDecisionWindow_Continuous] > GPS_CACHE_WINDOW) {
                LOG_W(get_TAG(), "Continuous decision window %u is greater than cache window %u. Resetting continuous decision window to half of short window %u", gps_dec_window[eDecisionWindow_Continuous], GPS_CACHE_WINDOW, GPS_CONTINUOUS_DEC_WINDOW);
                gps_dec_window[eDecisionWindow_Continuous] = GPS_CONTINUOUS_DEC_WINDOW;
            }
            set_decison_interval(eDecisionWindow_Continuous, gps_dec_window[eDecisionWindow_Continuous]);
            set_decison_interval(eDecisionWindow_Cache, GPS_CACHE_WINDOW);
            // Set legacy mode
            use_legacy = legacy_mode;
            sensor_decision_disabled = disable_sensor;

        } else {
            LOG_C(get_TAG(), "%s BinDataObj is NULL!!!!. Falling back to Legacy");
            use_legacy = true;
        }

        initMotionState();
    }


    virtual ~GPS_worker() {}
    poll_func_t poll_func;
    intr_func_t intr_func;
    filter_func_t filter_func;
    read_status_t read_status;

    inline void insert_bin_data();

    int get_signal_mask() {
        return GPS_MASK_POS;
    }

    const char* get_TAG(){
        return TAG_GPS;
    }

    int get_ign_src_type(){
        return IgnitonSource::eSatellite;
    }

    std::string get_src_status_sysfs_path() {
        return get_sysfs_path_from_enum(PowermonParam::eGPS_IGN);
    }

    std::string get_attr_user() {
        return USER_GPS;
    }
};

#endif
