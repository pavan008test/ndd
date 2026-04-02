/* Copyright (C) 2019 - 2020 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Devendra Yadav <devendra.yadav@netradyne.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <stdbool.h>
#include <pthread.h>
#include <semaphore.h>
#include <mutex>
#include <unistd.h>
#include <iostream>
#include <fstream>
#include "apm.h"
#include "apm_ignition.h"
#include "apm_worker.h"
#include "ndmb/nd_msg_interface.h"
#include "ndmb/nd_mbserver.h"
#include "nd_msg_utils.h"

#define TAG "A_IGNS"
// Undef if we don't want a debounce read of  IGN STATUS
#define IGN_STAT_DEBOUNCE 1
#define APM_IGN_POLL_TIME 30 //seconds
#define APM_IGN_DEBOUNCE_TIME (1000*50) //ms
#define APM_IGN_DEBOUNCE_COUNT 3

#ifdef IGNITION_BROADCAST
        extern NDMBServer server;
#endif

#define nd_device_obj (ND_DeviceFactory::Create_NDDevice())

extern NDService *nd_service_obj;
extern int all_thread_keepalive_status;
using namespace std;

int aon_curr_ign_status = IGNITION_ERR;
std::mutex igns_lock;

//static const int QCS_HEARTBEAT_DETECTION_MASK = 0x40;

/*
* read_aon_ign_status()
* Local method to read the ignition status with a debounce logic
*/


bool read_aon_ign_status(ignition_status_t &status)
{
	bool ret = true; 
	int curr_ign_status[APM_IGN_DEBOUNCE_COUNT] = {IGNITION_ERR, IGNITION_ERR, IGNITION_ERR};
	int ign_error_count = 0;

	for (int i = 0; i < APM_IGN_DEBOUNCE_COUNT; ++i) {
		usleep(APM_IGN_DEBOUNCE_TIME); // 50ms
		int ign_status = nd_device_obj->get_ignition_status();

		if (IGNITION_ERR == ign_status) {
			LOG_E(TAG, " Unable to read current ign status , Cached Ignition state : 0x%02x", aon_curr_ign_status);
			nd_service_obj->send_err_msg(SM_E_APM_MSP_FAIL, 0, "read curr ign status failed");
			ret = false;
			ign_error_count++;
			LOG_E(TAG, "Ignition Error Count : %d", ign_error_count);
		}
		else {
			ret = true;
		}
		curr_ign_status[i] = ign_status;

		if (ign_error_count >= APM_IGN_DEBOUNCE_COUNT) {
			msp_status_msg_t msg;
			msg.status = false;
			msg.len = sizeof(msp_status_msg_t);

			// Send message to power_monitor for reboot to recover from MSP failure
			if (send_msg((generic_msg_t *)&msg, REQ_POWERMON_MSP_FAIL_TO_REBOOT, sizeof(msp_status_msg_t), "q_apm", "q_power_monitor", 0) == false) {
				LOG_E(TAG, "Failed to send MSP status message to power monitor");
			}
			ign_error_count = 0;
		}
	}

	// Log Values incase of change.
	if( aon_curr_ign_status != curr_ign_status[0] ) {
		LOG_D(TAG, "read_aon_ign_status() Cached Ignition state : 0x%02x. Curr Ignition state : 0x%02x", aon_curr_ign_status, curr_ign_status[0]);
	}

	status = ignition_status_t::IGNITION_ERR; // Default to error

	if( false == ret )
	{
		return ret;
	}

	// Update aon_curr_ign_status with value read
	if( ( curr_ign_status[0] == curr_ign_status[1] ) && ( curr_ign_status[0] == curr_ign_status[2] ) )
	{
		igns_lock.lock();
		aon_curr_ign_status = curr_ign_status[0] ;
		igns_lock.unlock();
	}
	else// Don't Update. Log the Values read.
	{
		for( int i = 0 ; i < 3 ; ++i )
		{
			LOG_E(TAG, " read_aon_ign_status() Error in read current ign status , Ignition state : 0x%02x, aon_curr_ign_status: 0x%02x", curr_ign_status[i], aon_curr_ign_status);
		}
	}
	status = static_cast<ignition_status_t>(aon_curr_ign_status);
	return true;
}

void IGNS_worker::initMotionState() {
	motion_status_t motion_status = STATIONARY;
	motion_status = (IGNITION_ON == nd_device_obj->get_ignition_status()) ? MOVING : STATIONARY;
	setMotionState(motion_status);
}

/*
 * Func name: IGNS_worker::filter_func()
 * function write the intr value to intr mask
 */
void IGNS_worker::filter_func() {

    if(use_legacy == false){
        return;
    }

    static int previous_ignition_bit_status = IGNITION_ERR;// Initialize with error so that first event is always honored
    int ignition_bit_status = aon_curr_ign_status;


    if(previous_ignition_bit_status != ignition_bit_status) {
        if( IGNITION_OFF == ignition_bit_status ) {
           //Reset IGNS bit
            reset_bit(IGNS_MASK_POS);
            LOG_D(TAG, "IGNS Reset signal sent");
            LOG_D(TAG, "filter_func() Ignition state : 0x%02x", aon_curr_ign_status);
        } else if( IGNITION_ON == ignition_bit_status) {
            //Set IGNS bit
            set_bit(IGNS_MASK_POS);
            LOG_D(TAG, "IGNS Set signal sent");
            LOG_D(TAG, "filter_func() Ignition state : 0x%02x", aon_curr_ign_status);
        }
        else {
            LOG_C(TAG, "filter_func() Unknown Ignition state : 0x%02x", aon_curr_ign_status);
        }
		previous_ignition_bit_status = ignition_bit_status;
    }
}

void IGNS_worker::send_ign_status(ignition_status_t status) {

	std::string valid_sysfs_path = getWorkerBinDataObj()->get_valid_sysfs_path();

    DataValidCount  valid_cnt;
    get_data_validity_count(valid_sysfs_path, valid_cnt);

	const uint debounce_count = get_decision_count(eAPM_Decision_Debounce, get_prev_decision_index());
	const uint present_debounce_count = get_decision_count(eAPM_Decision_Debounce, get_decision_index());

	uint event_cnt = (debounce_count > present_debounce_count) ?
					(debounce_count - present_debounce_count) :
					(present_debounce_count - debounce_count);

	if((event_cnt < get_decision_interval(eDecisionWindow_Valid)) &&
		(false == is_data_outage()) &&
		(false == is_continuous_motion(eAPM_Decision_Debounce, get_prev_decision_index(), get_decision_interval(eDecisionWindow_Valid)))) {

		LOG_D(TAG, "setMotionState() Ignition status: %d", status);
		setMotionState( (status == IGNITION_ON) ? MOVING : STATIONARY);
		if(false == use_legacy) {
			(getMotionState() == STATIONARY) ? reset_bit(get_signal_mask()) : set_bit(get_signal_mask());
		}
	}

	set_decision_count(eAPM_Decision_Debounce, get_decision_count(eAPM_Decision_Debounce, get_decision_index()) + 1, is_event_driven);
	push_data_to_sysfs(PowermonParam::eIGN_VALID, 0); // Update IGN Validity to true

	// update ignition_event in  attribute
	apm_attr_util::set_attr_status(vehicle_attributes, USER_IGNITION_EVENT_STR, std::to_string(get_decision_count(eAPM_Decision_Debounce, get_decision_index())).c_str());
	// update timestamp
	int64_t curr_time = MS_TO_S(get_system_time());
	apm_attr_util::set_attr_status(vehicle_attributes, USER_TIME, std::to_string(curr_time).c_str());
}

/*
 * Func name: ignCallback()
 * function registered by IGNS_worker::intr_func ,
 * updated the igns state the sooner igns interrupt generates
 * returns POWER_SUCCESS on success and POWER_FAILURE on failure
 */
int ignCallback() {

	bool ret = false;
#if IGN_STAT_DEBOUNCE
	ignition_status_t status;
	ret = read_aon_ign_status(status);
#else
	int ign_status = nd_device_obj->get_ignition_status();
	if(IGNITION_ERR != ign_status ) {
		ret = true;
		igns_lock.lock();
		aon_curr_ign_status = ign_status; 
		igns_lock.unlock();
	}
#endif

	LOG_D(TAG, "ignCallback()  current ign status = 0x%02x" , aon_curr_ign_status);

	if(true != ret) {
		LOG_E(TAG, "ignCallback failed");
		nd_service_obj->send_err_msg(SM_E_APM_MSP_FAIL, 0, "read curr status callback failed");
		return -1;
	}

	APM::WorkerInstance(eCrankLine)->filter_func();

	IGNS_worker* ign_worker = static_cast<IGNS_worker*>(APM::WorkerInstance(eCrankLine));
	ign_worker->send_ign_status(status);


	return 0;
}


void IGNS_worker::insert_bin_data() {


    all_thread_keepalive_status &= ~(1 << get_signal_mask());

    IGNData ign_data;
	read_aon_ign_status(ign_data.status);

	push_data_to_sysfs(PowermonParam::eIGN_VALID, 0); // Update IGN Validity to true

    getWorkerBinDataObj()->set_bin_data(get_index(), (void *) &ign_data);


	incr_index();

	// persist debounce count
	set_decision_count(eAPM_Decision_Debounce, 0);
}


/*
 * Func name: IGNS_worker::poll_func()
 * function polls ignition state after every APM_IGN_POLL_TIME sec
 * returns NULL
 */
void* IGNS_worker::poll_func() {

	LOG_I(TAG, "Dummy function do nothing here");
	detect_engine_status();
	return NULL;
}

static bool ign_glitch_report() {
	int pow_on_off_reason = 0;
	string reason("");
    nd_device_obj->get_reset_wake_reason(pow_on_off_reason, reason);
    bool wakeonign_detected = pow_on_off_reason & ( 1 << PowerOnTriggerT::WAKEonIGNITION);
    bool ign_detected = (IGNITION_ON == aon_curr_ign_status);
    if( wakeonign_detected ^ ign_detected ) {
		string err_msg = "Glitch Detected: WakeOnIGN(" + std::to_string(wakeonign_detected) + ") IGN(" + std::to_string(ign_detected) + ")";
        LOG_C(TAG, "%s", err_msg.c_str());
		nd_service_obj->send_err_msg(SM_E_APM_EVENT_STATUS, static_cast<int>(eIGN_GLITCH_AUX_CODE), err_msg);
		return true;
    }
	return false;
}

void IGNS_worker::test_callback_fn(int status) {
   ignition_status_t ign_status = (status == 0) ? IGNITION_OFF : IGNITION_ON;
   send_ign_status(ign_status);
}

/*
 * Func name: IGNS_worker::intr_func()
 * function registers ignCallback to get ignition state
 * returns NULL on success and APM_ERROR on failure
 */
void* IGNS_worker::intr_func() {

	NDDeviceTypeT deviceType = ND_DeviceFactory::getBuildDeviceType();

	ignition_status_t status;
	read_aon_ign_status(status);
	ign_glitch_report();


	// Only If test file is present , start the test thread
	if ( true == file_is_present(get_test_file_path()) ) {
		// lambda function to bind member function
		test_event_driven_thread = std::thread(&APM_worker::test_event_driven_worker_fn, this);
	}

	while(1) {
			if(false == nd_device_obj->register_ignition_interrupt(&ignCallback)) {
				LOG_E(TAG, "registercallback failed");
				nd_service_obj->send_err_msg(SM_E_APM_MSP_FAIL, 0, "ign registercallback failed");
				sleep(2);
				continue;
			}
			break;
	}

	if(test_event_driven_thread.joinable()) {
		test_event_driven_thread.join();
	}

	return NULL;
}

/*
 * Func name: IGNS_worker::read_status()
 * function reads the current status of the igntion
 * returns motion_status_t
 */
motion_status_t IGNS_worker::read_status() {

	if((false == enabled) || (true == sensor_decision_disabled)) {
		LOG_D(TAG, "Ignition worker(%d), sensor decision disabled(%d), return STATIONARY", enabled, sensor_decision_disabled);
		return STATIONARY;
	}

    bool ret=false;
	string ign_gpio_sysfs_path = nd_device_obj->get_ign_gpio_sysfs_path();

    if(false == APM::WorkerInstance(eCrankLine)->use_legacy){
        if (STATIONARY == getMotionState()) {
            write_into_sysfs_entry(ign_gpio_sysfs_path, IGNITION_OFF);
        }
        else if(MOVING == getMotionState()) {
            write_into_sysfs_entry(ign_gpio_sysfs_path, IGNITION_ON);
        }
        else {
            LOG_E(TAG, "Unknown motion state : %d", getMotionState());
            write_into_sysfs_entry(ign_gpio_sysfs_path, IGNITION_OFF);
        }
        return getMotionState();
    }

#if IGN_STAT_DEBOUNCE
	ignition_status_t status;
    ret = read_aon_ign_status(status);
#else
    int ign_status = nd_device_obj->get_ignition_status();
    if(IGNITION_ERR != ign_status ) {
	    ret = true;
	    igns_lock.lock();
	    aon_curr_ign_status = ign_status; 
	    igns_lock.unlock();
    }
#endif
    LOG_D(TAG, "read_status() Ignition state : 0x%02x", aon_curr_ign_status);

    if(true != ret) {
        LOG_E(TAG, "Unable to read current ign status");
        nd_service_obj->send_err_msg(SM_E_APM_MSP_FAIL, 0, "aon read curr ign status failed");
		write_into_sysfs_entry(ign_gpio_sysfs_path, IGNITION_OFF);
        return STATIONARY;
    } else {
#ifdef IGNITION_BROADCAST
	    std::shared_ptr<ndmbmsg_apm_ign_status_t>  apm_ign_status(new ndmbmsg_apm_ign_status_t);
	    apm_ign_status->ign_status = aon_curr_ign_status;
	    bool res = server.publish(TOPIC_APM_IGN_STATUS, apm_ign_status);
	    if (!res){
		    LOG_E(TAG, "APM_IGN_STATUS publish error!!!!!");
	    }
#endif
        if( IGNITION_OFF == aon_curr_ign_status) {
			write_into_sysfs_entry(ign_gpio_sysfs_path, IGNITION_OFF);
            return STATIONARY;
        } else if( IGNITION_ON == aon_curr_ign_status ) {
			write_into_sysfs_entry(ign_gpio_sysfs_path, IGNITION_ON);
            return MOVING;
        } else {
            LOG_I(TAG, "Unknown Ignition state : 0x%02x", aon_curr_ign_status);
			write_into_sysfs_entry(ign_gpio_sysfs_path, IGNITION_OFF);
            return MOVING;
        }
    }

	write_into_sysfs_entry(ign_gpio_sysfs_path, IGNITION_ON);
    return MOVING;
}

const char* IGNS_worker::get_TAG() {
	return TAG;
}


void IGNS_worker::detect_easy_install_mode() {

	// read ignition event count from attribute
	std::string ign_event_str = apm_attr_util::get_attr_status(vehicle_attributes, USER_IGNITION_EVENT_STR, "0");
	int ign_event_count = 0;
	if(false == string_to_integer(ign_event_str, ign_event_count)) {
		LOG_E(TAG, "Failed to parse ignition event count string: %s", ign_event_str.c_str());
		return ;
	}

	if( (ignition_status_t::IGNITION_ON == nd_device_obj->get_ignition_status()) || (ign_event_count > MAX_CNT_FOR_EASY_INSTALL)) {
		apm_attr_util::set_attr_status(vehicle_attributes, USER_EASY_INSTALL_STR, FALSE_STR);
		return;
	}

	std::string last_event_time_str = apm_attr_util::get_attr_status(vehicle_attributes, USER_LAST_IGN_EVENT_TIME_STR, "0");
	int64_t last_event_time = 0;
	if(false == string_to_int64(last_event_time_str, last_event_time)) {
		LOG_E(TAG, "Failed to parse last ignition event time string: %s", last_event_time_str.c_str());
		return ;
	}

	int64_t curr_time = MS_TO_S(get_system_time());
	int64_t time_diff = (curr_time > last_event_time) ? (curr_time - last_event_time) : 0;

	// If event is 0 and last event time is older than 24 hours then only make it true
	if( (ign_event_count <= MAX_CNT_FOR_EASY_INSTALL) && (time_diff > (ONE_DAY_SECONDS * WEEK_DAYS)) ) {
		apm_attr_util::set_attr_status(vehicle_attributes, USER_EASY_INSTALL_STR, TRUE_STR);
	}
}