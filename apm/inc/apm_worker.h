#ifndef __APM_WORKER_H_
#define __APM_WORKER_H_

#include "apm.h"
#include <sys/xattr.h>

namespace apm_worker_utils {

    inline bool is_decision_valid(APM_Decision decision) {

        if((decision >= APM_Decision::eAPM_Decision_Max) || (decision < APM_Decision::eAPM_Decision_Engine_Off)) {
            //LOG_E(TAG, "Invalid decision value");
            return false;
        }
        return true;
    }

    inline bool is_index_valid(uint index, uint window_size ) {

        if(index >= window_size ) {
            return false;
        }
        return true;
    }

    inline bool is_update_reader_count_supported() {

        NDDeviceTypeT d_type = ND_DeviceFactory::getBuildDeviceType();
        switch(d_type) {
            case NDDeviceTypeT::krait:
            case NDDeviceTypeT::krait2:
                return true;
            default:
				break;
        }

        return false;
    }
};

constexpr uint BUF_SIZE = 16;
const string print_debug_log_file = DEV_SHM + "apm_debug_log.txt";
constexpr uint DEFAULT_READ_INTERVAL = 1;

namespace apm_attr_util {

    inline std::string get_attr_status(std::string file_path, const std::string type, std::string def) {

        char buffer[BUF_SIZE] = "false";
        ssize_t ret = getxattr(file_path.c_str(), type.c_str(), buffer, sizeof(buffer));
        if(ret <= 0) {
            LOG_E("APM_WORKER", "Failed to read xattr %s", type.c_str());
            return def;
        }

        return std::string(buffer, static_cast<size_t>(ret));
    }

    inline bool set_attr_status(std::string file_path, const std::string type, const char* status) {

        if(status == nullptr) {
            LOG_E("APM_WORKER", "Status pointer is null for %s", type.c_str());
            return false;
        }

        if(false == file_is_present(file_path)) {
            file_touch(file_path);
        }

        size_t status_len = strlen(status);
        ssize_t ret = setxattr(file_path.c_str(), type.c_str(), status, status_len, 0);

        if(ret < 0) {
            LOG_E("APM_WORKER", "Failed to set xattr %s", type.c_str());
            return false;
        }

        return true;
    }

	inline attr_file_status_t create_attr_file(const std::string& full_path) {

		// fetch folder path from full path
		std::string dir_path = full_path.substr(0, full_path.find_last_of("/"));

		// Create directory hierarchy if not present
		if (!create_directories_recursive(dir_path)) {
			return eATTR_FILE_INVALID;
		}

		if(false == file_is_present(full_path)) {
			if (!file_touch(full_path)) {
				LOG_E("APM_WORKER", "Failed to create attribute file: %s", full_path.c_str());
				return eATTR_FAIL_TO_CREATE;
			}
			return eATTR_FILE_CREATED;
		}
		return eATTR_FILE_PRESENT;
	}
};

namespace apm_wom_utils {
	static std::vector<std::vector<std::vector<int>>> default_wom_thres_map = { //[veh_class][device_type][axis]
		//class 0 - Unknown (defaults to Class 2)
		{
			{DEF_WOM_THRESHOLD, DEF_WOM_THRESHOLD, DEF_WOM_THRESHOLD},								// b1
			{DEF_WOM_THRESHOLD, DEF_WOM_THRESHOLD, DEF_WOM_THRESHOLD},								// b2
			{D450_MOVING_CLASS2_WOM_THRES, D450_DOOR_CLASS2_WOM_THRES, D450_FLOOR_CLASS2_WOM_THRES},// b3
			{D2XX_DOOR_CLASS2_WOM_THRES, D2XX_MOVING_CLASS2_WOM_THRES, D2XX_FLOOR_CLASS2_WOM_THRES},// k1
			{D2XX_DOOR_CLASS2_WOM_THRES, D2XX_FLOOR_CLASS2_WOM_THRES, D2XX_MOVING_CLASS2_WOM_THRES}	// k2
		},
		//class 1
		{
			{DEF_WOM_THRESHOLD, DEF_WOM_THRESHOLD, DEF_WOM_THRESHOLD},								// b1
			{DEF_WOM_THRESHOLD, DEF_WOM_THRESHOLD, DEF_WOM_THRESHOLD},								// b2
			{D450_MOVING_CLASS1_WOM_THRES, D450_DOOR_CLASS1_WOM_THRES, D450_FLOOR_CLASS1_WOM_THRES},// b3
			{D2XX_DOOR_CLASS1_WOM_THRES, D2XX_MOVING_CLASS1_WOM_THRES, D2XX_FLOOR_CLASS1_WOM_THRES},// k1
			{D2XX_DOOR_CLASS1_WOM_THRES, D2XX_FLOOR_CLASS1_WOM_THRES, D2XX_MOVING_CLASS1_WOM_THRES}	// k2
		},
		//class 2
		{
			{DEF_WOM_THRESHOLD, DEF_WOM_THRESHOLD, DEF_WOM_THRESHOLD},								// b1
			{DEF_WOM_THRESHOLD, DEF_WOM_THRESHOLD, DEF_WOM_THRESHOLD},								// b2
			{D450_MOVING_CLASS2_WOM_THRES, D450_DOOR_CLASS2_WOM_THRES, D450_FLOOR_CLASS2_WOM_THRES},// b3
			{D2XX_DOOR_CLASS2_WOM_THRES, D2XX_MOVING_CLASS2_WOM_THRES, D2XX_FLOOR_CLASS2_WOM_THRES},// k1
			{D2XX_DOOR_CLASS2_WOM_THRES, D2XX_FLOOR_CLASS2_WOM_THRES, D2XX_MOVING_CLASS2_WOM_THRES}	// k2
		},
		//class 3
		{
			{DEF_WOM_THRESHOLD, DEF_WOM_THRESHOLD, DEF_WOM_THRESHOLD},								// b1
			{DEF_WOM_THRESHOLD, DEF_WOM_THRESHOLD, DEF_WOM_THRESHOLD},								// b2
			{D450_MOVING_CLASS2_WOM_THRES, D450_DOOR_CLASS2_WOM_THRES, D450_FLOOR_CLASS2_WOM_THRES},// b3
			{D2XX_DOOR_CLASS2_WOM_THRES, D2XX_MOVING_CLASS2_WOM_THRES, D2XX_FLOOR_CLASS2_WOM_THRES},// k1
			{D2XX_DOOR_CLASS2_WOM_THRES, D2XX_FLOOR_CLASS2_WOM_THRES, D2XX_MOVING_CLASS2_WOM_THRES}	// k2
		},
		//class 4
		{
			{DEF_WOM_THRESHOLD, DEF_WOM_THRESHOLD, DEF_WOM_THRESHOLD},								// b1
			{DEF_WOM_THRESHOLD, DEF_WOM_THRESHOLD, DEF_WOM_THRESHOLD},								// b2
			{D450_MOVING_CLASS2_WOM_THRES, D450_DOOR_CLASS2_WOM_THRES, D450_FLOOR_CLASS2_WOM_THRES},// b3
			{D2XX_DOOR_CLASS2_WOM_THRES, D2XX_MOVING_CLASS2_WOM_THRES, D2XX_FLOOR_CLASS2_WOM_THRES},// k1
			{D2XX_DOOR_CLASS2_WOM_THRES, D2XX_FLOOR_CLASS2_WOM_THRES, D2XX_MOVING_CLASS2_WOM_THRES}	// k2
		},
		//class 5
		{
			{DEF_WOM_THRESHOLD, DEF_WOM_THRESHOLD, DEF_WOM_THRESHOLD},								// b1
			{DEF_WOM_THRESHOLD, DEF_WOM_THRESHOLD, DEF_WOM_THRESHOLD},								// b2
			{D450_MOVING_CLASS2_WOM_THRES, D450_DOOR_CLASS2_WOM_THRES, D450_FLOOR_CLASS2_WOM_THRES},// b3
			{D2XX_DOOR_CLASS2_WOM_THRES, D2XX_MOVING_CLASS2_WOM_THRES, D2XX_FLOOR_CLASS2_WOM_THRES},// k1
			{D2XX_DOOR_CLASS2_WOM_THRES, D2XX_FLOOR_CLASS2_WOM_THRES, D2XX_MOVING_CLASS2_WOM_THRES}	// k2
		},
		//class 6
		{
			{DEF_WOM_THRESHOLD, DEF_WOM_THRESHOLD, DEF_WOM_THRESHOLD},								// b1
			{DEF_WOM_THRESHOLD, DEF_WOM_THRESHOLD, DEF_WOM_THRESHOLD},								// b2
			{D450_MOVING_CLASS2_WOM_THRES, D450_DOOR_CLASS2_WOM_THRES, D450_FLOOR_CLASS2_WOM_THRES},// b3
			{D2XX_DOOR_CLASS2_WOM_THRES, D2XX_MOVING_CLASS2_WOM_THRES, D2XX_FLOOR_CLASS2_WOM_THRES},// k1
			{D2XX_DOOR_CLASS2_WOM_THRES, D2XX_FLOOR_CLASS2_WOM_THRES, D2XX_MOVING_CLASS2_WOM_THRES}	// k2
		},
		//class 7
		{
			{DEF_WOM_THRESHOLD, DEF_WOM_THRESHOLD, DEF_WOM_THRESHOLD},								// b1
			{DEF_WOM_THRESHOLD, DEF_WOM_THRESHOLD, DEF_WOM_THRESHOLD},								// b2
			{D450_MOVING_CLASS2_WOM_THRES, D450_DOOR_CLASS2_WOM_THRES, D450_FLOOR_CLASS2_WOM_THRES},// b3
			{D2XX_DOOR_CLASS2_WOM_THRES, D2XX_MOVING_CLASS2_WOM_THRES, D2XX_FLOOR_CLASS2_WOM_THRES},// k1
			{D2XX_DOOR_CLASS2_WOM_THRES, D2XX_FLOOR_CLASS2_WOM_THRES, D2XX_MOVING_CLASS2_WOM_THRES}	// k2
		},
		//class 8
		{
			{DEF_WOM_THRESHOLD, DEF_WOM_THRESHOLD, DEF_WOM_THRESHOLD},								// b1
			{DEF_WOM_THRESHOLD, DEF_WOM_THRESHOLD, DEF_WOM_THRESHOLD},								// b2
			{D450_MOVING_CLASS2_WOM_THRES, D450_DOOR_CLASS2_WOM_THRES, D450_FLOOR_CLASS2_WOM_THRES},// b3
			{D2XX_DOOR_CLASS2_WOM_THRES, D2XX_MOVING_CLASS2_WOM_THRES, D2XX_FLOOR_CLASS2_WOM_THRES},// k1
			{D2XX_DOOR_CLASS2_WOM_THRES, D2XX_FLOOR_CLASS2_WOM_THRES, D2XX_MOVING_CLASS2_WOM_THRES}	// k2
		}
	};
};

class WorkerData {

	private:
		uint index = 0;
		uint data_outage_count = 0;
		uint decision_interval[eDecisionWindow_Max] = {0}; // Different for all sensors
		std::atomic<motion_status_t> g_vehicle_state{STATIONARY};
		bool fusion_activated = false;
		//std::mutex data_lock;

	public:

		uint getIndex() {
            return index;
        }

		uint getDataOutageCnt() {
            return data_outage_count;
        }

		uint getDecisionInterval(DecisionWindow dWind ) {
			if( (dWind >= eDecisionWindow_Short) && (dWind < eDecisionWindow_Max) ) { return  decision_interval[dWind]; }
			return eDecisionWindow_Short;
		}

		motion_status_t getMotionState() {
            return g_vehicle_state.load();
        }

		void incrIndex() {
			// Wrap index around using the Long window size (buffer size)
			if(decision_interval[eDecisionWindow_Cache] > 1) {
				index = (index + 1) % decision_interval[eDecisionWindow_Cache];
			}else{
				index = 0; // If window size is <=1 default is index zero.
			}
		}

		void incrDataOutageCnt() {
            ++data_outage_count;
        }

		void resetDataOutageCnt() {
            data_outage_count = 0;
        }

		void setDecisonInterval(DecisionWindow dWind, uint interval) {
			if( (dWind >= eDecisionWindow_Short) && (dWind < eDecisionWindow_Max) ) { decision_interval[dWind] = interval; }
		}

		void setMotionState(motion_status_t motion_status) {
            g_vehicle_state.store(motion_status);
        }

		bool getFusionActivated() {
			return fusion_activated;
		}

		void setFusionActivated(bool status) {
			fusion_activated = status;
		}
};

class WorkerBinData {

	public:

		virtual void validate_thresholds() {return ;}
        virtual  float get_threshold(APM_Decision decision) = 0;
		virtual  uint get_decision_count(APM_Decision decision, uint index) = 0;
		virtual  void set_decision_count(APM_Decision decision, uint index, uint count)  = 0;
		virtual  void init_decision_counts(uint index)  = 0;
		virtual  bool is_above_threshold(APM_Decision decision, uint index)  = 0;
		virtual  bool is_data_outage( uint index)  = 0;
		virtual  void set_bin_data(uint index, void* data)  = 0;
		virtual  string get_valid_sysfs_path() = 0;
		virtual  string get_data_string(uint index) = 0;
		virtual  std::string get_thres_string() = 0;
		virtual  void mark_data_invalid(uint index) = 0;
		virtual  bool is_stationary(uint index) { return true; }
		virtual  void update_calibration_data(uint index) {return ;}
		virtual  bool is_idling(uint index) = 0;
		virtual  bool create_battery_status_file() { return false; } // To be overridden by POWER_VOLT_worker
		virtual void set_24V_battery_status(bool status = true) {} // To be overridden by POWER_VOLT_worker
		virtual void update_bad_battery_cnt(uint index) {} // To be overridden by POWER_VOLT_worker
};

class APM;

class APM_worker {
	private:
		WorkerData mWorkerData;
		WorkerBinData *mBinData;

	public:

		APM_worker() { }
		virtual ~APM_worker() {}
		bool enabled = true;
		bool use_legacy = true;
		bool is_bin_data_valid = false;
		uint64_t logging_interval_sec = LOGGING_INTERVAL_SEC;
		uint read_interval_sec = DEFAULT_READ_INTERVAL;
		bool is_event_driven = false;
		bool is_data_outage_event = false;
		bool fusion_enabled = false;
		bool init_state_read = false;

		std::atomic<bool> sm_sleep{true}; // To control if thread should sleep or not for trigger/callback based workers, will break out of the loop instead of sleeping is false

		// From overrides if any sensor threshold is set to SENSOR_DECISION_DISABLE_THRESHOLD make flag to true
		bool sensor_decision_disabled = false;

		virtual poll_func_t poll_func = 0;
		virtual intr_func_t intr_func = 0;

		virtual filter_func_t filter_func = 0;
		virtual read_status_t read_status = 0;

		WorkerBinData* getWorkerBinDataObj() {
			return mBinData;
		}

		void setWorkerBinDataObj(WorkerBinData* binData) {
			mBinData = binData;
			if(mBinData != NULL) {
			       is_bin_data_valid = true;	
			}
		}

		WorkerData& getWorkerDataObj() {
			return mWorkerData;
		}

		const char* TAG = "APM_WORKER";

		bool is_worker_enabled() {
			return enabled;
		}

		virtual int get_signal_mask() = 0;
		virtual const char* get_TAG() = 0;
		virtual int get_ign_src_type() = 0;

		void detect_engine_status(bool init_state = true);
		virtual void insert_bin_data() = 0; // Different for all sensors
		virtual void check_data_validity();
		virtual bool update_data_outage_count();// If returns true, it means data outage count incremented, false means data outage count is reset to zero
		void filter_func_new ();
		void log_data(bool force = false);

		uint get_decision_count(APM_Decision dec, uint index = GET_CURR_DATA);
		void set_decision_count(APM_Decision dec, uint count, bool is_event = false, bool is_prev = false);
		virtual bool is_above_threshold(APM_Decision decision);
		bool is_continuous_motion(APM_Decision decision, uint index, uint duration);
		motion_status_t update_decision(APM_Decision decision);
		uint get_index();
		uint get_prev_index();
		uint get_decision_index();
		uint get_prev_decision_index();
		void incr_index();
		bool is_data_outage();
		uint get_data_outage_count();
		string get_data_string();
		uint get_read_interval();
		uint get_decision_interval(DecisionWindow dWind = eDecisionWindow_Short);
		void set_decison_interval(DecisionWindow dWind, uint interval);

		motion_status_t getMotionState();
		virtual void setMotionState(motion_status_t motion_status);
		virtual void initMotionState();
		motion_status_t getInitialState();
		virtual bool send_signal_status(motion_status_t status, bool boot = false);

		float get_threshold(APM_Decision decision);
		void set_threshold(APM_Decision decision, float threshold);
		std::string get_threshold_string();

		//Future Update w.r.t Adaptive Sensor Fusion.
		void update_calibration_data();
		bool is_idling(uint index);

		motion_status_t read_ign_state_from_sysfs();
		motion_status_t read_ign_state_from_attr();

		virtual std::string get_src_status_sysfs_path() { return ""; }
		virtual std::string get_attr_user() { return ""; }

		void update_attr_status(motion_status_t status, bool time_update = false, bool force = false);

		// Override if you want do something specific on data outage
		virtual bool handle_data_outage();

		bool is_fusion_enabled() { return fusion_enabled; }
		void update_fusion_count(APM_Decision decision, bool reset = false);
		bool get_fusion_activated();
		void set_fusion_activated(bool status);

       // thread for testing purpose for event driven workers
       // If passed file is present, it will  created thread which will monitor events and accordingly call callbacks
       std::thread test_event_driven_thread;
       void test_event_driven_worker_fn();
       virtual std::string get_test_file_path() { return ""; }
	   virtual void test_callback_fn(int status)  { return;  }
	   virtual motion_status_t getEngineState() {return getMotionState();}

};

#endif
