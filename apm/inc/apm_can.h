#ifndef _APM_CAN_H_
#define _APM_CAN_H_

#include "apm.h"

#define CAN_ENGINE_STATUS_OFF       0
#define CAN_ENGINE_STATUS_ON        1
#define CAN_ENGINE_STATUS_INVALID   3

#define TAG_CAN "A_CAN"


constexpr uint CAN_SHORT_WINDOW = 10; // seconds
constexpr uint CAN_FULL_WINDOW = 200; // seconds
constexpr uint CAN_DATA_OUTAGE_THRESHOLD = 60; // seconds
constexpr uint CAN_DATA_READ_INTERVAL_SEC = 1; // seconds
constexpr uint CAN_MEDIAN_DEC_WINDOW = 70; // seconds close to 1/3 of full window
constexpr uint CAN_CONTINUOUS_DEC_WINDOW = (CAN_SHORT_WINDOW / 2); // seconds, continuous decision window is half of short window

constexpr uint DEFAULT_CAN_DATA = 10000;
constexpr uint CAN_DATA_VALIDITY_THRESHOLD = CAN_SHORT_WINDOW; // Sec

// CAN RPM threshold offsets for decision boundaries
constexpr uint CAN_ENG_OFF_RPM_OFFSET = 50;  // RPM offset below threshold for Engine OFF detection
constexpr uint CAN_ENG_ON_RPM_OFFSET = 100;  // RPM offset above threshold for IDLING detection
constexpr uint CAN_RPM_THRESHOLD = 350;

// Early Detection Window
constexpr uint CAN_FULL_WINDOW_ED = 60; // seconds

constexpr uint CAN_CACHE_WINDOW = CAN_FULL_WINDOW; // seconds, cache window to store CAN data


enum CanDataIndex {
    eCAN_DATA_RPM,
    eCAN_DATA_ENG_STAT,
    eCAN_DATA_SPEED,
    eCAN_DATA_MAX
};
struct CanData {
    uint decision[APM_Decision::eAPM_Decision_Max] = {0};
    uint can_data[eCAN_DATA_MAX] = {DEFAULT_CAN_DATA};
};

class CanBinData : public WorkerBinData {

    public:
    CanBinData(sensor_config_t sensor_cfg) {
        threshold[APM_Decision::eAPM_Decision_Engine_Off] = sensor_cfg.can_rpm_engine_off_threshold;
        threshold[APM_Decision::eAPM_Decision_Moving] = sensor_cfg.can_rpm_threshold;
        threshold[APM_Decision::eAPM_Decision_Idling] = sensor_cfg.can_rpm_threshold;
        validate_thresholds();
    }

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
        if(apm_worker_utils::is_index_valid(index, CAN_CACHE_WINDOW) == false) {
            LOG_E(TAG_CAN, "set_decision_count Invalid index requested: %u", index);
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

        // If decision is for fusion, then check if fusion is activated or not before applying threshold
        // So whoever is calling this API for fusion decision, should get status based on fusion activation.
        if( (decision >= APM_Decision::eAPM_Decision_Fusion_Engine_Off) && (decision <= APM_Decision::eAPM_Decision_Fusion_Idling) ) {
            if(false == APM::WorkerInstance(eCANBus)->get_fusion_activated()) {
                return false;
            }
        }

        return ( (get_data(index).can_data[eCAN_DATA_RPM] >= get_threshold(decision)) &&  (get_data(index).can_data[eCAN_DATA_RPM] < DEFAULT_CAN_DATA) );
    }

    inline bool is_data_outage(uint index) {
        if( (get_data(index).can_data[eCAN_DATA_RPM] < SysfsErrorCodes::eSYSFS_RET_ZERO) ||
                (get_data(index).can_data[eCAN_DATA_ENG_STAT] < SysfsErrorCodes::eSYSFS_RET_ZERO) ||
                (get_data(index).can_data[eCAN_DATA_SPEED] < SysfsErrorCodes::eSYSFS_RET_ZERO) ||
                (get_data(index).can_data[eCAN_DATA_RPM] >= DEFAULT_CAN_DATA) ||
                (get_data(index).can_data[eCAN_DATA_ENG_STAT] >= DEFAULT_CAN_DATA) ||
                (get_data(index).can_data[eCAN_DATA_SPEED] >= DEFAULT_CAN_DATA)) {
            return true;
        }
        return false;
    }

    inline void set_threshold(APM_Decision decision, uint thresh) {

        if(false == apm_worker_utils::is_decision_valid(decision)) {
            return;
        }
        threshold[decision] = thresh;
    }

    inline float get_threshold(APM_Decision decision) {

        if(false == apm_worker_utils::is_decision_valid(decision)) {
            return CAN_ENG_OFF_RPM_OFFSET; // TODO: CHECK VALUE
        }
        return threshold[decision];
    }
//handle Index
    inline CanData get_data(uint index ) {
        if(apm_worker_utils::is_index_valid(index, CAN_CACHE_WINDOW) == false) {
            // LOG_E(TAG, "Invalid index requested: %u", index);
            return CanData();
        }

        return data[index];
    }

    inline void set_bin_data(uint index, void* can_data ) {
        if(apm_worker_utils::is_index_valid(index, CAN_CACHE_WINDOW) == false) {
            LOG_E(TAG_CAN, "Invalid index requested: %u", index);
            return;
        }

        CanData* c_data = static_cast<CanData*>(can_data);
        for(int i = 0; i < eCAN_DATA_MAX; i++) {
            data[index].can_data[i] = c_data->can_data[i];
        }
    }

    inline std::string get_valid_sysfs_path() {
        return get_sysfs_path_from_enum(PowermonParam::eOBD_VALID);
    }

    inline string get_data_string(uint index){
        if(apm_worker_utils::is_index_valid(index, CAN_CACHE_WINDOW) == false) {
            LOG_E(TAG_CAN, "get_data_string Invalid index requested: %u", index);
            return "INVALID Index";
        }

	    string data_str = "";
	    data_str = "CAN: RPM: " + to_string(data[index].can_data[eCAN_DATA_RPM]) + "," +
		    "ENG_S: " + to_string(data[index].can_data[eCAN_DATA_ENG_STAT]) + "," +
		    "SP: " + to_string(data[index].can_data[eCAN_DATA_SPEED]);
	    return data_str;
    }

    inline void mark_data_invalid(uint index) {
        if(false == apm_worker_utils::is_index_valid(index, CAN_CACHE_WINDOW) ) {
            LOG_E(TAG_CAN, "Invalid index to mark data valid: %u", index);
            return;
        }

        // mark data as in valid for the given index
        data[index].can_data[eCAN_DATA_RPM] = DEFAULT_CAN_DATA;
        data[index].can_data[eCAN_DATA_ENG_STAT] = DEFAULT_CAN_DATA;
        data[index].can_data[eCAN_DATA_SPEED] = DEFAULT_CAN_DATA;
    }

    bool is_idling(uint index){
        // Idling not applicable for CAN currently
        return false;
    }

    void validate_thresholds() {
        // validate RPM thresholds
        if((threshold[APM_Decision::eAPM_Decision_Engine_Off] < MIN_CAN_RPM_THRESHOLD) || (threshold[APM_Decision::eAPM_Decision_Engine_Off] > MAX_CAN_RPM_THRESHOLD)) {
            LOG_E(TAG_CAN, "Invalid CAN Engine Off RPM threshold: %f. Resetting to default %d", threshold[APM_Decision::eAPM_Decision_Engine_Off], DEF_CAN_RPM_ENGINE_OFF_THRESHOLD);
            threshold[APM_Decision::eAPM_Decision_Engine_Off] = DEF_CAN_RPM_ENGINE_OFF_THRESHOLD;
        }

        if((threshold[APM_Decision::eAPM_Decision_Moving] < MIN_CAN_RPM_THRESHOLD) || (threshold[APM_Decision::eAPM_Decision_Moving] > MAX_CAN_RPM_THRESHOLD)) {
            LOG_E(TAG_CAN, "Invalid CAN Moving RPM threshold: %f. Resetting to default %d", threshold[APM_Decision::eAPM_Decision_Moving], CAN_RPM_THRESHOLD);
            threshold[APM_Decision::eAPM_Decision_Moving] = CAN_RPM_THRESHOLD;
        }

        if((threshold[APM_Decision::eAPM_Decision_Idling] < MIN_CAN_RPM_THRESHOLD) || (threshold[APM_Decision::eAPM_Decision_Idling] > MAX_CAN_RPM_THRESHOLD)) {
            LOG_E(TAG_CAN, "Invalid CAN Idling RPM threshold: %f. Resetting to default %d", threshold[APM_Decision::eAPM_Decision_Idling], CAN_RPM_THRESHOLD);
            threshold[APM_Decision::eAPM_Decision_Idling] = CAN_RPM_THRESHOLD;
        }
        return;
    }

    std::string get_thres_string() {
        std::ostringstream oss;
        oss << "CAN_Thres[";
        oss << " RPM_Mov: " << std::fixed << std::setprecision(3) << threshold[APM_Decision::eAPM_Decision_Moving];
        oss << ", RPM_Off: " << std::fixed << std::setprecision(3) << threshold[APM_Decision::eAPM_Decision_Engine_Off];
        oss << ", RPM_Idle: " << std::fixed << std::setprecision(3) << threshold[APM_Decision::eAPM_Decision_Idling];
        oss << " ]";
        return oss.str();
    }

    private:
    CanData data[CAN_CACHE_WINDOW];
    float threshold[APM_Decision::eAPM_Decision_Max] = {0};
};

class CAN_worker : public APM_worker {
    public:

    CAN_worker(sensor_config_t sensor_cfg, bool legacy_mode, bool disable_sensor, int can_dec_window[eDecisionWindow_Max]) {
        setWorkerBinDataObj(new CanBinData(sensor_cfg));

        if(getWorkerBinDataObj() != NULL) {
            // Initialize decision intervals
            set_decison_interval(eDecisionWindow_Short, can_dec_window[eDecisionWindow_Short]);
            set_decison_interval(eDecisionWindow_Long,  can_dec_window[eDecisionWindow_Long]);
            set_decison_interval(eDecisionWindow_Valid, can_dec_window[eDecisionWindow_Short]);
            if(can_dec_window[eDecisionWindow_Outage] > CAN_CACHE_WINDOW) {
                LOG_W(get_TAG(), "Outage decision window %u is greater than cache window %u, setting it to default window", can_dec_window[eDecisionWindow_Outage], CAN_CACHE_WINDOW);
                can_dec_window[eDecisionWindow_Outage] = CAN_DATA_OUTAGE_THRESHOLD;
            }
            set_decison_interval(eDecisionWindow_Outage, can_dec_window[eDecisionWindow_Outage]);
            set_decison_interval(eDecisionWindow_Sleep,  CAN_DATA_READ_INTERVAL_SEC);
            set_decison_interval(eDecisionWindow_Median, can_dec_window[eDecisionWindow_Median]);
            if(can_dec_window[eDecisionWindow_Continuous] > CAN_CACHE_WINDOW) {
                LOG_W(get_TAG(), "Continuous decision window %u is greater than cache window %u, setting it to default window", can_dec_window[eDecisionWindow_Continuous], CAN_CACHE_WINDOW);
                can_dec_window[eDecisionWindow_Continuous] = CAN_CONTINUOUS_DEC_WINDOW;
            }
            set_decison_interval(eDecisionWindow_Continuous, can_dec_window[eDecisionWindow_Continuous]);
            set_decison_interval(eDecisionWindow_Cache, CAN_CACHE_WINDOW);
            // Default to legacy mode
            use_legacy = legacy_mode;
            sensor_decision_disabled = disable_sensor;
        } else {
            LOG_C(get_TAG(), "%s BinDataObj is NULL!!!!. Falling back to Legacy");
            use_legacy = true;
        }
        initMotionState();
    }

    poll_func_t poll_func;
    intr_func_t intr_func;
    filter_func_t filter_func;
    read_status_t read_status;

    bool update_reader_count = false;

    void insert_bin_data();

    int get_signal_mask() {
        return CAN_MASK_POS;
    }

    const char* get_TAG();

    int get_ign_src_type(){
        return IgnitonSource::eCANBus;
    }

    std::string get_src_status_sysfs_path() {
        return get_sysfs_path_from_enum(PowermonParam::eCAN_IGN);
    }

    std::string get_attr_user() {
        return USER_CAN;
    }
};

#endif
