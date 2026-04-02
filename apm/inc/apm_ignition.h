/* Copyright (C) 2019 - 2020 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Devendra Yadav <devendra.yadav@netradyne.com>
 */

#ifndef __IGNITION_H_
#define __IGNITION_H_

#include <stdio.h>
#include <apm.h>

static const int IGNITION_OFF_POS = 5;
static const int IGNITION_ON_POS  = 0;

constexpr uint IGN_SHORT_WINDOW = 1; // seconds
constexpr uint IGN_FULL_WINDOW = 1; // seconds
constexpr uint IGN_DATA_OUTAGE_THRESHOLD = 60; // seconds
constexpr uint IGN_READ_INTERVAL_SEC = 1; // seconds
constexpr uint IGN_MEDIAN_DEC_WINDOW = 30; // seconds 1/3 of the full window
constexpr uint IGN_CONTINUOUS_DEC_WINDOW = IGN_FULL_WINDOW; // TODO: Use it for validation and debounce
constexpr uint IGN_CACHE_WINDOW = 60; // seconds
constexpr uint IGN_MAX_DEBOUNCE_COUNT = 5;
constexpr uint MAX_CNT_FOR_EASY_INSTALL = 100;

struct IGNData {
  uint decision[APM_Decision::eAPM_Decision_Max] = {0};
  ignition_status_t status = IGNITION_OFF;
};


class IGNBinData : public WorkerBinData {

    public:
    IGNBinData() {}

    // By default return the latest data
    inline uint get_decision_count(APM_Decision decision, uint index ) {

        if((false == apm_worker_utils::is_decision_valid(decision)) ||
           (false == apm_worker_utils::is_index_valid(index, IGN_CACHE_WINDOW)) ) {
            return 0;
        }
        return get_data(index).decision[decision];
    }

    inline void set_decision_count(APM_Decision decision, uint index, uint count) {

        if((false == apm_worker_utils::is_decision_valid(decision)) ||
           (false == apm_worker_utils::is_index_valid(index, IGN_CACHE_WINDOW)) ) {
            return ;
        }

       data[index].decision[decision] = count;
    }

    void init_decision_counts(uint index) {
        for(uint i = 0; i < APM_Decision::eAPM_Decision_Max; ++i) {
            data[index].decision[i] = 0;
        }
    }

    inline bool is_above_threshold(APM_Decision decision, uint index) {

       if((false == apm_worker_utils::is_decision_valid(decision)) ||
          (false == apm_worker_utils::is_index_valid(index, IGN_CACHE_WINDOW)) ){
           return false;
       }

      return (get_data(index).status == ignition_status_t::IGNITION_ON);
    }

    inline bool is_data_outage(uint index) {

        if(false == apm_worker_utils::is_index_valid(index, IGN_CACHE_WINDOW)) {
            return false;
        }

        if((get_data(index).status < ignition_status_t::IGNITION_OFF) && (get_data(index).status > ignition_status_t::IGNITION_ON)) {
           return true;
        }
       return false;
    }

    inline void set_threshold(APM_Decision decision, float thresh) { }

    inline float get_threshold(APM_Decision decision) { return 0.0; }

    inline IGNData get_data(uint index ) {
        if(apm_worker_utils::is_index_valid(index, IGN_CACHE_WINDOW) == false) {
            return IGNData();
        }
        return data[index];
    }

    inline void set_bin_data(uint index, void* ign_data ) {

        if(false == apm_worker_utils::is_index_valid(index, IGN_CACHE_WINDOW)) {
            return ;
        }

        IGNData* v_data = static_cast<IGNData*>(ign_data);
        data[index].status = v_data->status;
        init_decision_counts(index);
    }

    std::string get_ignition_status_string(ignition_status_t status) {
        string status_str = "";
        if(status == IGNITION_ON) {
            status_str += "ON";
        } else if(status == IGNITION_OFF) {
            status_str += "OFF";
        } else {
            status_str += "ERR";
        }
        return status_str;
    }

    inline string get_data_string(uint index) {

        if(false == apm_worker_utils::is_index_valid(index, IGN_CACHE_WINDOW)) {
            return string("IGN: INVALID_IDX");
        }

        string data_str = "";
        uint prev_index = (index == 0) ? (IGN_CACHE_WINDOW - 1) : (index - 1);
        uint old_index = (index == (IGN_CACHE_WINDOW - 1)) ? 0 : (index + 1);
        float ign_voltage = ND_DeviceFactory::Create_NDDevice()->get_voltage_value(eIGN_VOLT);

        std::ostringstream oss;
        oss << "IGN: " << get_ignition_status_string(data[index].status)
            << " Prev: " << get_ignition_status_string(data[prev_index].status)
            << " Old(" << old_index << "): " << get_ignition_status_string(data[old_index].status)
            << " V: " << std::fixed << std::setprecision(3) << ign_voltage;

        return oss.str();
    }

    inline void bin_data() {

        APM_Decision bin_decision =  ( MOVING  == APM::WorkerInstance(eSatellite)->getMotionState() ) ? eAPM_Decision_Moving : eAPM_Decision_Engine_Off;
    }

    inline std::string get_valid_sysfs_path() {
        return get_sysfs_path_from_enum(PowermonParam::eIGN_VALID);
    }

    inline void mark_data_invalid(uint index) {

        if(false == apm_worker_utils::is_index_valid(index, IGN_CACHE_WINDOW) ) {
            return;
        }

        uint prev_index = (index == 0) ? (IGN_CACHE_WINDOW - 1) : (index - 1);

        // update the previous data... sustain the old status until data outage is true.
        data[index].status = get_data(prev_index).status;
    }

    inline bool is_idling(uint index) {
        if(apm_worker_utils::is_index_valid(index, IGN_CACHE_WINDOW) == false) {
            return false;
        }

        return is_above_threshold(APM_Decision::eAPM_Decision_Idling, index);
    }

    std::string get_thres_string() {
        std::ostringstream oss;
        oss << "IGN_Thres[ ";
        oss << "ON: " << IGNITION_ON;
        oss << ", OFF: " << IGNITION_OFF;
        oss << ", Debounce(s): " << IGN_MAX_DEBOUNCE_COUNT;
        oss << " ]";
        return oss.str();
    }

  private:
  IGNData data[IGN_CACHE_WINDOW];

};

class IGNS_worker : public APM_worker {
public:

    IGNS_worker(bool legacy_mode, bool disable_sensor) {

        setWorkerBinDataObj(new IGNBinData());
        // Initialize decision intervals to avoid division by zero and set proper windows
        set_decison_interval(eDecisionWindow_Short, IGN_SHORT_WINDOW);
        set_decison_interval(eDecisionWindow_Long,  IGN_FULL_WINDOW);
        set_decison_interval(eDecisionWindow_Valid, IGN_MAX_DEBOUNCE_COUNT);
        set_decison_interval(eDecisionWindow_Outage, IGN_DATA_OUTAGE_THRESHOLD);
        set_decison_interval(eDecisionWindow_Sleep,  IGN_READ_INTERVAL_SEC);
        set_decison_interval(eDecisionWindow_Median, IGN_MEDIAN_DEC_WINDOW);
        set_decison_interval(eDecisionWindow_Continuous, IGN_CONTINUOUS_DEC_WINDOW);
        set_decison_interval(eDecisionWindow_Cache, IGN_CACHE_WINDOW);
        use_legacy = legacy_mode;
        is_event_driven = true;
        sensor_decision_disabled = disable_sensor;

        initMotionState();
        detect_easy_install_mode();
    }

  poll_func_t poll_func;
  intr_func_t intr_func;
  filter_func_t filter_func;
  read_status_t read_status;

    void insert_bin_data();

    int get_signal_mask() {
        return IGNS_MASK_POS;
    }

    const char* get_TAG();

    int get_ign_src_type(){
        return IgnitonSource::eCrankLine;
    }

    std::string get_src_status_sysfs_path() {
        return get_sysfs_path_from_enum(PowermonParam::eGPIO_IGN);
    }

    std::string get_attr_user() {
        return USER_IGN;
    }

    void send_ign_status(ignition_status_t status);

    void initMotionState();

    void test_callback_fn(int status);
    std::string get_test_file_path() {
        return "/dev/shm/apm_test/apm_ign_test";
    }

    void detect_easy_install_mode();

    bool is_above_threshold(APM_Decision decision) {

        if(false == apm_worker_utils::is_decision_valid(decision)) {
            return false;
        }

        if((APM_Decision::eAPM_Decision_Engine_Off != decision) && (APM_Decision::eAPM_Decision_Moving != decision)) {
            return APM_worker::is_above_threshold(decision);
        }

        bool ign_valid = (get_data_outage_count() == 0) ? true : false;

        if(true == ign_valid) {
            return APM_worker::is_above_threshold(decision);
        }
        else {

            bool pwr_valid = (NULL != APM::WorkerInstance(eCrankVolt) ? APM::WorkerInstance(eCrankVolt)->is_worker_enabled() : false);
            bool imu_valid = (NULL != APM::WorkerInstance(eInertial) ? APM::WorkerInstance(eInertial)->is_worker_enabled() : false);
            bool gps_valid = (NULL != APM::WorkerInstance(eSatellite) ? APM::WorkerInstance(eSatellite)->is_worker_enabled() : false);
            bool can_valid = (NULL != APM::WorkerInstance(eCANBus) ? APM::WorkerInstance(eCANBus)->is_worker_enabled() : false);

                if( (false == pwr_valid) &&
                    (false == imu_valid) &&
                    (false == gps_valid) &&
                    (false == can_valid) ) {

                    return APM_worker::is_above_threshold(decision);
                }
                else {

                    // check if last ingested inputs are NOT in data outage bucket for imu, gps and pwr
                    // consider a source for fusion only when it's enabled/valid and has no outage
                    bool fusion_pwr = (true == pwr_valid) ? (APM::WorkerInstance(eCrankVolt)->get_data_outage_count() == 0) : false;
                    bool fusion_imu = (true == imu_valid) ? (APM::WorkerInstance(eInertial)->get_data_outage_count() == 0) : false;
                    bool fusion_gps = (true == gps_valid) ? (APM::WorkerInstance(eSatellite)->get_data_outage_count() == 0) : false;
                    bool fusion_can = (true == can_valid) ? (APM::WorkerInstance(eCANBus)->get_data_outage_count() == 0) : false;

                    bool fusion_result = false;

                    fusion_result |= (true == fusion_pwr) ? ( MOVING == APM::WorkerInstance(eCrankVolt)->getMotionState() ) : false;
                    fusion_result |= (true == fusion_imu) ? ( MOVING == APM::WorkerInstance(eInertial)->getMotionState() ) : false;
                    fusion_result |= (true == fusion_gps) ? ( MOVING == APM::WorkerInstance(eSatellite)->getMotionState() ) : false;
                    fusion_result |= (true == fusion_can) ? ( MOVING == APM::WorkerInstance(eCANBus)->getMotionState() ) : false;

                    return fusion_result;
                }
        }
    }

    void setMotionState(motion_status_t motion_status) {
        if(true == is_data_outage()) {
            motion_status = getMotionState(); // retain previous state
        }
        APM_worker::setMotionState(motion_status);
    }

};


#endif
