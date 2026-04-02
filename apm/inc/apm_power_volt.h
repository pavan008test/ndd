#ifndef _APM_POWER_VOLT_H_
#define _APM_POWER_VOLT_H_

#include <apm.h>
#include "nd_file_utils.h"
#include "system_utils.h"

#define TAG_PWR_VOLT "A_PWR_VOLT"


constexpr uint PWR_SHORT_WINDOW = 10; // seconds
constexpr uint PWR_FULL_WINDOW = 60; // seconds
constexpr uint PWR_DATA_OUTAGE_THRESHOLD = 120; // seconds
constexpr uint POWER_VOLT_READ_INTERVAL_SEC = 1; // seconds
constexpr uint PWR_MEDIAN_DEC_WINDOW = 20; // seconds 1/3 of the full window
constexpr uint PWR_CONTINUOUS_DEC_WINDOW = (PWR_SHORT_WINDOW / 2); // seconds, continuous decision window is half of short window

constexpr uint MAX_BAD_BATTERY_COUNT = PWR_FULL_WINDOW; // 1 minutes

// Early Detection Window
constexpr uint PWR_FULL_WINDOW_ED = 10; // seconds
constexpr uint PWR_FULL_WINDOW_MEDIAN_ED = 10; // seconds

constexpr uint PWR_CACHE_WINDOW = PWR_FULL_WINDOW; // seconds, cache window to store Power Voltage data

constexpr const char* USER_ON_GT_CNT = "user.on_gt_cnt";
constexpr const char* USER_ON_LT_CNT = "user.on_lt_cnt";
constexpr const char* USER_ON_GT_AVG = "user.on_gt_avg";
constexpr const char* USER_ON_LT_AVG = "user.on_lt_avg";
constexpr const char* USER_OFF_GT_CNT = "user.off_gt_cnt";
constexpr const char* USER_OFF_LT_CNT = "user.off_lt_cnt";
constexpr const char* USER_OFF_GT_AVG = "user.off_gt_avg";
constexpr const char* USER_OFF_LT_AVG = "user.off_lt_avg";

constexpr uint MAX_BAD_VOLTAGE_COUNT = PWR_FULL_WINDOW;

struct VoltData {
    uint decision[APM_Decision::eAPM_Decision_Max] = {0};
    float power_volt = ND_DeviceFactory::Create_NDDevice()->get_voltage_value(eCRANK_VOLT);
    float drp_volt = ND_DeviceFactory::Create_NDDevice()->get_voltage_value(eCRANK_VDROP);
};

class VoltBinData : public WorkerBinData {

    public:
    VoltBinData(sensor_config_t sensor_cfg) {
        // Add checks for valid thresholds

        // 12V thresholds
        threshold[APM_Decision::eAPM_Decision_Engine_Off] = sensor_cfg.eng_off_crank_v_th;
        threshold[APM_Decision::eAPM_Decision_Moving] = sensor_cfg.eng_on_crank_v_th;
        threshold[APM_Decision::eAPM_Decision_Idling] = sensor_cfg.idle_crank_v_th;

        // 12V Fusion thresholds
        threshold[APM_Decision::eAPM_Decision_Fusion_Engine_Off] = sensor_cfg.fusion_eng_off_crank_v_th;
        threshold[APM_Decision::eAPM_Decision_Fusion_Moving] = sensor_cfg.fusion_eng_on_crank_v_th;
        threshold[APM_Decision::eAPM_Decision_Fusion_Idling] = sensor_cfg.fusion_idle_crank_v_th;

        // 24V thresholds
        threshold_24v[APM_Decision::eAPM_Decision_Engine_Off] = sensor_cfg.eng_off_crank_v_th_24v;
        threshold_24v[APM_Decision::eAPM_Decision_Moving] = sensor_cfg.eng_on_crank_v_th_24v;
        threshold_24v[APM_Decision::eAPM_Decision_Idling] = sensor_cfg.idle_crank_v_th_24v;

        // 24V Fusion thresholds
        threshold_24v[APM_Decision::eAPM_Decision_Fusion_Engine_Off] = sensor_cfg.fusion_eng_off_crank_v_th_24v;
        threshold_24v[APM_Decision::eAPM_Decision_Fusion_Moving] = sensor_cfg.fusion_eng_on_crank_v_th_24v;
        threshold_24v[APM_Decision::eAPM_Decision_Fusion_Idling] = sensor_cfg.fusion_idle_crank_v_th_24v;

        min_voltage_limit_12V = sensor_cfg.min_v_lim_12v;
        min_voltage_limit_24V = sensor_cfg.min_v_lim_24v;

        validate_thresholds(); // Validate thresholds

        wrn_bad_battery_threshold = crit_bad_battery_threshold = min_voltage_limit_12V; // Initialize to 12V limit
        wrn_bad_battery_threshold +=  ((get_threshold(APM_Decision::eAPM_Decision_Engine_Off) - min_voltage_limit_12V) / 2);
        crit_bad_battery_threshold +=  ((get_threshold(APM_Decision::eAPM_Decision_Engine_Off) - wrn_bad_battery_threshold) / 2);
        LOG_I(TAG_PWR_VOLT, "wrn_bad_battery_threshold %f , crit_bad_battery_threshold %f", wrn_bad_battery_threshold, crit_bad_battery_threshold);
    }

    // By default return the latest data
    inline uint get_decision_count(APM_Decision decision, uint index ) {
        //is index in range.
        return get_data(index).decision[decision];
    }

    inline void set_decision_count(APM_Decision decision, uint index, uint count) {

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

        bool fusion_enabled   = APM::WorkerInstance(eCrankVolt)->is_fusion_enabled();
        bool fusion_activated = APM::WorkerInstance(eCrankVolt)->get_fusion_activated();

        APM_Decision orig_decision = decision;

        if( (true == fusion_enabled) && (true == fusion_activated) ) {
            if(decision == APM_Decision::eAPM_Decision_Engine_Off) {
                decision = APM_Decision::eAPM_Decision_Fusion_Engine_Off;
            }
            else if(decision == APM_Decision::eAPM_Decision_Moving) {
                decision = APM_Decision::eAPM_Decision_Fusion_Moving;
            }
            else if(decision == APM_Decision::eAPM_Decision_Idling) {
                decision = APM_Decision::eAPM_Decision_Fusion_Idling;
            }
            else {}
        }

        if((decision == APM_Decision::eAPM_Decision_Fusion_Engine_Off) || (decision == APM_Decision::eAPM_Decision_Fusion_Moving)) {

            bool ign_valid = (NULL != APM::WorkerInstance(eCrankLine) ? APM::WorkerInstance(eCrankLine)->is_worker_enabled() : false);
            bool imu_valid = (NULL != APM::WorkerInstance(eInertial) ? APM::WorkerInstance(eInertial)->is_worker_enabled() : false);
            bool gps_valid = (NULL != APM::WorkerInstance(eSatellite) ? APM::WorkerInstance(eSatellite)->is_worker_enabled() : false);
            bool can_valid = (NULL != APM::WorkerInstance(eCANBus) ? APM::WorkerInstance(eCANBus)->is_worker_enabled() : false);

                if( (false == ign_valid) &&
                    (false == imu_valid) &&
                    (false == gps_valid) &&
                    (false == can_valid) ) {

                    return ((get_data(index).power_volt >= get_threshold(orig_decision)) && (get_data(index).power_volt < nd_factory_utils::get_max_valid_voltage()) );
                }
                else {

                    // check if last ingested inputs are NOT in data outage bucket for imu, gps and ign
                    // consider a source for fusion only when it's enabled/valid and has no outage
                    bool fusion_ign = (true == ign_valid) ? (APM::WorkerInstance(eCrankLine)->get_data_outage_count() == 0) : false;
                    bool fusion_imu = (true == imu_valid) ? (APM::WorkerInstance(eInertial)->get_data_outage_count() == 0) : false;
                    bool fusion_gps = (true == gps_valid) ? (APM::WorkerInstance(eSatellite)->get_data_outage_count() == 0) : false;
                    bool fusion_can = (true == can_valid) ? (APM::WorkerInstance(eCANBus)->get_data_outage_count() == 0) : false;

                    bool fusion_result = false;

                    fusion_result |= (true == fusion_ign) ? APM::WorkerInstance(eCrankLine)->is_above_threshold(decision) : false;
                    fusion_result |= (true == fusion_imu) ? ( MOVING == APM::WorkerInstance(eInertial)->getEngineState() ) : false; // Get Engine State from IMU
                    fusion_result |= (true == fusion_gps) ? APM::WorkerInstance(eSatellite)->is_above_threshold(decision) : false;
                    fusion_result |= (true == fusion_can) ? APM::WorkerInstance(eCANBus)->is_above_threshold(decision) : false;

                    bool fusion_pwr = ( get_data(index).power_volt >= get_threshold(APM_Decision::eAPM_Decision_Fusion_Engine_Off) ) && ( get_data(index).power_volt < nd_factory_utils::get_max_valid_voltage() );
                    fusion_pwr = (false == is_data_outage(index)) ? fusion_pwr : true;

                    return ( fusion_result && fusion_pwr );

                }
        }
        return ((get_data(index).power_volt >= get_threshold(decision)) && (get_data(index).power_volt < nd_factory_utils::get_max_valid_voltage())) ;
    }


    bool is_24_volt_battery_detected() {
        return is_24_volt_battery;
    }

    void set_24V_battery_status(bool status = true) {
        is_24_volt_battery = status;
        if(true == is_24_volt_battery) {
            is_24V_battery_cnt = PWR_FULL_WINDOW * 2; // Set count to max to avoid flickering
            wrn_bad_battery_threshold = crit_bad_battery_threshold = min_voltage_limit_24V;
        }
        else {
            is_24V_battery_cnt = 0;
            wrn_bad_battery_threshold =  crit_bad_battery_threshold =  min_voltage_limit_12V;
        }

        wrn_bad_battery_threshold +=  ((get_threshold(APM_Decision::eAPM_Decision_Engine_Off) - wrn_bad_battery_threshold) / 2);
        crit_bad_battery_threshold +=  ((get_threshold(APM_Decision::eAPM_Decision_Engine_Off) - wrn_bad_battery_threshold) / 2);

    }

    void update_bad_battery_cnt(uint index) {

        std::string sysfs_path = get_sysfs_path_from_enum(PowermonParam::eBAD_BATTERY_CNT);

        if(get_data(index).power_volt < wrn_bad_battery_threshold) {

            if(bad_battery_count == MAX_BAD_BATTERY_COUNT) {
                if (false == is_bad_battery) {
                    BatteryState state = (get_data(index).power_volt < crit_bad_battery_threshold) ? BatteryState::eBatteryCritical : BatteryState::eBatteryWarning;
                    write_into_dev_shm_file(sysfs_path, static_cast<uint64_t>(state));
                }
                bad_battery_count = (MAX_BAD_BATTERY_COUNT * 2); // to avoid multiple writes
                is_bad_battery = true;
            }
            else if(get_data(index).power_volt < crit_bad_battery_threshold) {
                if(bad_battery_count == (MAX_BAD_BATTERY_COUNT * 2)) {
                    if (false == is_bad_battery) {
                        write_into_dev_shm_file(sysfs_path, static_cast<uint64_t>(BatteryState::eBatteryCritical));
                    }
                    bad_battery_count = (MAX_BAD_BATTERY_COUNT * 3); // to avoid multiple writes
                    is_bad_battery = true;
                }
                else if(bad_battery_count < (MAX_BAD_BATTERY_COUNT * 2)) {
                    bad_battery_count++; // increment bad battery count, and do not exceed max limit
                }
            }
            else if(bad_battery_count < (MAX_BAD_BATTERY_COUNT * 2)) {
                bad_battery_count++; // increment bad battery count, and do not exceed max limit
            }
            else if(bad_battery_count > (MAX_BAD_BATTERY_COUNT * 2)){
                    bad_battery_count--;
            }
            else {}
        }
        else {
            if(bad_battery_count > 0) {
                bad_battery_count--;
            }
            if(bad_battery_count == MAX_BAD_BATTERY_COUNT) {
                write_into_dev_shm_file(sysfs_path, static_cast<uint64_t>(BatteryState::eBatteryNormal));
                bad_battery_count = 0;
                is_bad_battery = false;
            }
        }

    }

    inline bool is_data_outage(uint index) {
	    if( (get_data(index).power_volt < nd_factory_utils::get_min_valid_voltage()) || (get_data(index).power_volt > nd_factory_utils::get_max_valid_voltage())) {
		    return true;
	    }

        if(get_data(index).power_volt > min_voltage_limit_24V) {
            is_24V_battery_cnt++;
            if(is_24V_battery_cnt > (PWR_FULL_WINDOW * 2)) {
                set_24V_battery_status(true);
                apm_attr_util::set_attr_status(vehicle_attributes, USER_24V_STR, TRUE_STR);
            }
        }
        else {
            if(is_24V_battery_cnt > 0) {
                is_24V_battery_cnt--;
            }
            else if((0 == is_24V_battery_cnt) && (true == is_24_volt_battery_detected())) {
                set_24V_battery_status(false);
                apm_attr_util::set_attr_status(vehicle_attributes, USER_24V_STR, FALSE_STR);
            }
        }

        update_bad_battery_cnt(index);

	    return false;
    }


    inline void set_threshold(APM_Decision decision, float thresh) {

        if(false == apm_worker_utils::is_decision_valid(decision)) {
            return;
        }

        threshold[decision] = thresh;
    }

    inline float get_threshold(APM_Decision decision) {

        if(false == apm_worker_utils::is_decision_valid(decision)) {
            return ENGINE_OFF_CRANK_VOLTAGE_THRESHOLD;
        }

        if(true == is_24_volt_battery_detected()) {
            return threshold_24v[decision];
        }
        else {
            return threshold[decision];
        }

        return threshold[decision];
    }

   //Local Method
   // By default return the latest data
    inline VoltData get_data(uint index ) {
        if(apm_worker_utils::is_index_valid(index, PWR_CACHE_WINDOW) == false) {
            // LOG_E(TAG, "Invalid index requested: %u", index);
            return VoltData();
        }
	    //is index in  valid range
	    return data[index];
    }

    inline void set_bin_data(uint index, void* volt_data ) {
        //is index in  valid range
        VoltData* v_data = static_cast<VoltData*>(volt_data);
        data[index].power_volt = v_data->power_volt;
        data[index].drp_volt = v_data->drp_volt;
    }

    inline string get_data_string(uint index) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(3);

        oss << "VOLT: C: " << data[index].power_volt << ","
            << "I: " << data[index].drp_volt
            << ", is24V: " << (is_24_volt_battery ? "true" : "false")
            << ", BadBattery: " << (is_bad_battery ? "true" : "false")
            << ", FusionActive: " << (APM::WorkerInstance(eCrankVolt)->get_fusion_activated() ? "true" : "false");

        return oss.str();
    }

    inline void bin_data() {

        APM_Decision bin_decision =  ( NULL != APM::WorkerInstance(eSatellite) ? (MOVING  == APM::WorkerInstance(eSatellite)->getMotionState()) : false) ? eAPM_Decision_Moving : eAPM_Decision_Engine_Off;
    }

    inline std::string get_valid_sysfs_path() {
        return get_sysfs_path_from_enum(PowermonParam::eADC_VALID);
    }

    inline void mark_data_invalid(uint index) {

        if(false == apm_worker_utils::is_index_valid(index, PWR_CACHE_WINDOW) ) {
            LOG_E(TAG_PWR_VOLT, "Invalid index to mark data valid: %u", index);
            return;
        }
        // mark data as in valid for the given index
        data[index].power_volt = static_cast<float>(SysfsErrorCodes::eSYSFS_INVALID_DATA);
        data[index].drp_volt   = static_cast<float>(SysfsErrorCodes::eSYSFS_INVALID_DATA);
    }

    inline bool is_idling(uint index) {
        if(apm_worker_utils::is_index_valid(index, PWR_CACHE_WINDOW) == false) {
            LOG_E(TAG_PWR_VOLT, "is_idling Invalid index requested: %u", index);
            return false;
        }
        if(data[index].power_volt < get_threshold(APM_Decision::eAPM_Decision_Moving) &&
           data[index].power_volt >= get_threshold(APM_Decision::eAPM_Decision_Idling)) {
            return true;
        }
        return false;
    }
    // Definition present in apm_power_volt.cpp
    void update_calibration_data(uint index) ;

    bool is_valid_threshold(float thr) {

        if((thr < nd_factory_utils::get_min_valid_voltage()) || (thr >  nd_factory_utils::get_max_valid_voltage())) {
            return false;
        }
        return true;
    }

    void validate_or_set_default(float &value, float def) {
        if(false == is_valid_threshold(value)) {
            value = def;
        }
    }

    void ensure_order(float &high, float &low, float offset) {
        if(high <= low) {
            high = low + offset;
        }
    }

    void validate_thresholds() {

        // Validation for non-fusion thresholds
        validate_or_set_default(threshold[APM_Decision::eAPM_Decision_Engine_Off], ENGINE_OFF_CRANK_VOLTAGE_THRESHOLD);
        validate_or_set_default(threshold_24v[APM_Decision::eAPM_Decision_Engine_Off], ENGINE_OFF_CRANK_VOLTAGE_THRESHOLD_24V);
        validate_or_set_default(threshold[APM_Decision::eAPM_Decision_Moving], ENGINE_ON_CRANK_VOLTAGE_THRESHOLD);
        validate_or_set_default(threshold_24v[APM_Decision::eAPM_Decision_Moving], ENGINE_ON_CRANK_VOLTAGE_THRESHOLD_24V);
        validate_or_set_default(threshold[APM_Decision::eAPM_Decision_Idling], ENGINE_IDLING_CRANK_VOLTAGE_THRESHOLD);
        validate_or_set_default(threshold_24v[APM_Decision::eAPM_Decision_Idling], ENGINE_IDLING_CRANK_VOLTAGE_THRESHOLD_24V);

        // Proper Order Checking
        ensure_order(threshold[APM_Decision::eAPM_Decision_Moving],
                        threshold[APM_Decision::eAPM_Decision_Engine_Off],
                        (ENGINE_ON_CRANK_VOLTAGE_THRESHOLD - ENGINE_OFF_CRANK_VOLTAGE_THRESHOLD));

        ensure_order(threshold_24v[APM_Decision::eAPM_Decision_Moving],
                        threshold_24v[APM_Decision::eAPM_Decision_Engine_Off],
                        (ENGINE_ON_CRANK_VOLTAGE_THRESHOLD_24V - ENGINE_OFF_CRANK_VOLTAGE_THRESHOLD_24V));


        // validation for fusion thresholds
        validate_or_set_default(threshold[APM_Decision::eAPM_Decision_Fusion_Engine_Off], DEF_ENGINE_OFF_CRANK_VOLTAGE_THRESHOLD_FUSION);
        validate_or_set_default(threshold_24v[APM_Decision::eAPM_Decision_Fusion_Engine_Off], DEF_ENGINE_OFF_CRANK_VOLTAGE_THRESHOLD_24V_FUSION);
        validate_or_set_default(threshold[APM_Decision::eAPM_Decision_Fusion_Moving], threshold[APM_Decision::eAPM_Decision_Fusion_Engine_Off]);
        validate_or_set_default(threshold_24v[APM_Decision::eAPM_Decision_Fusion_Moving], threshold_24v[APM_Decision::eAPM_Decision_Fusion_Engine_Off]);
        validate_or_set_default(threshold[APM_Decision::eAPM_Decision_Fusion_Idling], threshold[APM_Decision::eAPM_Decision_Fusion_Idling]);
        validate_or_set_default(threshold_24v[APM_Decision::eAPM_Decision_Fusion_Idling], threshold_24v[APM_Decision::eAPM_Decision_Fusion_Idling]);

        // Proper Order Checking for fusion thresholds
        ensure_order(threshold[APM_Decision::eAPM_Decision_Fusion_Moving],
                        threshold[APM_Decision::eAPM_Decision_Fusion_Engine_Off],
                        (DEF_ENGINE_ON_CRANK_VOLTAGE_THRESHOLD_FUSION - DEF_ENGINE_OFF_CRANK_VOLTAGE_THRESHOLD_FUSION));

        ensure_order(threshold_24v[APM_Decision::eAPM_Decision_Fusion_Moving],
                        threshold_24v[APM_Decision::eAPM_Decision_Fusion_Engine_Off],
                        (DEF_ENGINE_ON_CRANK_VOLTAGE_THRESHOLD_24V_FUSION - DEF_ENGINE_OFF_CRANK_VOLTAGE_THRESHOLD_24V_FUSION));

        ensure_order(threshold[APM_Decision::eAPM_Decision_Fusion_Idling],
                        threshold[APM_Decision::eAPM_Decision_Fusion_Moving],
                        (DEF_ENGINE_IDLING_CRANK_VOLTAGE_THRESHOLD_FUSION - DEF_ENGINE_ON_CRANK_VOLTAGE_THRESHOLD_FUSION));

        ensure_order(threshold_24v[APM_Decision::eAPM_Decision_Fusion_Idling],
                        threshold_24v[APM_Decision::eAPM_Decision_Fusion_Moving],
                        (DEF_ENGINE_IDLING_CRANK_VOLTAGE_THRESHOLD_24V_FUSION - DEF_ENGINE_ON_CRANK_VOLTAGE_THRESHOLD_24V_FUSION));

        // validate min and max thresholds from power monitor section
        if(min_voltage_limit_12V < MIN_POWER_VOLTAGE_THRESHOLD) {
            min_voltage_limit_12V = DEF_MIN_POWER_VOLTAGE;
        }

        constexpr float MIN_VOLT_DIFF = 5.0; // Minimum voltage difference between 12V and 24V systems
        float volt_diff = min_voltage_limit_24V - min_voltage_limit_12V;
        if((min_voltage_limit_24V < MIN_POWER_VOLTAGE_THRESHOLD_24V) || (volt_diff <= MIN_VOLT_DIFF)) {
            min_voltage_limit_24V = DEF_MIN_POWER_VOLTAGE_24V;
        }
    }

    std::string get_thres_string() {
        std::ostringstream oss;
        oss << "PWR_Thres[";
        oss << " Off: " << std::fixed << std::setprecision(3) << threshold[APM_Decision::eAPM_Decision_Engine_Off];
        oss << ", On: " << std::fixed << std::setprecision(3) << threshold[APM_Decision::eAPM_Decision_Moving];
        oss << ", Idle: " << std::fixed << std::setprecision(3) << threshold[APM_Decision::eAPM_Decision_Idling];
        oss << ", Fusion_Off: " << std::fixed << std::setprecision(3) << threshold[APM_Decision::eAPM_Decision_Fusion_Engine_Off];
        oss << ", Fusion_On: " << std::fixed << std::setprecision(3) << threshold[APM_Decision::eAPM_Decision_Fusion_Moving];
        oss << ", Type: " << (is_24_volt_battery ? "24V" : "12V");
        oss << ", MinV: " << (is_24_volt_battery ? min_voltage_limit_24V : min_voltage_limit_12V);
        oss << " ]";
        return oss.str();
    }

    private:
    VoltData data[PWR_CACHE_WINDOW];
    float threshold[APM_Decision::eAPM_Decision_Max] = {0.0};
    float threshold_24v[APM_Decision::eAPM_Decision_Max] = {0.0};
    float min_voltage_limit_12V = DEF_MIN_POWER_VOLTAGE;
    float min_voltage_limit_24V = DEF_MIN_POWER_VOLTAGE_24V;
    bool is_24_volt_battery = false;
    uint is_24V_battery_cnt = 0;
    float wrn_bad_battery_threshold = 0.0;
    float crit_bad_battery_threshold = 0.0;
    uint64_t bad_battery_count = 0;
    bool is_bad_battery = false;
    float supercap_threshold = DEF_SUPERCAP_LOWER_THRESHOLD; // Supercap lower threshold, this will be used to trigger supercap event for krait platforms

    float fusion_avg_voltage_abv = 0.0;
    float fusion_avg_voltage_below = 0.0;
    int prev_ign_state = -1; // Is ignition previously ON 1 or OFF 0
};

class POWER_VOLT_worker : public APM_worker {


    private:


    public:
    POWER_VOLT_worker(sensor_config_t sensor_cfg,
                        bool legacy_mode, bool disable_sensor, int power_dec_window[eDecisionWindow_Max]) {

        setWorkerBinDataObj(new VoltBinData(sensor_cfg));

        if(getWorkerBinDataObj() != NULL ) {
            // Initialize decision intervals
            set_decison_interval(eDecisionWindow_Short, power_dec_window[eDecisionWindow_Short]);
            set_decison_interval(eDecisionWindow_Long,  power_dec_window[eDecisionWindow_Long]);
            set_decison_interval(eDecisionWindow_Valid, power_dec_window[eDecisionWindow_Short]);
            if(power_dec_window[eDecisionWindow_Outage] > PWR_CACHE_WINDOW) {
                LOG_W(get_TAG(), "Outage decision window %u is greater than cache window %u, setting it to cache window", power_dec_window[eDecisionWindow_Outage], PWR_CACHE_WINDOW);
                power_dec_window[eDecisionWindow_Outage] = PWR_DATA_OUTAGE_THRESHOLD;
            }
            set_decison_interval(eDecisionWindow_Outage, power_dec_window[eDecisionWindow_Outage]);
            set_decison_interval(eDecisionWindow_Sleep,  POWER_VOLT_READ_INTERVAL_SEC);
            set_decison_interval(eDecisionWindow_Median, power_dec_window[eDecisionWindow_Median]);
            if(power_dec_window[eDecisionWindow_Continuous] > PWR_CACHE_WINDOW) {
                LOG_W(get_TAG(), "Continuous decision window %u is greater than cache window %u, setting it to default continuous window", power_dec_window[eDecisionWindow_Continuous], PWR_CACHE_WINDOW);
                power_dec_window[eDecisionWindow_Continuous] = PWR_CONTINUOUS_DEC_WINDOW;
            }
            set_decison_interval(eDecisionWindow_Continuous, power_dec_window[eDecisionWindow_Continuous]);
            set_decison_interval(eDecisionWindow_Cache, PWR_CACHE_WINDOW);
            // Set legacy mode
            use_legacy = legacy_mode;
            fusion_enabled = sensor_cfg.pwr_volt_fusion_enable;
            set_fusion_activated(true);
            sensor_decision_disabled = disable_sensor;
        } else {
            LOG_C(get_TAG(), "%s BinDataObj is NULL!!!!. Falling back to Legacy");
            use_legacy = true;
        }

        initMotionState();
    }

    bool extend_misc_file_created = false;
    bool increment_self_gen_count = false;
    bool glitch_reported = false;
    bool update_reader_count = false;

    poll_func_t poll_func;
    intr_func_t intr_func;
    filter_func_t filter_func;
    read_status_t read_status;

    void detect_engine_status();
    void insert_bin_data();
    void filter_func_new();
    void crank_glitch_report();

    int get_signal_mask() {
        return POWER_VOLT_MASK_POS;
    }

    const char* get_TAG() {
        return TAG_PWR_VOLT;
    }

    int get_ign_src_type(){
        return IgnitonSource::eCrankVolt;
    }

    inline void update_calibration_data();

    float get_threshold(APM_Decision decision) {
        return getWorkerBinDataObj()->get_threshold(decision);
    }

    bool check_battery_status_file() {

        if((true == is_bin_data_valid) && ("true" == apm_attr_util::get_attr_status(vehicle_attributes, USER_24V_STR, FALSE_STR))) {
            getWorkerBinDataObj()->set_24V_battery_status();
        }

        return true;
    }

    std::string get_src_status_sysfs_path() {
        return get_sysfs_path_from_enum(PowermonParam::eCRANK_IGN);
    }

    std::string get_attr_user() {
        return USER_PWR;
    }

    bool handle_data_outage();

    void initMotionState() {
        motion_status_t motion_status = STATIONARY;
        motion_status = read_ign_state_from_sysfs();
        setMotionState(motion_status);
    }

    void setMotionState(motion_status_t motion_status) {

        // In case of data outage we fallback on other ignition sources
        if(true == is_data_outage()) {

            bool ign_valid = (NULL != APM::WorkerInstance(eCrankLine)) ? APM::WorkerInstance(eCrankLine)->is_worker_enabled() : false;
            bool imu_valid = (NULL != APM::WorkerInstance(eInertial)) ? APM::WorkerInstance(eInertial)->is_worker_enabled() : false;
            bool gps_valid = (NULL != APM::WorkerInstance(eSatellite)) ? APM::WorkerInstance(eSatellite)->is_worker_enabled() : false;
            bool can_valid = (NULL != APM::WorkerInstance(eCANBus)) ? APM::WorkerInstance(eCANBus)->is_worker_enabled() : false;

            bool fusion_ign = (true == ign_valid) ? (APM::WorkerInstance(eCrankLine)->get_data_outage_count() == 0) : false;
            bool fusion_imu = (true == imu_valid) ? (APM::WorkerInstance(eInertial)->get_data_outage_count() == 0) : false;
            bool fusion_gps = (true == gps_valid) ? (APM::WorkerInstance(eSatellite)->get_data_outage_count() == 0) : false;
            bool fusion_can = (true == can_valid) ? (APM::WorkerInstance(eCANBus)->get_data_outage_count() == 0) : false;

            bool fusion_result = false;

            fusion_result |= (true == fusion_ign) ? ( MOVING == APM::WorkerInstance(eCrankLine)->getMotionState() ) : false;
            fusion_result |= (true == fusion_imu) ? ( MOVING == APM::WorkerInstance(eInertial)->getEngineState() ) : false; // Get Engine State from IMU
            fusion_result |= (true == fusion_gps) ? ( MOVING == APM::WorkerInstance(eSatellite)->getMotionState() ) : false;
            fusion_result |= (true == fusion_can) ? ( MOVING == APM::WorkerInstance(eCANBus)->getMotionState() ) : false;

            motion_status = (fusion_result) ? MOVING : STATIONARY;
        }
        APM_worker::setMotionState(motion_status);
    }

};

#endif
