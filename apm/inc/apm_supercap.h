/* Copyright (C) 2019 - 2020 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Devendra Yadav <devendra.yadav@netradyne.com>
 */

#ifndef __APM_SUPERCAP_H_
#define __APM_SUPERCAP_H_

#include <apm.h>


constexpr uint PFI_SHORT_WINDOW = 1; // seconds
constexpr uint PFI_FULL_WINDOW = 5; // seconds
constexpr uint PFI_DATA_OUTAGE_THRESHOLD = 60; // seconds
constexpr uint PFI_READ_INTERVAL_SEC = 1; // seconds
constexpr uint PFI_MEDIAN_DEC_WINDOW = 30; // seconds 1/3 of the full window
constexpr uint PFI_CONTINUOUS_DEC_WINDOW = PFI_FULL_WINDOW;
constexpr uint PFI_CACHE_WINDOW = 60; // seconds
constexpr uint PFI_MAX_DEBOUNCE_COUNT = 5;

const std::string event_count_xattr_key = "user.sc_cnt";
const std::string event_timestamp_xattr_key = "user.sc_ts";
const std::string event_volt_xattr_key = "user.sc_volt";
const std::string event_count_file_path = ND_ATTR_PATH + std::string("apm/sc_count");

struct PFIData {
    uint decision[APM_Decision::eAPM_Decision_Max] = {0};
    supercap_status_t status = BATTERY_ACTIVE;
    float voltage = static_cast<float>(SysfsErrorCodes::eSYSFS_INVALID_DATA);
};


class PFIBinData : public WorkerBinData {

    public:
    PFIBinData(sensor_config_t cfg) {

        if(true == is_24V_battery()) {
            this->supercap_threshold[APM_Decision::eAPM_Decision_Moving] = cfg.pfi_on_crank_v_th_24v;
            this->supercap_threshold[APM_Decision::eAPM_Decision_Engine_Off] = cfg.pfi_off_crank_v_th_24v;
        }
        else {
            this->supercap_threshold[APM_Decision::eAPM_Decision_Moving] = cfg.pfi_on_crank_v_th;
            this->supercap_threshold[APM_Decision::eAPM_Decision_Engine_Off] = cfg.pfi_off_crank_v_th;
        }

        validate_thresholds();
        create_supercap_event_file();

    }

    // By default return the latest data
    inline uint get_decision_count(APM_Decision decision, uint index ) {

        if((false == apm_worker_utils::is_decision_valid(decision)) ||
           (false == apm_worker_utils::is_index_valid(index, PFI_CACHE_WINDOW)) ) {
            return 0;
        }
        return get_data(index).decision[decision];
    }

    inline void set_decision_count(APM_Decision decision, uint index, uint count) {

        if((false == apm_worker_utils::is_decision_valid(decision)) ||
           (false == apm_worker_utils::is_index_valid(index, PFI_CACHE_WINDOW)) ) {
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
        if((false == apm_worker_utils::is_decision_valid(decision)) ||
            (false == apm_worker_utils::is_index_valid(index, PFI_CACHE_WINDOW)) ) {
            return false;
        }
        supercap_status_t pfi_status = get_data(index).status;
        float voltage = get_data(index).voltage;

        if( SUPERCAP_ACTIVE == pfi_status ) { // If supercap is active, return false
            return true;
        }
        // Checking supercap status above threshold according to pfi and voltage thresholds
        //   < supercap_thresold (9.75/19.50) is SUPERCAP_ACTIVE means MOVING
        //   >= supercap_threshold_off (10.01/20.01) is BATTERY_ACTIVE means STATIONARY
        //   > supercap_threshold and < supercap_threshold_off then previous state is maintained (hysteresis window for clear)
         // MOVING is considered supercap active, STATIONARY is considered battery active
        if((voltage >= nd_factory_utils::get_min_valid_voltage()) && (voltage <= nd_factory_utils::get_max_valid_voltage()) && (voltage < get_threshold(decision))) {
            return true;
        }
        return false;
    }

    inline bool is_data_outage(uint index) {

        if(false == apm_worker_utils::is_index_valid(index, PFI_CACHE_WINDOW)) {
            return false;
        }

        // Not checking voltage here as data outage is also used to notify the coninuous supercap toggle state. Only checking supercap status is valid or not here to identify data outage.
        if(((get_data(index).status < supercap_status_t::SUPERCAP_ACTIVE) && (get_data(index).status > supercap_status_t::BATTERY_ACTIVE) )) {
           return true;
        }
        return false;
    }

   //Local Method
   // By default return the latest data
    inline PFIData get_data(uint index ) {
        if(apm_worker_utils::is_index_valid(index, PFI_CACHE_WINDOW) == false) {
            // LOG_E(TAG, "Invalid index requested: %u", index);
            return PFIData();
        }
       //is index in  valid range
       return data[index];
    }

    inline void set_bin_data(uint index, void* pfi_data ) {

        if(false == apm_worker_utils::is_index_valid(index, PFI_CACHE_WINDOW)) {
            return ;
        }

        PFIData* v_data = static_cast<PFIData*>(pfi_data);
        data[index].status = v_data->status;
        data[index].voltage = v_data->voltage;
        init_decision_counts(index);
    }

    std::string get_sc_status_string(supercap_status_t status, float voltage) {
        std::ostringstream oss;
        oss << "PFI: ";
        if(status == BATTERY_ACTIVE) {
            oss << BATTERY_ACTIVE;
        } else if(status == SUPERCAP_ACTIVE) {
            oss << SUPERCAP_ACTIVE;
        } else {
            oss << SUPERCAP_ERROR;
        }
        oss << " V: " << std::fixed << std::setprecision(3) << voltage;
        return oss.str();
    }

    inline string get_data_string(uint index) {

        if(false == apm_worker_utils::is_index_valid(index, PFI_CACHE_WINDOW)) {
            return string("SC: INVALID_IDX");
        }

        std::ostringstream volt_oss;
        volt_oss << std::fixed << std::setprecision(3) << std::stof(event_volt_str);

        string data_str = "";
        uint prev_index = (index == 0) ? (PFI_CACHE_WINDOW - 1) : (index - 1);
        uint old_index = (index == (PFI_CACHE_WINDOW - 1)) ? 0 : (index + 1);
        data_str = get_sc_status_string(data[index].status, data[index].voltage) +
                    " Prev: " + get_sc_status_string(data[prev_index].status, data[prev_index].voltage) +
                    " Old(" + to_string(old_index) + "): " + get_sc_status_string(data[old_index].status, data[old_index].voltage) +
                    " Time: " + event_time_str + "ms" +
                    " Volt: " + volt_oss.str() + "V" +
                    " Backup: " + event_count_str + "s" ;

        return data_str;
    }

    inline void bin_data() {

        APM_Decision bin_decision =  (NULL != APM::WorkerInstance(eSatellite) ? ((MOVING  == APM::WorkerInstance(eSatellite)->getMotionState()) ? eAPM_Decision_Moving : eAPM_Decision_Engine_Off) : eAPM_Decision_Engine_Off);
    }

    inline std::string get_valid_sysfs_path() {
        return get_sysfs_path_from_enum(PowermonParam::ePFI_VALID);
    }

    inline void mark_data_invalid(uint index) {

        if(false == apm_worker_utils::is_index_valid(index, PFI_CACHE_WINDOW) ) {
            return;
        }

        uint prev_index = (index == 0) ? (PFI_CACHE_WINDOW - 1) : (index - 1);

        // update the previous data... sustain the old status until data outage is true.
        data[index].status = get_data(prev_index).status;
    }

    inline bool is_idling(uint index) {
        if(apm_worker_utils::is_index_valid(index, PFI_CACHE_WINDOW) == false) {
            return false;
        }

        return is_above_threshold(APM_Decision::eAPM_Decision_Idling, index);
    }

    float get_threshold(APM_Decision decision) {
        // Does not matter which decision, return the supercap threshold
        return supercap_threshold[decision];
    }

    bool is_24V_battery() {
        return ((TRUE_STR == apm_attr_util::get_attr_status(vehicle_attributes, USER_24V_STR, FALSE_STR)) ? true : false);
    }

    void validate_thresholds() {
        // validate supercap thresholds

        if(false == is_24V_battery()) {
            if((supercap_threshold[APM_Decision::eAPM_Decision_Moving] < MIN_SUPERCAP_LOWER_THRESHOLD) ||
            (supercap_threshold[APM_Decision::eAPM_Decision_Moving] > MAX_SUPERCAP_UPPER_THRESHOLD)) {
                supercap_threshold[APM_Decision::eAPM_Decision_Moving] = DEF_SUPERCAP_LOWER_THRESHOLD;
            }

            if((supercap_threshold[APM_Decision::eAPM_Decision_Engine_Off] < MIN_SUPERCAP_LOWER_THRESHOLD) ||
            (supercap_threshold[APM_Decision::eAPM_Decision_Engine_Off] > MAX_SUPERCAP_UPPER_THRESHOLD)) {
                supercap_threshold[APM_Decision::eAPM_Decision_Engine_Off] = DEF_SUPERCAP_UPPER_THRESHOLD;
            }
        }
        else {
            if((supercap_threshold[APM_Decision::eAPM_Decision_Moving] < MIN_SUPERCAP_LOWER_THRESHOLD_24V) ||
            (supercap_threshold[APM_Decision::eAPM_Decision_Moving] > MAX_SUPERCAP_UPPER_THRESHOLD_24V)) {
                supercap_threshold[APM_Decision::eAPM_Decision_Moving] = DEF_SUPERCAP_LOWER_THRESHOLD_24V;
            }

            if((supercap_threshold[APM_Decision::eAPM_Decision_Engine_Off] < MIN_SUPERCAP_LOWER_THRESHOLD_24V) ||
            (supercap_threshold[APM_Decision::eAPM_Decision_Engine_Off] > MAX_SUPERCAP_UPPER_THRESHOLD_24V)) {
                supercap_threshold[APM_Decision::eAPM_Decision_Engine_Off] = DEF_SUPERCAP_UPPER_THRESHOLD_24V;
            }
        }
    }

    bool create_supercap_event_file(){
        attr_file_status_t status = apm_attr_util::create_attr_file(event_count_file_path);
        if(eATTR_FILE_CREATED == status) {
            // Initialize xattributes
            apm_attr_util::set_attr_status(event_count_file_path, event_count_xattr_key, "0");
            apm_attr_util::set_attr_status(event_count_file_path, event_timestamp_xattr_key, "0");
            apm_attr_util::set_attr_status(event_count_file_path, event_volt_xattr_key, "0.0");
        }
        else if(eATTR_FILE_PRESENT == status) {
            event_count_str = apm_attr_util::get_attr_status(event_count_file_path, event_count_xattr_key, "0");
            event_time_str = apm_attr_util::get_attr_status(event_count_file_path, event_timestamp_xattr_key, "0");
            event_volt_str = apm_attr_util::get_attr_status(event_count_file_path, event_volt_xattr_key, "0.0");

            // Reset all sc attributes
            apm_attr_util::set_attr_status(event_count_file_path, event_count_xattr_key, "0");
            apm_attr_util::set_attr_status(event_count_file_path, event_timestamp_xattr_key, "0");
            apm_attr_util::set_attr_status(event_count_file_path, event_volt_xattr_key, "0.0");
        }

        return true;
    }

    std::string get_thres_string() {
        std::ostringstream oss;
        oss << "SC_Thres[ ";
        oss << "BATTERY_ACTIVE: " << std::fixed << std::setprecision(3) << supercap_threshold[APM_Decision::eAPM_Decision_Engine_Off]<< " ";
        oss << ",SUPERCAP_ACTIVE: " << std::fixed << std::setprecision(3) << supercap_threshold[APM_Decision::eAPM_Decision_Moving]<< " ";
        oss << ",Debounce(s): " << PFI_MAX_DEBOUNCE_COUNT << " ";
        oss << ",Type: " << (is_24V_battery() ? "24V" : "12V") << " ";
        oss << "]";
        return oss.str();
    }

    private:
    PFIData data[PFI_CACHE_WINDOW];
    float supercap_threshold[APM_Decision::eAPM_Decision_Max] = {DEF_SUPERCAP_LOWER_THRESHOLD};
    std::string event_count_str = "0";
    std::string event_time_str = "0";
    std::string event_volt_str = "0.0";
};

class SC_worker : public APM_worker {
public:

    SC_worker(bool legacy_mode, sensor_config_t sensor_cfg, bool disable_sensor) {
        setWorkerBinDataObj(new PFIBinData(sensor_cfg));
        // Initialize decision intervals to avoid division by zero and set proper windows
        set_decison_interval(eDecisionWindow_Short, PFI_SHORT_WINDOW);
        set_decison_interval(eDecisionWindow_Long,  PFI_FULL_WINDOW);
        set_decison_interval(eDecisionWindow_Valid, PFI_MAX_DEBOUNCE_COUNT);
        set_decison_interval(eDecisionWindow_Outage, PFI_DATA_OUTAGE_THRESHOLD);
        set_decison_interval(eDecisionWindow_Sleep,  PFI_READ_INTERVAL_SEC);
        set_decison_interval(eDecisionWindow_Median, PFI_MEDIAN_DEC_WINDOW);
        set_decison_interval(eDecisionWindow_Continuous, PFI_CONTINUOUS_DEC_WINDOW);
        set_decison_interval(eDecisionWindow_Cache, PFI_CACHE_WINDOW);
        use_legacy = legacy_mode;
        is_event_driven = true;
        sensor_decision_disabled = disable_sensor;

        initMotionState();
        supercap_backup_count = 0;

		// // Set supercap thresholds for bagheera2 and bagheera3 as adc interrupt is supported
		if(false == ND_DeviceFactory::Create_NDDevice()->set_supercap_thresholds(nd_factory_utils::get_max_valid_voltage(), get_threshold(APM_Decision::eAPM_Decision_Engine_Off))) {
			LOG_E(TAG,"Failed in setting supercap threshold");
		}
    }

    poll_func_t poll_func;
    intr_func_t intr_func;
    filter_func_t filter_func;
    read_status_t read_status;

    uint64_t supercap_backup_count = 0;
    bool send_health_event = false;

    void insert_bin_data();

    int get_signal_mask() {
        return SC_MASK_POS;
    }

    const char* get_TAG();

    int get_ign_src_type(){
        return IgnitonSource::ePowerFail;
    }

    void send_sc_status(bool event = false);

    bool send_signal_status(motion_status_t status, bool event = false) {

        if(true == is_continuous_motion(eAPM_Decision_Debounce, get_prev_decision_index(), get_decision_interval(eDecisionWindow_Valid)) &&
           (false == is_data_outage())) {
            setMotionState(MOVING);

            if(false == use_legacy) {
                reset_bit(get_signal_mask());
            }

            set_decision_count(eAPM_Decision_Engine_Off, 0);
            set_decision_count(eAPM_Decision_Moving, 0);
        }

        if((getMotionState() == MOVING) || ( getMotionState() != status)) {
            send_sc_status();
        }

        return APM_worker::send_signal_status(status, event);
    }

    std::string get_src_status_sysfs_path() {
        return get_sysfs_path_from_enum(PowermonParam::ePFI_STAT);
    }

    std::string get_attr_user() {
        return USER_PFI;
    }

    std::thread detect_engine_status_th;

    float get_threshold(APM_Decision decision) {
        return getWorkerBinDataObj()->get_threshold(decision);
    }

    void update_supercap_backup_count(bool reset = false) {
        if(true == reset) {
            supercap_backup_count = 0;
            return;
        }
        supercap_backup_count++;
    }

    uint64_t get_supercap_backup_count() {
        return supercap_backup_count;
    }

    bool create_supercap_event_file();

    bool write_supercap_event_xattr(std::string key, std::string value);

    void initMotionState() {
        motion_status_t motion_status = STATIONARY;
        setMotionState(motion_status);
    }

    void test_callback_fn(int status);
    std::string get_test_file_path() {
        return "/dev/shm/apm_test/apm_sc_test";
    }

};
#endif
