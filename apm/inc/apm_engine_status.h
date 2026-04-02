#ifndef __APM_ENGINE_STATUS_H__
#define __APM_ENGINE_STATUS_H__

#include "apm.h"
#include "apm_worker.h"

#define TAG_ENGINE_STATUS "A_ENG_STAT"

#define DEF_SAMPLE_RATE 20 // 20 Hz sample rate from IMU for engine status decision

constexpr uint ENG_STAT_SHORT_WINDOW = 5; // seconds
constexpr uint ENG_STAT_FULL_WINDOW = 10; // seconds
constexpr uint ENG_STAT_READ_INTERVAL_SEC = 1; // seconds, defined as dummy will not be used
constexpr uint ENG_STAT_DATA_OUTAGE_THRESHOLD = 10; //Outage window uses same logic as IMU only threshold is less
constexpr uint ENG_STAT_MEDIAN_DEC_WINDOW = 6; // seconds .IMU_FULL_WINDOW / 3
constexpr uint ENG_STAT_CONTINUOUS_DEC_WINDOW = (ENG_STAT_SHORT_WINDOW / 2); // seconds, continuous decision window is half of short window

constexpr uint ENG_STAT_FULL_WINDOW_SAMPLES = ENG_STAT_FULL_WINDOW * DEF_SAMPLE_RATE;

enum eENG_STAT_Axis_Data {
    eENG_STAT_Accel_X = 0,
    eENG_STAT_Accel_Y,
    eENG_STAT_Accel_Z,
    eENG_STAT_Gyro_X,
    eENG_STAT_Gyro_Y,
    eENG_STAT_Gyro_Z,
    eENG_STAT_Accel_RMS,
    eENG_STAT_Gyro_RMS,
    eENG_STAT_Accel_RMS_Delta,
	eENG_STAT_Accel_RMS_Delta_Factor,
    eENG_STAT_Gyro_RMS_Delta,
	eENG_STAT_Gyro_RMS_Delta_Factor,
	eENG_STAT_Motion_Score,
	eENG_STAT_Motion_Score_Factor,
    eENG_STAT_AxisMax,
};
struct EngStatData {
    uint decision[APM_Decision::eAPM_Decision_Max] = {0};
    float axis[eENG_STAT_AxisMax] = {static_cast<float>(SysfsErrorCodes::eSYSFS_INVALID_DATA)}; // Axis data represents max of all 6 axes
};
class EngStatBinData : public WorkerBinData {

    public:
	EngStatBinData(eng_stat_thresholds eng_stat_thr) {
		// Accel RMS thresholds (not used) 
		threshold[eENG_STAT_Accel_RMS][APM_Decision::eAPM_Decision_Moving] = eng_stat_thr.imu_accel_rms_on_threshold;
		threshold[eENG_STAT_Accel_RMS][APM_Decision::eAPM_Decision_Engine_Off] = eng_stat_thr.imu_accel_rms_off_threshold;
		// Gyro RMS thresholds (not used)
		threshold[eENG_STAT_Gyro_RMS][APM_Decision::eAPM_Decision_Moving] = eng_stat_thr.imu_gyro_rms_on_threshold;
		threshold[eENG_STAT_Gyro_RMS][APM_Decision::eAPM_Decision_Engine_Off] = eng_stat_thr.imu_gyro_rms_off_threshold;
		// ACCEL RMS Delta thresholds (used for engine status decision in fusion mode)
		threshold[eENG_STAT_Accel_RMS_Delta][APM_Decision::eAPM_Decision_Moving] = eng_stat_thr.imu_accel_rms_on_threshold;
		threshold[eENG_STAT_Accel_RMS_Delta][APM_Decision::eAPM_Decision_Engine_Off] = eng_stat_thr.imu_accel_rms_off_threshold;
		// GYRO RMS Delta thresholds (used for engine status decision in fusion mode)
		threshold[eENG_STAT_Gyro_RMS_Delta][APM_Decision::eAPM_Decision_Moving] = eng_stat_thr.imu_gyro_rms_on_threshold;
		threshold[eENG_STAT_Gyro_RMS_Delta][APM_Decision::eAPM_Decision_Engine_Off] = eng_stat_thr.imu_gyro_rms_off_threshold;
		// Motion Score thresholds (used for engine status decision in fusion mode)
		threshold[eENG_STAT_Motion_Score][APM_Decision::eAPM_Decision_Moving] = eng_stat_thr.imu_motion_score_on_threshold;
		threshold[eENG_STAT_Motion_Score][APM_Decision::eAPM_Decision_Engine_Off] = eng_stat_thr.imu_motion_score_off_threshold;
		// In Idling updating precentage  change threshold
		threshold[eENG_STAT_Accel_RMS_Delta_Factor][APM_Decision::eAPM_Decision_Idling] = eng_stat_thr.imu_rms_percentage_threshold;
		threshold[eENG_STAT_Gyro_RMS_Delta_Factor][APM_Decision::eAPM_Decision_Idling] = eng_stat_thr.imu_rms_percentage_threshold;
		threshold[eENG_STAT_Motion_Score_Factor][APM_Decision::eAPM_Decision_Idling] = eng_stat_thr.imu_rms_percentage_threshold;
		validate_thresholds();
		LOG_I(TAG_ENGINE_STATUS, "EngStatBinData initialized with thresholds: %s", get_thres_string().c_str());
	}

    inline EngStatData get_data(uint index ) {
        if(apm_worker_utils::is_index_valid(index, ENG_STAT_FULL_WINDOW_SAMPLES) == false) {
            LOG_E("A_ENG_STAT", "Invalid index requested: %u", index);
            return EngStatData();
        }
        //is index in  valid range
        return data[index];
    }

    inline void set_bin_data(uint index, void* eng_stat_data ) {

        if(apm_worker_utils::is_index_valid(index, ENG_STAT_FULL_WINDOW_SAMPLES) == false) {
            LOG_E("A_ENG_STAT", "Invalid index requested: %u", index);
            return;
        }

        EngStatData* eng_stat_data_ptr = static_cast<EngStatData*>(eng_stat_data);
        data[index] = *eng_stat_data_ptr;
    }

    virtual  float get_threshold(APM_Decision decision){
		if(false == apm_worker_utils::is_decision_valid(decision)) {
			return DEF_IMU_THRESHOLD;
		}

		return threshold[eENG_STAT_Accel_X][decision];	
	}

	inline string get_data_string(uint index) {
		if(apm_worker_utils::is_index_valid(index, ENG_STAT_FULL_WINDOW_SAMPLES) == false) {
			LOG_E("A_ENG_STAT", "Invalid index requested: %u", index);
			return "";
		}
		EngStatData eng_stat_data = get_data(index);
		std::ostringstream oss;
		oss // << " A:" << eng_stat_data.axis[eENG_STAT_Accel_X] << ","
			// << eng_stat_data.axis[eENG_STAT_Accel_Y] << ","
			// << eng_stat_data.axis[eENG_STAT_Accel_Z] << ","
			// << "G:" << eng_stat_data.axis[eENG_STAT_Gyro_X] << ","
			// << eng_stat_data.axis[eENG_STAT_Gyro_Y] << ","
			// << eng_stat_data.axis[eENG_STAT_Gyro_Z] << ","
			<< "ARMS:" << eng_stat_data.axis[eENG_STAT_Accel_RMS] << ","
			<< "GRMS:" << eng_stat_data.axis[eENG_STAT_Gyro_RMS] << ","
			<< "ARMSD:" << eng_stat_data.axis[eENG_STAT_Accel_RMS_Delta] << ","
			<< "GRMSD:" << eng_stat_data.axis[eENG_STAT_Gyro_RMS_Delta]
			<< ", Motion Score:" << eng_stat_data.axis[eENG_STAT_Motion_Score]
			<< ", ARMSD_Factor:" << eng_stat_data.axis[eENG_STAT_Accel_RMS_Delta_Factor]
			<< ", GRMSD_Factor:" << eng_stat_data.axis[eENG_STAT_Gyro_RMS_Delta_Factor]
			<< ", Motion_Score_Factor:" << eng_stat_data.axis[eENG_STAT_Motion_Score_Factor];
		return oss.str();
	}

	inline std::string get_thres_string() {
		std::ostringstream oss;
		oss << "TH - ";
		oss << "ARMS (on: " << std::fixed << std::setprecision(3) << threshold[eENG_STAT_Accel_RMS][eAPM_Decision_Moving] << " off: " << std::fixed << std::setprecision(3) << threshold[eENG_STAT_Accel_RMS][eAPM_Decision_Engine_Off] << ")";
		oss << ", GRMS (on: " << std::fixed << std::setprecision(3) << threshold[eENG_STAT_Gyro_RMS][eAPM_Decision_Moving] << " off: " << std::fixed << std::setprecision(3) << threshold[eENG_STAT_Gyro_RMS][eAPM_Decision_Engine_Off] << ")";
		oss << ", ARMSD (on: " << std::fixed << std::setprecision(3) << threshold[eENG_STAT_Accel_RMS_Delta][eAPM_Decision_Moving] << " off: " << std::fixed << std::setprecision(3) << threshold[eENG_STAT_Accel_RMS_Delta][eAPM_Decision_Engine_Off] << ")";
		oss << ", GRMSD (on: " << std::fixed << std::setprecision(3) << threshold[eENG_STAT_Gyro_RMS_Delta][eAPM_Decision_Moving] << " off: " << std::fixed << std::setprecision(3) << threshold[eENG_STAT_Gyro_RMS_Delta][eAPM_Decision_Engine_Off] << ")";
		oss << ", Motion Score (on: " << std::fixed << std::setprecision(3) << threshold[eENG_STAT_Motion_Score][eAPM_Decision_Moving] << " off: " << std::fixed << std::setprecision(3) << threshold[eENG_STAT_Motion_Score][eAPM_Decision_Engine_Off] << ")";
		oss << ", Percentage Change Factor (Idling) ( " << std::fixed << std::setprecision(3) << threshold[eENG_STAT_Accel_RMS_Delta_Factor][eAPM_Decision_Idling] << " )";
		return oss.str();
	}

    virtual  uint get_decision_count(APM_Decision decision, uint index) {
		if(false == apm_worker_utils::is_decision_valid(decision)) {
			return 0;
		}
		if(apm_worker_utils::is_index_valid(index, ENG_STAT_FULL_WINDOW_SAMPLES) == false) {
			LOG_E("A_ENG_STAT", "Invalid index requested: %u", index);
			return 0;
		}
		return data[index].decision[decision];
	}
    void set_decision_count(APM_Decision decision, uint index, uint count) {
		if(false == apm_worker_utils::is_decision_valid(decision)) {
			return;
		}
		if(apm_worker_utils::is_index_valid(index, ENG_STAT_FULL_WINDOW_SAMPLES) == false) {
			LOG_E("A_ENG_STAT", "Invalid index requested: %u", index);
			return;
		}
		data[index].decision[decision] = count;
	}
    void init_decision_counts(uint index) {
		// dummy for now
	}
    bool is_above_threshold(APM_Decision decision, uint index){
		if(false == apm_worker_utils::is_decision_valid(decision)) {
			return false;
		}

		if(decision == eAPM_Decision_Idling){
			// Increment when percent change in delta is more than a certain thresold
			return !( (data[index].axis[eENG_STAT_Accel_RMS_Delta_Factor] > threshold[eENG_STAT_Accel_RMS_Delta_Factor][decision]));
		}

		return ( (data[index].axis[eENG_STAT_Accel_RMS_Delta] > threshold[eENG_STAT_Accel_RMS_Delta][decision]) && (data[index].axis[eENG_STAT_Accel_RMS_Delta] <= MAX_IMU_ENG_STAT_THRESHOLD) );
			//   ( (data[index].axis[eENG_STAT_Gyro_RMS_Delta] > threshold[eENG_STAT_Gyro_RMS_Delta][decision]) && (data[index].axis[eENG_STAT_Gyro_RMS_Delta] <= MAX_RMS_DELTA_THRESHOLD) ) ||
			//   ( (data[index].axis[eENG_STAT_Motion_Score] > threshold[eENG_STAT_Motion_Score][decision]) && (data[index].axis[eENG_STAT_Motion_Score] <= MAX_MOTION_SCORE_THRESHOLD) ) );
	}

	inline bool is_data_outage(uint index) {
        for(int i = eENG_STAT_Accel_RMS; i < eENG_STAT_AxisMax; ++i) { // Only checking RMS data here as Accel and Gyro raw data can have negative values
            if((get_data(index).axis[i] < SysfsErrorCodes::eSYSFS_RET_ZERO)) {
                return true;
            }
        }
        return false;
    }

	inline std::string get_valid_sysfs_path() {
        return "";
    }

	inline void mark_data_invalid(uint index) {
        if(false == apm_worker_utils::is_index_valid(index, ENG_STAT_FULL_WINDOW_SAMPLES) ) {
            LOG_E(TAG_ENGINE_STATUS, "Invalid index to mark data valid: %u", index);
            return;
        }
        // mark data as in valid for the given index
        for(int i = 0; i < eENG_STAT_AxisMax; ++i) {
            data[index].axis[i] = static_cast<float>(SysfsErrorCodes::eSYSFS_INVALID_DATA);
        }
    }

	void validate_thresholds() {
		// Only validating accel rms delta threshold for now, as those are only used for engine status detection
		if( (threshold[eENG_STAT_Accel_RMS_Delta][eAPM_Decision_Moving] < MIN_IMU_ENG_STAT_THRESHOLD) || 
			(threshold[eENG_STAT_Accel_RMS_Delta][eAPM_Decision_Moving] > MAX_IMU_ENG_STAT_THRESHOLD) ||
			(threshold[eENG_STAT_Accel_RMS_Delta][eAPM_Decision_Engine_Off] < MIN_IMU_ENG_STAT_THRESHOLD) ||
			(threshold[eENG_STAT_Accel_RMS_Delta][eAPM_Decision_Engine_Off] > MAX_IMU_ENG_STAT_THRESHOLD) ) {
			LOG_W(TAG_ENGINE_STATUS, "Engine Stat Accel RMS Threshold for OFF/ON is out of expected range, setting default. ON threshold: %f, OFF threshold: %f", threshold[eENG_STAT_Accel_RMS_Delta][eAPM_Decision_Moving], threshold[eENG_STAT_Accel_RMS_Delta][eAPM_Decision_Engine_Off]);
			threshold[eENG_STAT_Accel_RMS_Delta][eAPM_Decision_Moving] = DEF_IMU_ACCEL_RMS_ON_THRESHOLD;
			threshold[eENG_STAT_Accel_RMS_Delta][eAPM_Decision_Engine_Off] = DEF_IMU_ACCEL_RMS_OFF_THRESHOLD;
		}
	}

    bool is_stationary(uint index) { return true; }
    void update_calibration_data(uint index) {return ;}
    bool is_idling(uint index) {return false; }

    private:
    EngStatData data[ENG_STAT_FULL_WINDOW_SAMPLES]; // Initialize all data as invalid
    float threshold[eENG_STAT_AxisMax][APM_Decision::eAPM_Decision_Max] = {0.0};
};

class ENG_STAT_worker : public APM_worker {
    public:
		float prev_accel_rms = -1, prev_gyro_rms = -1; // To be updated by IMU worker for each sample and used for engine status decision
		float alpha = DEF_IMU_MOTION_SCORE_ALPHA;
		int sample_rate = DEF_SAMPLE_RATE;
		bool is_first_sample = true; // Used to do initialization steps in detect engine status method.
		float prev_accel_rms_delta = -1, prev_gyro_rms_delta = -1, prev_motion_score = -1;
		ENG_STAT_worker(int dec_window[eDecisionWindow_Max], eng_stat_thresholds eng_stat_thr, bool disable_sensor) {
            setWorkerBinDataObj(new EngStatBinData(eng_stat_thr));
			// Decision Windows
			set_decison_interval(eDecisionWindow_Short, dec_window[eDecisionWindow_Short] * sample_rate);
			set_decison_interval(eDecisionWindow_Long, dec_window[eDecisionWindow_Long] * sample_rate);
			set_decison_interval(eDecisionWindow_Valid, dec_window[eDecisionWindow_Short] * sample_rate);
			if(dec_window[eDecisionWindow_Outage] > ENG_STAT_FULL_WINDOW){
				dec_window[eDecisionWindow_Outage] = ENG_STAT_DATA_OUTAGE_THRESHOLD; // If outage window is not configured, set it to default outage threshold
			}
			set_decison_interval(eDecisionWindow_Outage, dec_window[eDecisionWindow_Outage]);
			set_decison_interval(eDecisionWindow_Sleep, ENG_STAT_READ_INTERVAL_SEC);
			if(dec_window[eDecisionWindow_Median] > ENG_STAT_FULL_WINDOW){
				dec_window[eDecisionWindow_Median] = ENG_STAT_MEDIAN_DEC_WINDOW; // If median window is not configured, set it to default median window
			}
			set_decison_interval(eDecisionWindow_Median, dec_window[eDecisionWindow_Median] * sample_rate);
			set_decison_interval(eDecisionWindow_Continuous, (dec_window[eDecisionWindow_Short])); // Do not multiply with sample rate, check continuous only for number of samples (this is not in seconds)
			set_decison_interval(eDecisionWindow_Cache, ENG_STAT_FULL_WINDOW_SAMPLES);
			// Set hyper parameters for motion score calculation
			alpha = eng_stat_thr.imu_motion_score_alpha;

			sm_sleep = false; // Thread should not sleep as engine status decision is based on IMU data which is coming at 20 Hz and we need to process each sample for accurate engine status decision

			sensor_decision_disabled = disable_sensor;// In this scenario, worker will enabled but decision will be send based on this flag
		}

        poll_func_t poll_func ;
        intr_func_t intr_func ;
        filter_func_t filter_func ;
        read_status_t read_status ;

        void insert_bin_data();

        int get_signal_mask() {
            return (1 << IMU_MASK_POS);
        }

        const char* get_TAG() {
            return TAG_ENGINE_STATUS;
        }

        int get_ign_src_type(){
            return IgnitonSource::eInertial;
        }

        std::string get_src_status_sysfs_path() {
            return "";
        }

        std::string get_attr_user() {
            return USER_ENG_STAT;
        }

		void insert_bin_data(apm_imu_data_t &data);

		bool send_signal_status(motion_status_t status, bool boot = false){
			return true;
		} // Dummy for ENG_STAT_WORKER

		void check_data_validity();

		bool update_data_outage_count(){
			return false;
		}

		// bool handle_data_outage();
};

#endif // __APM_ENGINE_STATUS_H__
