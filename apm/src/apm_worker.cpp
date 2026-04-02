
#include "apm_worker.h"

#define AUX_MAGIC_NUM_ONE   25 // data
#define AUX_MAGIC_NUM_TWO   26 // State Machine
#define AUX_MAGIC_NUM_THREE 27 // Data Outage Clear

// TODO:: We Should Optimize Read with larger batch size.
constexpr int64_t DEF_UPTIME_THRESHOLD_SEC = 60;

void APM_worker::detect_engine_status(bool init_state) {

    if(init_state == true){
    // Only do the initialization related task if flag is true, this will help trigger/callback based workers
    if(false == is_bin_data_valid){
	    LOG_E(get_TAG(), "%s ERROR!!! BinDataObj Allocation Failed. Falling back to legacy", __func__);
	    return;
    }

    read_interval_sec = get_read_interval();

    // Safety check: if interval not initialized, use default
    read_interval_sec = ( (read_interval_sec < DEFAULT_READ_INTERVAL) ? DEFAULT_READ_INTERVAL : read_interval_sec ); 

    // To print logs for debugging, check file /dev/shm/apm_debug_log.txt
    if((true == file_is_present(print_debug_log_file))) {
        if((read_from_dev_shm_file(print_debug_log_file, logging_interval_sec) == false)){
            logging_interval_sec = LOGGING_INTERVAL_SEC;
        }
    }else{
        logging_interval_sec = LOGGING_INTERVAL_SEC;
    }

    LOG_C(get_TAG(), "%s Started with Read Interval :%d , Logging Interval :%llu secs", __func__, read_interval_sec, logging_interval_sec);

    //Sending boot apm health metrics and critical info
    send_signal_status(MOTION_STATUS_MAX, true);

    //Logging Thread Start Up Defaults.
    log_data(true);

    }


    while (true) {

	//1) Fetch and Populate Sensor Data
        insert_bin_data();    

	//2) Check and Update the Validity of Fetched Data
        check_data_validity();

	//3) Check and Update Data Outage 
        bool data_outage_incemented = update_data_outage_count();

	//4) Update Motion State based 0n Fetched Data
        filter_func_new();

	//5) Log the Relevant Data for Monitoring and Debug
        log_data();

    //6) Update status ext. attribute
    update_attr_status(getMotionState(), false);

    // Update cailbiration
    update_calibration_data();

	//6) Wait for next Batch of Sensor Data 
        if(true == sm_sleep.load()){ // Only sleep if not event based
            sleep(read_interval_sec);
        }else{
            break; // For event/trigger based workers break the loop and wait for trigger to start next iteration
        }
    }
}

uint APM_worker::get_decision_count(APM_Decision dec, uint index) {
    if( false == apm_worker_utils::is_decision_valid(dec)) {
        return 0;
    }

    if(GET_CURR_DATA == index) {
        return mBinData->get_decision_count(dec, get_decision_index());
    }

    return mBinData->get_decision_count(dec, index);
}

void APM_worker::set_decision_count(APM_Decision dec, uint count, bool is_event, bool is_prev) {
    if(false == apm_worker_utils::is_decision_valid(dec)) {
        return;
    }

    if(APM_Decision::eAPM_Decision_Idling == dec) {
        if(is_event != is_event_driven) {
            if(true == is_event_driven) {
                // persist the previous count if event type does not match
                mBinData->set_decision_count(dec, get_decision_index(), get_decision_count(dec, get_prev_decision_index()));
            }
            return;

        }
    }

    uint decision_index = (true == is_prev) ? get_prev_decision_index() : get_decision_index();

    mBinData->set_decision_count(dec, decision_index, count);
}

bool APM_worker::send_signal_status(motion_status_t status, bool boot) {

    if(true == boot){
        //sending threshold info as critical event and healthstats
        apm_thresholds_msg_t apm_thresholds = {0};
        apm_thresholds.ts = get_system_time();
        apm_thresholds.uptime = get_system_monotonic_time();
        if(get_file_xattr(ND_UDID_SID, FileMetadataKey::udid, &apm_thresholds.udid, sizeof(apm_thresholds.udid))!= XATTR_OK) {
            LOG_E(TAG, "Read fail. udid is: %lld", apm_thresholds.udid);
        }
        if(get_file_xattr(ND_UDID_SID, FileMetadataKey::sid, &apm_thresholds.sid, sizeof(apm_thresholds.sid))!= XATTR_OK) {
            LOG_E(TAG, "Read fail. sid is: %lld", apm_thresholds.sid);
        }
        apm_thresholds.src_type = get_ign_src_type();
        apm_thresholds.on_wd = get_decision_interval(eDecisionWindow_Short);
        apm_thresholds.off_wd = get_decision_interval(eDecisionWindow_Long);
        apm_thresholds.outage_wd = get_decision_interval(eDecisionWindow_Outage);
        apm_thresholds.valid_dbn_wd = get_decision_interval(eDecisionWindow_Valid);
        apm_thresholds.sleep_wd = get_decision_interval(eDecisionWindow_Sleep);
        apm_thresholds.hysterisis_wd = get_decision_interval(eDecisionWindow_Median);
        apm_thresholds.continuous_wd = get_decision_interval(eDecisionWindow_Continuous);
        apm_thresholds.data_cache_wd = get_decision_interval(eDecisionWindow_Cache);
        std::string thres_string = get_threshold_string();
        nd_strncpy(apm_thresholds.thres_str, thres_string.c_str(), thres_string.length()+1);
        int aux_code = (1 << get_signal_mask());
        LOG_I(get_TAG(), "%s", thres_string.c_str());
        (NDService::get_service_obj(TAG))->send_err_msg(SM_E_APM_EVENT_STATUS, aux_code, thres_string);
        LOG_I(get_TAG(), "Sending APM Thresholds msg to DIAGNOSTIC");
        send_msg ((generic_msg_t *)&apm_thresholds, RES_APM_THRESHOLDS, sizeof(apm_thresholds), APM::Instance()->apm_q_name, diagnostic_q_name, 0);
    }

    if(getMotionState() != status) {

        LOG_D(get_TAG(), "Motion state changed from %d to %d", status, getMotionState());
        update_attr_status(getMotionState(), true, true);


        if(false == use_legacy) {
            (getMotionState() == STATIONARY) ? reset_bit(get_signal_mask()) : set_bit(get_signal_mask());
        }

        DataValidCount valid_cnt = {0,0};
        string valid_sysfs_path = getWorkerBinDataObj()->get_valid_sysfs_path();
        if(valid_sysfs_path.empty() != true) {
            get_data_validity_count(valid_sysfs_path, valid_cnt);
        }

        apm_metrics_msg_t apm_metrics = {0};
        apm_metrics.ts = get_system_time();
        apm_metrics.uptime = get_system_monotonic_time();
        if(get_file_xattr(ND_UDID_SID, FileMetadataKey::udid, &apm_metrics.udid, sizeof(apm_metrics.udid))!= XATTR_OK){
            LOG_E(TAG, "Read fail. udid is: %lld", apm_metrics.udid);
        }
        if(get_file_xattr(ND_UDID_SID, FileMetadataKey::sid, &apm_metrics.sid, sizeof(apm_metrics.sid))!= XATTR_OK){
            LOG_E(TAG, "Read fail. sid is: %lld", apm_metrics.sid);
        }
        apm_metrics.src_type = get_ign_src_type();
        apm_metrics.is_event = is_event_driven;
        std::string data_string = get_data_string();
        nd_strncpy(apm_metrics.data_str, data_string.c_str(), MAX_APM_DATA_LEN);
        apm_metrics.idx = get_index();
        apm_metrics.decision_idx = get_decision_index();
        apm_metrics.off_cnt = get_decision_count(APM_Decision::eAPM_Decision_Engine_Off);
        apm_metrics.on_cnt = get_decision_count(APM_Decision::eAPM_Decision_Moving);
        apm_metrics.idl_dbn_cnt = get_decision_count(APM_Decision::eAPM_Decision_Idling);
        apm_metrics.prev_off_cnt = get_decision_count(APM_Decision::eAPM_Decision_Engine_Off, get_prev_decision_index());
        apm_metrics.prev_on_cnt = get_decision_count(APM_Decision::eAPM_Decision_Moving, get_prev_decision_index());
        apm_metrics.prev_idl_dbn_cnt = get_decision_count(APM_Decision::eAPM_Decision_Idling, get_prev_decision_index());
        apm_metrics.reader_cnt = valid_cnt.reader_count;
        apm_metrics.generator_cnt = valid_cnt.generator_count;
        apm_metrics.outage_cnt = get_data_outage_count();

        string msg_string = get_TAG() + std::string(": ") + (getMotionState() == motion_status_t::MOVING ? "ON" : "OFF")
                + " I(" + to_string(apm_metrics.decision_idx) + ")"
                + " Cnt(" + to_string(apm_metrics.off_cnt)
                    + " : " + to_string(apm_metrics.on_cnt)
                    + " : " + to_string(apm_metrics.idl_dbn_cnt)
                    + " PC: " +  to_string(apm_metrics.prev_off_cnt)
                    + " : " + to_string(apm_metrics.prev_on_cnt)
                    + " : " + to_string(apm_metrics.prev_idl_dbn_cnt)
                    + " OC: " + to_string(apm_metrics.outage_cnt)
                    + " RC: " + to_string(apm_metrics.reader_cnt)
                    + " GC: " + to_string(apm_metrics.generator_cnt)
                    + " UID: " + to_string(apm_metrics.udid)
                    + " SID: " + to_string(apm_metrics.sid) + ")";

        if(true == is_fusion_enabled()) {
            // If fusion is enabled, then append fusion counts also
            msg_string += " F: " + std::string( (get_fusion_activated() ? "true" : "false") ) + "OFF: " + to_string(get_decision_count(APM_Decision::eAPM_Decision_Fusion_Engine_Off))
                          + " ON: " + to_string( get_decision_count(APM_Decision::eAPM_Decision_Fusion_Moving) )
                          + " IDLE: " + to_string( get_decision_count(APM_Decision::eAPM_Decision_Fusion_Idling) );
        }

        int aux_code = ((1 << AUX_MAGIC_NUM_ONE) | (1 << get_signal_mask()));

        (NDService::get_service_obj(TAG))->send_err_msg(SM_E_APM_EVENT_STATUS, aux_code, msg_string); // AUX Code has to be appropriately given to capture... ?

        msg_string = get_TAG() + std::string(": ") + "D: (" + std::string(apm_metrics.data_str) + ") ";
        aux_code = ((1 << AUX_MAGIC_NUM_TWO) | (1 << get_signal_mask()));

        (NDService::get_service_obj(TAG))->send_err_msg(SM_E_APM_EVENT_STATUS, aux_code, msg_string);

        LOG_I(get_TAG(), "Sending APM Health Metrics msg to DIAGNOSTIC");
        send_msg ((generic_msg_t *)&apm_metrics, RES_APM_METRICS, sizeof(apm_metrics), APM::Instance()->apm_q_name, diagnostic_q_name, 0);

        log_data(true); // Logging in case of state change
    }

    return true;
}

bool APM_worker::is_above_threshold(APM_Decision decision) {

    if(false == apm_worker_utils::is_decision_valid(decision)) {
        return false;
    }

    return mBinData->is_above_threshold(decision, get_decision_index());
}

bool APM_worker::is_continuous_motion(APM_Decision decision, uint index, uint duration) {
    if(false == apm_worker_utils::is_decision_valid(decision)) {
        return false;
    }

    // As current sample is already present above threshold, we need to check only (duration - 1) samples

    uint oldest_short_window_index = (index - (duration - 1) + get_decision_interval(eDecisionWindow_Cache)) % get_decision_interval(eDecisionWindow_Cache);

    LOG_D(get_TAG(),"Decision: %u ,Oldest Short Window Index: %u, Index: %u, get_decision_count(decision, index) %u, get_decision_count(decision, oldest_short_window_index) %u, duration %u",
                    decision,
                    oldest_short_window_index,
                    index,
                    get_decision_count(decision, index),
                    get_decision_count(decision, oldest_short_window_index),
                    duration);

    if((get_decision_count(decision, index) - get_decision_count(decision, oldest_short_window_index)) >= (duration - 1)){
	    // To Handle Cases Where the Vehicle is slow moving ( < 2 to 5 mph ) at a traffic light / slow moving traffic scenarios.
	    // Check difference in distance between the oldest valid (lat/long)data available and latest valid(lat/long) data in our buffer 
	    //against two or three car lengths distance covered/moved to take MOVING/STATIONARY decision

	    // Check Very Slow Motion/Complete Stationary Signatures in case of ENGINE OFF Decision( MOVING to STATIONARY ) motion
	    if(eAPM_Decision_Engine_Off == decision) {
		    //
		    (getWorkerBinDataObj()->is_stationary(index));
	    }
    	return true;
    }
    return false;
}

void APM_worker::check_data_validity() {

    std::string valid_sysfs_path = getWorkerBinDataObj()->get_valid_sysfs_path();

    DataValidCount  valid_cnt;
    get_data_validity_count(valid_sysfs_path, valid_cnt);


    const uint64_t diff_cnt = (valid_cnt.reader_count > valid_cnt.generator_count)
                        ? (valid_cnt.reader_count - valid_cnt.generator_count)
                        : (valid_cnt.generator_count - valid_cnt.reader_count);

    // Event driven validate based on the diff count.
    // Motion driven validate based on the generator count.
    if( ((true == is_event_driven) &&
        (true == getWorkerBinDataObj()->is_data_outage(get_decision_index())) &&
        (diff_cnt > get_decision_interval(eDecisionWindow_Valid))) ||
        (0 == valid_cnt.generator_count)) {

        //Invalidate Data.
        mWorkerData.incrDataOutageCnt();
        getWorkerBinDataObj()->mark_data_invalid(get_decision_index());

    }
    else {

        update_data_validity(valid_sysfs_path, false, is_event_driven, get_decision_interval(eDecisionWindow_Valid));
    }
}

bool APM_worker::update_data_outage_count() {

    if(((true == is_event_driven) &&
        (true == is_continuous_motion(eAPM_Decision_Debounce, get_prev_decision_index(), get_decision_interval(eDecisionWindow_Valid)))) ||
        (true == getWorkerBinDataObj()->is_data_outage(get_decision_index()))) {
        mWorkerData.incrDataOutageCnt();
        return true;
    }
    mWorkerData.resetDataOutageCnt();
    return false;
}

void APM_worker::update_fusion_count(APM_Decision decision, bool reset) {

    if(true == reset) {
        set_decision_count(eAPM_Decision_Fusion_Moving, 0);
        set_decision_count(eAPM_Decision_Fusion_Engine_Off, 0);
        return;
    }

    if(true == is_fusion_enabled()) {

        if(decision == eAPM_Decision_Moving) {
            decision = eAPM_Decision_Fusion_Moving;
        }
        else if(decision == eAPM_Decision_Engine_Off) {
            decision = eAPM_Decision_Fusion_Engine_Off;
        }
        else if(decision == eAPM_Decision_Idling) {
            decision = eAPM_Decision_Fusion_Idling;
        }

        uint prev_fusion_moving_count = get_decision_count(eAPM_Decision_Fusion_Moving, get_prev_decision_index());
        uint prev_fusion_engine_off_count = get_decision_count(eAPM_Decision_Fusion_Engine_Off, get_prev_decision_index());

        if(true == is_above_threshold(decision)) {
            set_decision_count(eAPM_Decision_Fusion_Moving, prev_fusion_moving_count + 1);
            set_decision_count(eAPM_Decision_Fusion_Engine_Off, prev_fusion_engine_off_count);
        }
        else {
            set_decision_count(eAPM_Decision_Fusion_Engine_Off, prev_fusion_engine_off_count + 1);
            set_decision_count(eAPM_Decision_Fusion_Moving, prev_fusion_moving_count);
        }

        {
            uint prev_fusion_idling_count = get_decision_count(eAPM_Decision_Fusion_Idling, get_prev_decision_index());
            uint fusion_idling_count = ( false == is_above_threshold(eAPM_Decision_Idling)) ? (prev_fusion_idling_count + 1) : prev_fusion_idling_count ;
            set_decision_count(eAPM_Decision_Fusion_Idling, fusion_idling_count);
	    }
    }
}

/*
	         		        \----------------------------------/
        \\-----------------------    	 SENSOR MOTION STATUS STATE MACHINE          ---------------------//
		        	        /----------------------------------\

    CURRENT           = 	              TRANSITION LOGIC	                        =            MOTION                               =  NEXT
     STATE            =                           STATE                                 =    PREV -  STATUS - NEXT                        =  STATE
[=====================================================================================================================================================]

(A)STATIONARY--> ||---> SensorData > MovingThreshold
  /MOVING        ||	a) ON > ShortWindow & ContinuousWindow --> ON = OFF = 0     --->  STATIONARY --->  MOVING    		    	--> (B)
		 ||								          MOVING     --->  MOVING			--> (B)
	         ||
 		 ||     b) ++ON, OFF 				    	            --->  STATIONARY --->  STATIONARY(--CONFIDENCE)	--> (C)
		 ||									  MOVING     --->  MOVING ( ++CONFIDENCE)       --> (C)
		 ||---> SensorData < MovingThreshold
		 ||     c) STATIONARY  --> ++OFF, ON 			            --->  STATIONARY ---> STATIONARY( ++CONFIDENCE )    --> (C)
		 ||	d) MOVING      --> ++ON , OFF			            --->  MOVING     ---> MOVING( ++CONFIDENCE )        --> (C)
		  
(C)CONFIDENCE--> ||====> TC = ON + OFF ( StationaryThreshold < SensorData > MovingThreshold ) (Hysteresis)
		 ||---> a) TC >= MedianWindow(Confidence Window) & ContinuousWindow
	         ||        ON >  (CONFIDENCE_FACTOR * TC) --> ON = OFF = 0          --->  STATIONARY ---> MOVING(TRANSITION COMPLETED)  --> (B)
                 || 
		 ||---> b)(TC >= MedianWindow + LongWindow) & ContinuousWindow 
		 ||	   OFF>= (CONFIDENCE_FACTOR * TC) --> ON = OFF = 0          --->  MOVING ---> STATIONARY(TRANSITION  COMPLETED) --> (A)
		 ||	
		 ||--->	 TC < MedianWindow(ConfidenceWindow)
		 ||     c)Continue Improving Confidence in ConfidenceWindow         --->  STATIONARY ---> STATIONARY	    		--> (A)
										    --->  MOVING     ---> MOVING			--> (B)

(B)MOVING -->    ||---> SensorData < StationaryThreshold
                 ||     a) OFF > LongWindow    		  --> ON = OFF = 0          --->  MOVING     ---> STATIONARY    	        --> (A)				 	
      		 ||     b) ++OFF, ON, Increase Stationary Confidence	            --->  MOVING     ---> MOVING( --CONFIDENCE)         --> (B)
		 ||
		 ||---> SensorData > StationaryThreshold
		        c) Increase Moving Confidence 				    --->  MOVING     ---> MOVING( ++CONFIDENCE)		--> (A)(Ca)
										    --->  MOVING     ---> STATIONARY 			--> (A)(Cb)

		
           		Short Window = 2/10 sec.  		    STATIONARY ---> MOVING STATUS Detection Window.
			Continuous Window = (ShortWindow / 2) sec.  Sensor Data Soak and Glitch Mitigation Sliding Window inside the Short Window.
			Long Window = 5/60/200 sec.		    MOVING     ---> STATIONARY STATUS Detection Window.
			Median/Confidence = Long Window/3 sec.      Hysteresis Threshold Logic Confidence Builder Sliding Window to handle intermittent/Glitchy SensorData.
			CONFIDENCE_FACTOR = 50%(0.50)

			MovingThreshold	   ::Threshold above which, the CURRENT STATE is moved to MOVING <--- STATIONARY.

	                HysteresisThreshold::Increases Confidence of the CURRENT STATE when Sensor Data is in the Hysteresis Window (StationaryThreshold < Sensor Data > MovingThreshold)

			StationaryThreshold = (MovingThreshold - HysteresisThreshold) ::Threshold below which, the CURRENT STATE is move to STATIONARY <---- MOVING.
*/

motion_status_t APM_worker::update_decision(APM_Decision decision) {

    if(false == apm_worker_utils::is_decision_valid(decision)) {
        return STATIONARY;
    }

    LOG_D(get_TAG(), "In %s for decision: %d", __func__, decision);

    uint prev_moving_count = get_decision_count(eAPM_Decision_Moving, get_prev_decision_index());
    uint moving_count = prev_moving_count + 1;
    uint prev_engine_off_count = get_decision_count(eAPM_Decision_Engine_Off, get_prev_decision_index());
    uint engine_off_count = prev_engine_off_count + 1;
    motion_status_t cur_motion_state =  getMotionState();

    {
	    uint prev_idling_count = get_decision_count(eAPM_Decision_Idling, get_prev_decision_index());
	    uint idling_count = (false == is_above_threshold(eAPM_Decision_Idling)) ? (prev_idling_count + 1) : prev_idling_count ;
	    set_decision_count(eAPM_Decision_Idling, idling_count);
    }

    // Transition from STATIONARY/STATIONARY_TRANSITION  to MOVING
    if(decision == APM_Decision::eAPM_Decision_Moving) {

        LOG_D(get_TAG(), "Decision MOVING count: %u", moving_count);

         // Check if sensor_reading > ON_threshold
        if(true == is_above_threshold(decision)) {
            LOG_D(get_TAG(), "Data above MOVING threshold");
        
	    // Transition to MOVING if sensor_reading > ON_threshold for SHORT Window contiguously over the last CONTINUOUS Window.
            // CONTINUOUS Window acts as Soaking Window , Smoothens the Glitches in the sensor_reading.
            if( (moving_count >= get_decision_interval(eDecisionWindow_Short) )
                && (true == is_continuous_motion(decision, get_prev_decision_index(), get_decision_interval(eDecisionWindow_Continuous)))) {

                set_decision_count(decision,0); // Reset MOVING count on MOVING decision
                set_decision_count(eAPM_Decision_Engine_Off,0); // Reset ENGINE OFF count on MOVING decision
                update_fusion_count(decision, true); // Reset Fusion Counts on Decision Change
                LOG_D(get_TAG(), "Decision MOVING count reached threshold: %u", moving_count);
                return MOVING;
            }
            else {
		// ++ON_count, ENGINE_OFF_count = prev_ENGINE_OFF_count 
                set_decision_count(decision, moving_count);
                set_decision_count(eAPM_Decision_Engine_Off, prev_engine_off_count);
                update_fusion_count(decision);
                LOG_D(get_TAG(), "Decision MOVING count incremented: %u, g_vehicle_state: %d", get_decision_count(decision), mWorkerData.getMotionState());
            }
        }
        else{
		
	    // Extend Confidence Bias towards the Current State when Threshold is in b/w | OFF_thresold >  sensor_reading  < ON_threshold |
	    // ++OFF_count if STATIONARY   <->   ++ON_count if MOVING  
            if(motion_status_t::MOVING == cur_motion_state) {
                set_decision_count(decision, moving_count);
                set_decision_count(eAPM_Decision_Engine_Off, prev_engine_off_count);
                update_fusion_count(eAPM_Decision_Engine_Off);
            }else{
                set_decision_count(eAPM_Decision_Engine_Off, engine_off_count);
                set_decision_count(decision, prev_moving_count);
                update_fusion_count(decision);
            }
            LOG_D(get_TAG(), "Data below MOVING threshold, incrementing ENGINE OFF count: %u", engine_off_count);
        }

	    // 50% Confidence Threshold Logic.

        uint total_count = moving_count + engine_off_count;
        LOG_D(get_TAG(), "Total count (MOVING + ENGINE OFF): %u", total_count);

        if( total_count >= get_decision_interval(eDecisionWindow_Median) ) {

	        // Transition from STATIONARY/STATIONARY_TRANSITION to MOVING if moving count > ON_confidence over the last MEDIAN Window
            if((moving_count >= (DEF_MOVING_CONFIDENCE_PERCENTAGE * total_count))
                && (true == is_continuous_motion(decision, get_prev_decision_index(), get_decision_interval(eDecisionWindow_Continuous)))) {

                LOG_D(get_TAG(), "Total count reached threshold: %u, MOVING count >= %f * Total count", total_count, DEF_MOVING_CONFIDENCE_PERCENTAGE);
                set_decision_count(decision, 0);
                set_decision_count(eAPM_Decision_Engine_Off, 0);
                update_fusion_count(decision, true);
                return MOVING;

            } else if ( ( engine_off_count >= get_decision_interval(eDecisionWindow_Long) ) &&
			( total_count >= ( get_decision_interval(eDecisionWindow_Median) + get_decision_interval(eDecisionWindow_Long) ) ) ) {

                LOG_D(get_TAG(), "Total count reached threshold: %u, ENGINE OFF count >= %f * Total count", total_count, DEF_MOVING_CONFIDENCE_PERCENTAGE);

		// Transition from MOVING/MOVING_TRANSITION -> STATIONARY if stationary_count > OFF_confidence over the last (MEDIAN + FULL) Window 
		// AND there is no contiguous motion over the last CONTINUOUS Window . If contiguous, the decision to remain in MOVING will be taken at the end of CONTINUOUS Window. 
		if( ( engine_off_count >= (DEF_MOVING_CONFIDENCE_PERCENTAGE * total_count) )
                    && ( true  == is_continuous_motion(eAPM_Decision_Engine_Off, get_prev_decision_index(), get_decision_interval(eDecisionWindow_Continuous)  ) ) ) {

			set_decision_count(decision, 0);
			set_decision_count(eAPM_Decision_Engine_Off, 0);
            update_fusion_count(decision, true);
			return STATIONARY;
		}

		// Restart the Confidence Window
		set_decision_count(decision, 0);
		set_decision_count(eAPM_Decision_Engine_Off, 0);
        update_fusion_count(decision, true);

        } else {
		//Continue till the end of Next Window
                LOG_D(get_TAG(), "Total count reached threshold: %u, Moving count < %f * Total count", total_count, DEF_MOVING_CONFIDENCE_PERCENTAGE);
	    }
        }

        return mWorkerData.getMotionState();
    }

    // Transition from  MOVING to STATIONARY/STATIONARY_TRANSITION
    if(decision == APM_Decision::eAPM_Decision_Engine_Off) {

        LOG_D(get_TAG(), "decision count: %u, prev dec count : %u", engine_off_count, get_decision_count(decision, get_prev_decision_index()));
        LOG_D(get_TAG(), "Decision ENGINE OFF count: %u", engine_off_count);

         // Check if sensor_reading < OFF_threshold
        if(false == is_above_threshold(decision)) {

            LOG_D(get_TAG(), "Data below ENGINE OFF threshold, g_vehicle_state: %d", mWorkerData.getMotionState());
	    // Transition to STATIONARY if sensor_reading < OFF_threshold over the LONG Window and Contiguous over Last CONTINUOUS  Window.
            if( (engine_off_count >= get_decision_interval(eDecisionWindow_Long))
                && (true == is_continuous_motion(decision, get_prev_decision_index(), get_decision_interval(eDecisionWindow_Continuous)))) {
                set_decision_count(decision, 0);
                set_decision_count(APM_Decision::eAPM_Decision_Moving,0); // Reset MOVING count on ENGINE OFF decision
                update_fusion_count(decision, true); // Reset Fusion Counts on Decision Change
                LOG_I(get_TAG(), "Decision ENGINE OFF count reached threshold: %u", engine_off_count);
                return STATIONARY;
            }
            else {

		        //  ++ENGINE_OFF_count,  ON_count = prev_ON_count 
                set_decision_count(decision, engine_off_count);
                set_decision_count(APM_Decision::eAPM_Decision_Moving, prev_moving_count);
                update_fusion_count(decision);
                LOG_D(get_TAG(), "Decision ENGINE OFF count incremented: %u, g_vehicle_state: %d", get_decision_count(decision), mWorkerData.getMotionState());
                return mWorkerData.getMotionState();//MOVING
            }
        }
        else {
            LOG_D(get_TAG(), "Data above ENGINE OFF threshold, Checking for MOVING decision");
            
            //Invoke ENGINE_OFF_confidence logic based on contiguous MOVING State Detection over (MEDIAN + LONG) Window to avoid Glitches MOVING <->ENGINE_OFF Transitions.
            return update_decision(APM_Decision::eAPM_Decision_Moving);
        }

    }
    LOG_I(get_TAG(), "%s: No state change, returning current state: %d", __func__, mWorkerData.getMotionState());
    return mWorkerData.getMotionState();
}

void APM_worker::log_data(bool force) {
    if(0 == logging_interval_sec) {
        return;
    }

    if( ((get_index() % logging_interval_sec) < get_read_interval() ) || (true == force) ) {
        DataValidCount  valid_cnt = {0,0};
        string valid_sysfs_path = getWorkerBinDataObj()->get_valid_sysfs_path();
        if(valid_sysfs_path.empty() != true) {
            get_data_validity_count(valid_sysfs_path, valid_cnt);
        }

        int64_t udid = -1, sid = -1;
        if(get_file_xattr(ND_UDID_SID, FileMetadataKey::udid, &udid, sizeof(udid))!= XATTR_OK){
            LOG_E(TAG, "Read fail. udid is: %lld", udid);
        }
        if(get_file_xattr(ND_UDID_SID, FileMetadataKey::sid, &sid, sizeof(sid))!= XATTR_OK){
            LOG_E(TAG, "Read fail. sid is: %lld", sid);
        }

        LOG_I(get_TAG(), "%s", getWorkerBinDataObj()->get_data_string(get_decision_index()).c_str());

        std::string is_event = (is_event_driven) ? "EVNT " : "IDLE ";

        LOG_I(get_TAG(), "S: %s(%d), IDX: %u, DIDX %u, DC - OFF %u, ON %u, %s %u, PDC - OFF %u, ON %u, %s %u, OC %u, RC %llu, GC %llu, UDID %lld, SID %lld",
                getMotionState() == motion_status_t::MOVING ? "ON" : "OFF",
                getMotionState(),
                get_index(),
                get_decision_index(),
                get_decision_count(APM_Decision::eAPM_Decision_Engine_Off),
                get_decision_count(APM_Decision::eAPM_Decision_Moving),
                is_event.c_str(),
                get_decision_count(APM_Decision::eAPM_Decision_Idling),
                get_decision_count(APM_Decision::eAPM_Decision_Engine_Off, get_prev_decision_index()),
                get_decision_count(APM_Decision::eAPM_Decision_Moving, get_prev_decision_index()),
                is_event.c_str(),
                get_decision_count(APM_Decision::eAPM_Decision_Idling, get_prev_decision_index()),
                get_data_outage_count(),
                valid_cnt.reader_count,
                valid_cnt.generator_count,
                udid,
                sid
            );
    }
}

bool APM_worker::handle_data_outage() {

    if(true == is_data_outage()) {

	    LOG_D(get_TAG(),"Vehicle is forced into STATIONARY if data_outage == true");
	    setMotionState(STATIONARY);
	    set_decision_count(APM_Decision::eAPM_Decision_Engine_Off, 0);
	    set_decision_count(APM_Decision::eAPM_Decision_Moving, 0);
        update_fusion_count(APM_Decision::eAPM_Decision_Engine_Off, true);

        if(false == is_data_outage_event) {

            if(false == use_legacy){
                reset_bit(get_signal_mask());
            }

            std::string toggle_event = (is_event_driven) ? "Toggle Count" : "Data Outage Count";
            uint count = (is_event_driven) ? get_decision_count(APM_Decision::eAPM_Decision_Debounce) : get_data_outage_count();
            //send critical info
            std::string err_msg = get_TAG() + std::string(": ") + toggle_event + std::string(": ") + to_string(count);

            (NDService::get_service_obj(TAG))->send_err_msg(SM_E_APM_EVENT_STATUS, count, err_msg);
            is_data_outage_event = true;
        }
	    return true;
    }
    else {
        if(true == is_data_outage_event) {

            std::string toggle_event = (is_event_driven) ? "Toggling Clear" : "Data Outage Clear";
            std::string err_msg = get_TAG() + std::string(": ") + toggle_event;

            int aux_code = (1 << AUX_MAGIC_NUM_THREE);
            (NDService::get_service_obj(TAG))->send_err_msg(SM_E_APM_EVENT_STATUS, aux_code, err_msg);
            is_data_outage_event = false;
        }
    }

    return false;

}

void APM_worker::filter_func_new () {

    LOG_D(TAG, "In %s", __func__);

    if(true == handle_data_outage()) {
        return;
    }

    motion_status_t prev_motion_state = getMotionState();

    // If already in STATIONARY, check the crank voltage for SHORT_WINDOW
    // If crank voltage is above threshold for SHORT_WINDOW duration, then go to MOVING
    // If already in MOVING, check the crank voltage for LONG_WINDOW
    // If crank voltage is below threshold for  LONG_WINDOW duration, then go to STATIONARY
    APM_Decision decision = (STATIONARY == prev_motion_state) ? APM_Decision::eAPM_Decision_Moving : APM_Decision::eAPM_Decision_Engine_Off;
    LOG_D(get_TAG(), "Decision to be evaluated: %d", decision);

#if 0
    // Fusion Activation is based on the Fusion Threshold based Counters.
    // Once Activated.... Stays Activated until the next service start.
    if((true == is_fusion_enabled()) && (false == get_fusion_activated())) {

	    // If STATIONARY , then check fusion off count is reflecting OFF over the Short Window which may Move to ON  based on the Non Fusion Thresholds.
	    // If STATIONARY , then check fusion off count is reflecting ON  over the Short Window which may Move to OFF based on the Non Fusion Thresholds.
	    // If MOVING, then check fusion on count is reflecting ON  over the Long Window which may Move to OFF based on the Non Fusion Thresholds.
	    // If MOVING, then check fusion on count is reflecting OFF over the Long Window which may Move to ON  based on the Non Fusion Thresholds.
	    DecisionWindow fusion_window = ( decision == APM_Decision::eAPM_Decision_Moving ) ? eDecisionWindow_Short : eDecisionWindow_Long ;

	    uint moving_count = get_decision_count(APM_Decision::eAPM_Decision_Moving, get_prev_decision_index());
	    uint engine_off_count = get_decision_count(APM_Decision::eAPM_Decision_Engine_Off, get_prev_decision_index());

	    uint fusion_moving_count = get_decision_count(APM_Decision::eAPM_Decision_Fusion_Moving, get_prev_decision_index());
	    uint fusion_engine_off_count = get_decision_count(APM_Decision::eAPM_Decision_Fusion_Engine_Off, get_prev_decision_index());

	    if( ( (moving_count + engine_off_count) >= (get_decision_interval(fusion_window) - 1))
			    && ( ( fusion_engine_off_count >= moving_count ) || (fusion_moving_count >= engine_off_count ) )
			    && ( (true == is_continuous_motion(APM_Decision::eAPM_Decision_Fusion_Engine_Off, get_prev_decision_index(), get_decision_interval(eDecisionWindow_Continuous)))
                || (true == is_continuous_motion(APM_Decision::eAPM_Decision_Fusion_Moving, get_prev_decision_index(), get_decision_interval(eDecisionWindow_Continuous))) ) ) {

		    set_fusion_activated(true); // Activate Fusion Thresholds
		    set_decision_count(eAPM_Decision_Moving,     fusion_moving_count); // Update MOVING count based on FUSION decision
		    set_decision_count(eAPM_Decision_Engine_Off, fusion_engine_off_count); // Update ENGINE OFF count based on FUSION decision
        }
    }
#endif

    setMotionState(update_decision(decision));

    send_signal_status(prev_motion_state);

    return;
}

uint APM_worker::get_index() {
    return mWorkerData.getIndex();
}

uint APM_worker::get_prev_index() {

    return ( (get_index() - 1 + mWorkerData.getDecisionInterval(eDecisionWindow_Cache)) % mWorkerData.getDecisionInterval(eDecisionWindow_Cache) );
}

uint APM_worker::get_decision_index() {
    return get_prev_index();
}

uint APM_worker::get_prev_decision_index() {
    return ( (get_decision_index() - 1 + mWorkerData.getDecisionInterval(eDecisionWindow_Cache)) % mWorkerData.getDecisionInterval(eDecisionWindow_Cache) );
}

void APM_worker::incr_index() {
    mWorkerData.incrIndex();
}

bool APM_worker::is_data_outage() {
    return (mWorkerData.getDataOutageCnt() > mWorkerData.getDecisionInterval(eDecisionWindow_Outage) );
}

uint APM_worker::get_data_outage_count() {
    return mWorkerData.getDataOutageCnt();
}

string APM_worker::get_data_string() {
    return getWorkerBinDataObj()->get_data_string(get_decision_index());
}

std::string APM_worker::get_threshold_string() {
    if(mBinData != nullptr) {
        return mBinData->get_thres_string();
    }
    else {
        LOG_E(TAG, "Failed to get threshold string: mBinData is null");
        return "";
    }
}

uint APM_worker::get_read_interval() {
    return mWorkerData.getDecisionInterval(eDecisionWindow_Sleep);
}

uint APM_worker::get_decision_interval(DecisionWindow dWind) {
    return mWorkerData.getDecisionInterval(dWind);
}

void APM_worker::set_decison_interval(DecisionWindow dWind, uint interval) {
    mWorkerData.setDecisonInterval(dWind, interval);
}

motion_status_t APM_worker::getMotionState() {
    return mWorkerData.getMotionState();
}

void APM_worker::setMotionState(motion_status_t motion_status) {
    mWorkerData.setMotionState(motion_status);
}

void APM_worker::initMotionState() {
    motion_status_t motion_status = STATIONARY;
    mWorkerData.setMotionState(motion_status);
}

// Only using for motion sensor(IMU, GPS, CAN) in read status
motion_status_t APM_worker::getInitialState() {

    int64_t uptime = MS_TO_S(get_system_monotonic_time()); // Convert to seconds
    motion_status_t motion_status = STATIONARY;
    if(uptime < DEF_UPTIME_THRESHOLD_SEC) {
        motion_status = read_ign_state_from_attr();
    }
    return motion_status;
}

void APM_worker::update_attr_status(motion_status_t status, bool is_event, bool force) {

    constexpr uint interval_sec = 60; // 1 min
    if((get_index() % interval_sec < get_read_interval()) || (true == force)) {
        int64_t curr_time = MS_TO_S(get_system_time());
        apm_attr_util::set_attr_status(vehicle_ign_state, USER_TIME, to_string(curr_time).c_str());

        if(false == is_event) {
            return;
        }
    }

    std::string status_str = (status == MOVING) ? MOVING_STR : STATIONARY_STR;
    apm_attr_util::set_attr_status(vehicle_ign_state, get_attr_user(), status_str.c_str());
}

motion_status_t APM_worker::read_ign_state_from_sysfs() {

    int status = STATIONARY;
    std::string path = get_src_status_sysfs_path();
    read_from_sysfs_entry(path, status);

    return (status == (int)MOVING) ? MOVING : STATIONARY;
}

motion_status_t APM_worker::read_ign_state_from_attr() {

    // Check the last update timestamp, if time is less then idle time which is 3 min then only read otherwise return STATIONARY
    int64_t curr_system_time = MS_TO_S(get_system_time()); // use as default
    // TODO: validate curr_system_time, epoch for default value 1971

    std::string time = apm_attr_util::get_attr_status(vehicle_ign_state, USER_TIME, to_string(curr_system_time).c_str());
    int64_t last_update_time = 0;

    if(false == string_to_int64(time, last_update_time)) {
        LOG_E(get_TAG(), "%s: ERROR!!! Converting time string to int64_t failed for time string: %s", __func__, time.c_str());
    }

    if((MS_TO_S(get_system_time()) - last_update_time) > 180) {
        return STATIONARY;
    }

    std::string status_str = apm_attr_util::get_attr_status(vehicle_ign_state, get_attr_user(), STATIONARY_STR);
    return (MOVING_STR == status_str) ? MOVING : STATIONARY;
}

bool APM_worker::is_idling(uint index) {
    return getWorkerBinDataObj()->is_idling(index);
}

float APM_worker::get_threshold(APM_Decision decision) {
    return getWorkerBinDataObj()->get_threshold(decision);
}

void APM_worker::update_calibration_data() { 

    //    getWorkerBinDataObj()->update_calibration_data(get_decision_index());
    // Dummy function for now
    return;
}

bool APM_worker::get_fusion_activated() {
    return mWorkerData.getFusionActivated();
}

void APM_worker::set_fusion_activated(bool status) {
    mWorkerData.setFusionActivated(status);
}

void APM_worker::test_event_driven_worker_fn() {

    std::string test_file_path = get_test_file_path();
    if(false == file_is_present(test_file_path)) {
        return;
    }

    //fetch file name from test_file_path
    std::string file_name = test_file_path.substr(test_file_path.find_last_of("/") + 1);
    std::string log_dir = "/home/ubuntu/.nddevice/log/apm/test/" + std::string(file_name);
    std::string log_file = log_dir + "/log_" + to_string(get_system_time()) + ".log";

    if(false == create_directories_recursive(log_dir.c_str())) {
        printf("unable to create log directory : %s ; Exiting from power_monitor\n", log_dir.c_str());
        return;
    }

    // open log file
    std::ofstream log_stream(log_file.c_str(), std::ios::out | std::ios::app);
    if(!log_stream.is_open()) {
        printf("unable to open log file : %s ; Exiting from power_monitor\n", log_file.c_str());
        return;
    }

    log_stream << "Start Time: " << get_system_time() << std::endl;
    log_stream << "Test File Path: " << test_file_path << std::endl;

    std::string gpio_ign_status = apm_attr_util::get_attr_status(vehicle_ign_state, USER_IGN, STATIONARY_STR);
    std::string power_volt_status = apm_attr_util::get_attr_status(vehicle_ign_state, USER_PWR, STATIONARY_STR);
    std::string imu_status = apm_attr_util::get_attr_status(vehicle_ign_state, USER_IMU, STATIONARY_STR);
    std::string gps_status = apm_attr_util::get_attr_status(vehicle_ign_state, USER_GPS, STATIONARY_STR);
    std::string can_status = apm_attr_util::get_attr_status(vehicle_ign_state, USER_CAN, STATIONARY_STR);
    std::string pfi_status = apm_attr_util::get_attr_status(vehicle_ign_state, USER_PFI, STATIONARY_STR);

    log_stream << "Initial Status - IGN: " << gpio_ign_status
               << ", PWR: " << power_volt_status
               << ", IMU: " << imu_status
               << ", GPS: " << gps_status
               << ", CAN: " << can_status
               << ", PFI: " << pfi_status << std::endl;

    sleep(30); // wait for 30 seconds so each source is initialized properly and start reading data

    // read file for number of events to simulate
    // format: event,sleep
    int num_events = 0;
    int sleep_time_ms = 0;

    {
        FILE *fp = fopen(test_file_path.c_str(), "r");
        if(fp == NULL) {
            log_stream << "ERROR!!! Opening test file: " << test_file_path << " failed" << std::endl;
            return;
        }
        fscanf(fp, "%d,%d", &num_events, &sleep_time_ms);
        fclose(fp);
    }

    log_stream << "Simulating " << num_events << " events with sleep time " << sleep_time_ms << " ms from test file: " << test_file_path << std::endl;

    int toggle_status = 0;
    for(int i = 0; i < num_events; ++i) {
        toggle_status = !toggle_status;
        log_stream << "Simulating event " << (i+1) << "/" << num_events << ", toggle_status: " << toggle_status << std::endl;
        test_callback_fn(toggle_status);
        usleep(1000 * sleep_time_ms);
    }

    log_stream << "End Time: " << get_system_time() << std::endl;
    log_stream.flush();
    log_stream.close();
}

