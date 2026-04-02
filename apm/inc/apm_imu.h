/* Copyright (C) 2019 - 2020 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Devendra Yadav <devendra.yadav@netradyne.com>
 */

#ifndef __APM_IMU_H_
#define __APM_IMU_H_

#include <apm.h>
#include "apm_engine_status.h"

constexpr uint IMU_SHORT_WINDOW = 10; // seconds
constexpr uint IMU_FULL_WINDOW = 200; // seconds
constexpr uint IMU_READ_INTERVAL_SEC = 1; // seconds
constexpr uint IMU_DATA_OUTAGE_THRESHOLD = 60; // seconds
constexpr uint IMU_MEDIAN_DEC_WINDOW = 70; // seconds .IMU_FULL_WINDOW / 3
constexpr uint IMU_CONTINUOUS_DEC_WINDOW = (IMU_SHORT_WINDOW / 2); // seconds, continuous decision window is half of short window

// Early Detection Window
constexpr uint IMU_FULL_WINDOW_ED = 60; // seconds

constexpr uint IMU_CACHE_WINDOW = IMU_FULL_WINDOW; // seconds, cache window to store IMU data

enum eIMU_Axis {
    eIMU_Accel_X = 0,
    eIMU_Accel_Y,
    eIMU_Accel_Z,
    eIMU_Gyro_X,
    eIMU_Gyro_Y,
    eIMU_Gyro_Z,
    eIMU_AxisMax,
};

enum eIMU_Data_Type {
    eIMU_SAD,
    eIMU_Displacement,
    eIMU_DataMax,
};

#define TAG_IMU "A_IMU"
struct ImuData {
    uint decision[APM_Decision::eAPM_Decision_Max] = {0};
    // uint decision_distance[APM_Decision::eAPM_Decision_Max] = {0};
    float axis[eIMU_AxisMax] = {static_cast<float>(SysfsErrorCodes::eSYSFS_INVALID_DATA)}; // Axis data represents max of all 6 axes
    float corrected_velocity = static_cast<float>(SysfsErrorCodes::eSYSFS_INVALID_DATA);
    float corrected_distance = static_cast<float>(SysfsErrorCodes::eSYSFS_INVALID_DATA);
};

class ENG_STAT_worker;
class ImuBinData : public WorkerBinData {

    public:
    ImuBinData(float axis_thresholds[eIMU_AxisMax][eAPM_Decision_Max]) {

        for(int i = 0; i < eIMU_AxisMax; ++i) {
            for(int j = 0; j < APM_Decision::eAPM_Decision_Max; ++j) {
                threshold[i][j] = axis_thresholds[i][j];
            }
        }

        validate_thresholds();
    }

    inline ImuData get_data(uint index ) {
        if(apm_worker_utils::is_index_valid(index, IMU_CACHE_WINDOW) == false) {
            LOG_E(TAG_IMU, "Invalid index requested: %u", index);
            return ImuData();
        }
        //is index in  valid range
        return data[index];
    }
 
    // By default return the latest data
    inline uint get_decision_count(APM_Decision decision, uint index ) {
        if(false == apm_worker_utils::is_decision_valid(decision)) {
            return 0;
        }
        //is index in range.
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

        if(decision == APM_Decision::eAPM_Decision_Idling){
            return ( (get_data(index).axis[eIMU_Accel_X] < get_threshold(eIMU_Accel_X, APM_Decision::eAPM_Decision_Moving) && get_data(index).axis[eIMU_Accel_X] >= get_threshold(eIMU_Accel_X, APM_Decision::eAPM_Decision_Idling) ) ||
                    (get_data(index).axis[eIMU_Accel_Y] < get_threshold(eIMU_Accel_Y, APM_Decision::eAPM_Decision_Moving) && get_data(index).axis[eIMU_Accel_Y] >= get_threshold(eIMU_Accel_Y, APM_Decision::eAPM_Decision_Idling) ) ||
                    (get_data(index).axis[eIMU_Accel_Z] < get_threshold(eIMU_Accel_Z, APM_Decision::eAPM_Decision_Moving) && get_data(index).axis[eIMU_Accel_Z] >= get_threshold(eIMU_Accel_Z, APM_Decision::eAPM_Decision_Idling) ) ||
                    (get_data(index).axis[eIMU_Gyro_X] < get_threshold(eIMU_Gyro_X, APM_Decision::eAPM_Decision_Moving) && get_data(index).axis[eIMU_Gyro_X] >= get_threshold(eIMU_Gyro_X, APM_Decision::eAPM_Decision_Idling) ) ||
                    (get_data(index).axis[eIMU_Gyro_Y] < get_threshold(eIMU_Gyro_Y, APM_Decision::eAPM_Decision_Moving) && get_data(index).axis[eIMU_Gyro_Y] >= get_threshold(eIMU_Gyro_Y, APM_Decision::eAPM_Decision_Idling) ) ||
                    (get_data(index).axis[eIMU_Gyro_Z] < get_threshold(eIMU_Gyro_Z, APM_Decision::eAPM_Decision_Moving) && get_data(index).axis[eIMU_Gyro_Z] >= get_threshold(eIMU_Gyro_Z, APM_Decision::eAPM_Decision_Idling) ));
        }
        return (
                ((get_data(index).axis[eIMU_Accel_X] >= get_threshold(eIMU_Accel_X, decision) && get_data(index).axis[eIMU_Accel_X] < DEF_IMU_MAX_THRESHOLD) ||
                    (get_data(index).axis[eIMU_Accel_Y] >= get_threshold(eIMU_Accel_Y, decision) && get_data(index).axis[eIMU_Accel_Y] < DEF_IMU_MAX_THRESHOLD) ||
                    (get_data(index).axis[eIMU_Accel_Z] >= get_threshold(eIMU_Accel_Z, decision) && get_data(index).axis[eIMU_Accel_Z] < DEF_IMU_MAX_THRESHOLD) ||
                    (get_data(index).axis[eIMU_Gyro_X] >= get_threshold(eIMU_Gyro_X, decision) && get_data(index).axis[eIMU_Gyro_X] < DEF_IMU_MAX_THRESHOLD) ||
                    (get_data(index).axis[eIMU_Gyro_Y] >= get_threshold(eIMU_Gyro_Y, decision) && get_data(index).axis[eIMU_Gyro_Y] < DEF_IMU_MAX_THRESHOLD) ||
                    (get_data(index).axis[eIMU_Gyro_Z] >= get_threshold(eIMU_Gyro_Z, decision) && get_data(index).axis[eIMU_Gyro_Z] < DEF_IMU_MAX_THRESHOLD)));
    }

    inline bool is_data_outage(uint index) {
        for(int i = 0; i < eIMU_AxisMax; ++i) {
            if((get_data(index).axis[i] < SysfsErrorCodes::eSYSFS_RET_ZERO) || (get_data(index).axis[i] > DEF_IMU_MAX_THRESHOLD)) {
                return true;
            }
        }
        return false;
    }

    //worker override level
    inline float get_threshold(APM_Decision decision) {

        if(false == apm_worker_utils::is_decision_valid(decision)) {
            return DEF_IMU_THRESHOLD;
        }
        return threshold[eIMU_Accel_X][decision];
    }

    //overload for local implementation
    inline float get_threshold(eIMU_Axis axis, APM_Decision decision) {

        if(false == apm_worker_utils::is_decision_valid(decision)) {
            return DEF_IMU_THRESHOLD;
        }

        return threshold[axis][decision];
    }

    inline void set_bin_data(uint index, void* imu_data ) {

        if(apm_worker_utils::is_index_valid(index, IMU_CACHE_WINDOW) == false) {
            LOG_E(TAG_IMU, "Invalid index requested: %u", index);
            return;
        }

        ImuData* imu_data_ptr = static_cast<ImuData*>(imu_data);
        data[index] = *imu_data_ptr;
    }

    string get_data_string(uint index) {
	    std::ostringstream oss;
        oss << std::fixed << std::setprecision(3);

        oss << "IMU: Velocity: " << data[index].corrected_velocity
            << ", Distance: " << data[index].corrected_distance;

        oss << " A: " << data[index].axis[eIMU_Accel_X] << ","
            << data[index].axis[eIMU_Accel_Y] << ","
            << data[index].axis[eIMU_Accel_Z] << ","
            << "G: " << data[index].axis[eIMU_Gyro_X] << ","
            << data[index].axis[eIMU_Gyro_Y] << ","
            << data[index].axis[eIMU_Gyro_Z];

        return oss.str();
    }

    inline std::string get_valid_sysfs_path() {
        return get_sysfs_path_from_enum(PowermonParam::eIMU_VALID);
    }

    inline void mark_data_invalid(uint index) {
        if(false == apm_worker_utils::is_index_valid(index, IMU_CACHE_WINDOW) ) {
            LOG_E(TAG_IMU, "Invalid index to mark data valid: %u", index);
            return;
        }
        // mark data as in valid for the given index
        for(int i = 0; i < eIMU_AxisMax; ++i) {
            data[index].axis[i] = static_cast<float>(SysfsErrorCodes::eSYSFS_INVALID_DATA);
        }
    }

    bool is_idling(uint index) {
        // Idling not applicable for IMU currently
        return false;
    }

    void validate_thresholds() {
        for(int i = 0; i < eIMU_AxisMax; ++i) {
            for(int j = 0; j < APM_Decision::eAPM_Decision_Max; ++j) {
                if((threshold[i][j] < DEF_IMU_MIN_THRESHOLD) || (threshold[i][j] > DEF_IMU_MAX_THRESHOLD)) {
                    LOG_E(TAG_IMU, "Invalid threshold for axis %d, decision %d: %f. Resetting to default %f", i, j, threshold[i][j], DEF_IMU_THRESHOLD);
                    if(j == APM_Decision::eAPM_Decision_Moving){
                      threshold[i][j] = DEF_IMU_THRESHOLD;
                    } else if (j == APM_Decision::eAPM_Decision_Engine_Off){
                      threshold[i][j] = DEF_IMU_ENGINE_OFF_THRESHOLD;
                    } else if (j == APM_Decision::eAPM_Decision_Fusion_Moving){
                      threshold[i][j] = DEF_IMU_ENGINE_OFF_THRESHOLD - DEF_ENGINE_OFF_IMU_THRESHOLD_FUSION;
                    } else if (j == APM_Decision::eAPM_Decision_Fusion_Engine_Off){
                      threshold[i][j] = DEF_ENGINE_OFF_IMU_THRESHOLD_FUSION;
                    } else {
                      threshold[i][j] = DEF_IMU_THRESHOLD;
                    }
                }
            }
        }

        // Moving should be greater than Engine Off
        for(int i = 0; i < eIMU_AxisMax; ++i) {
            if(threshold[i][APM_Decision::eAPM_Decision_Moving] <= threshold[i][APM_Decision::eAPM_Decision_Engine_Off]) {
                LOG_E(TAG_IMU, "Invalid thresholds for axis %d: Moving %f <= Engine Off %f. Adjusting Moving threshold.", i,
                      threshold[i][APM_Decision::eAPM_Decision_Moving],
                      threshold[i][APM_Decision::eAPM_Decision_Engine_Off]);
                threshold[i][APM_Decision::eAPM_Decision_Moving] = threshold[i][APM_Decision::eAPM_Decision_Engine_Off] + DEF_IMU_HYSTERESIS_THRESHOLD;
            }
        }
    }

    std::string get_thres_string() {
        std::ostringstream oss;
        oss << "IMU_Thres[ ";
        oss << "ax (on: " << std::fixed << std::setprecision(3) << threshold[eIMU_Accel_X][eAPM_Decision_Moving] << " off: " << std::fixed << std::setprecision(3) << threshold[eIMU_Accel_X][eAPM_Decision_Engine_Off] << ")";
        oss << ", ay (on: " << std::fixed << std::setprecision(3) << threshold[eIMU_Accel_Y][eAPM_Decision_Moving] << " off: " << std::fixed << std::setprecision(3) << threshold[eIMU_Accel_Y][eAPM_Decision_Engine_Off] << ")";
        oss << ", az (on: " << std::fixed << std::setprecision(3) << threshold[eIMU_Accel_Z][eAPM_Decision_Moving] << " off: " << std::fixed << std::setprecision(3) << threshold[eIMU_Accel_Z][eAPM_Decision_Engine_Off] << ")";
        oss << ", gx (on: " << std::fixed << std::setprecision(3) << threshold[eIMU_Gyro_X][eAPM_Decision_Moving] << " off: " << std::fixed << std::setprecision(3) << threshold[eIMU_Gyro_X][eAPM_Decision_Engine_Off] << ")";
        oss << ", gy (on: " << std::fixed << std::setprecision(3) << threshold[eIMU_Gyro_Y][eAPM_Decision_Moving] << " off: " << std::fixed << std::setprecision(3) << threshold[eIMU_Gyro_Y][eAPM_Decision_Engine_Off] << ")";
        oss << ", gz (on: " << std::fixed << std::setprecision(3) << threshold[eIMU_Gyro_Z][eAPM_Decision_Moving] << " off: " << std::fixed << std::setprecision(3) << threshold[eIMU_Gyro_Z][eAPM_Decision_Engine_Off] << ")";
        oss << " ]";
        return oss.str();
    }

    private:
    ImuData data[IMU_CACHE_WINDOW];
    float threshold[eIMU_AxisMax][APM_Decision::eAPM_Decision_Max] = {0.0};
};

class IMU_worker : public APM_worker {
public:
    ENG_STAT_worker *eng_stat_worker = nullptr;

    IMU_worker(float axis_thresholds[eIMU_AxisMax][eAPM_Decision_Max], bool legacy_mode, bool disable_sensor, int imu_decision_window[eDecisionWindow_Max], ENG_STAT_worker *eng_stat_worker_ptr){

        setWorkerBinDataObj(new ImuBinData(axis_thresholds));
	
        if( getWorkerBinDataObj() != NULL ) {
            // Initialize decision intervals to avoid division by zero
            set_decison_interval(eDecisionWindow_Short, imu_decision_window[eDecisionWindow_Short]);
            set_decison_interval(eDecisionWindow_Long,  imu_decision_window[eDecisionWindow_Long]);
            set_decison_interval(eDecisionWindow_Valid, imu_decision_window[eDecisionWindow_Valid]);
            if(imu_decision_window[eDecisionWindow_Outage] > IMU_CACHE_WINDOW) {
                LOG_W(TAG_IMU, "Outage decision window configured is more than cache window. Resetting outage decision window to %d seconds", IMU_DATA_OUTAGE_THRESHOLD);
                imu_decision_window[eDecisionWindow_Outage] = IMU_DATA_OUTAGE_THRESHOLD;
            }
            set_decison_interval(eDecisionWindow_Outage, imu_decision_window[eDecisionWindow_Outage]);
            set_decison_interval(eDecisionWindow_Sleep,  IMU_READ_INTERVAL_SEC);
            set_decison_interval(eDecisionWindow_Median, imu_decision_window[eDecisionWindow_Median]);
            if(imu_decision_window[eDecisionWindow_Continuous] > IMU_CACHE_WINDOW) {
                LOG_W(TAG_IMU, "Continuous decision window configured is more than cache window. Resetting continuous decision window to %d seconds", IMU_CONTINUOUS_DEC_WINDOW);
                imu_decision_window[eDecisionWindow_Continuous] = IMU_CONTINUOUS_DEC_WINDOW;
            }
            set_decison_interval(eDecisionWindow_Continuous, imu_decision_window[eDecisionWindow_Continuous]);
            set_decison_interval(eDecisionWindow_Cache, IMU_CACHE_WINDOW);
            // Set legacy mode
            use_legacy = legacy_mode;
            sensor_decision_disabled = disable_sensor;
        } else {
            LOG_C(get_TAG(), "%s BinDataObj is NULL!!!!. Falling back to Legacy");
            use_legacy = true;
        }

        initMotionState();

        eng_stat_worker = eng_stat_worker_ptr;
    }

    poll_func_t poll_func;
    intr_func_t intr_func;
    filter_func_t filter_func;
    read_status_t read_status;

    void insert_bin_data();

    int get_signal_mask() {
        return IMU_MASK_POS;
    }

    const char* get_TAG() {
        return TAG_IMU;
    }

    int get_ign_src_type(){
        return IgnitonSource::eInertial;
    }

  //inline void update_calibration_data();

    std::string get_src_status_sysfs_path() {
        return get_sysfs_path_from_enum(PowermonParam::eIMU_IGN);
    }

    std::string get_attr_user() {
        return USER_IMU;
    }

    bool update_data_outage_count();

    void compute_sad_avg_bin_data(apm_imu_data_t &data, float &correct_distance, float &correct_velocity);

    motion_status_t getEngineState();
};

#endif
