#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <string.h>
#include <pthread.h>
#include <string>
#include "apm.h"
#include "apm_can.h"
#include "apm_worker.h"
#include "system_utils.h"

#define TAG "A_CAN"
#define nd_device_obj (ND_DeviceFactory::Create_NDDevice())

extern int all_thread_keepalive_status;

motion_status_t CAN_worker::read_status() {

    if((false == enabled) || (true == sensor_decision_disabled)) {
        LOG_D(TAG, "CAN worker(%d), sensor_decision_disabled(%d). Returning STATIONARY", enabled, sensor_decision_disabled);
        return STATIONARY;
    }

    if(use_legacy == true){
        return STATIONARY;
    }

    motion_status_t status =  STATIONARY;

    if(false == init_state_read) {
        status = getInitialState();
        init_state_read = true;
    }
    else {
        status = getMotionState();
    }

    string ign_can_sysfs_path = nd_device_obj->get_can_ign_sysfs_path();

    if (STATIONARY == status) {
        write_into_sysfs_entry(ign_can_sysfs_path, IGNITION_OFF);
    }
    else if(MOVING == status) {
        write_into_sysfs_entry(ign_can_sysfs_path, IGNITION_ON);
    }
    else {
        LOG_E(TAG, "Unknown motion state : %d", status);
        write_into_sysfs_entry(ign_can_sysfs_path, IGNITION_OFF);
    }
    return status;
}

/* Func Name : poll_func()
* Desc      : Dummy function do nothing
*/
void* CAN_worker::poll_func() {
    LOG_D(TAG, "In %s", __func__);
    // Dummy function do nothing here
    return NULL;
}

void CAN_worker::filter_func() {
    LOG_D(TAG, "In %s", __func__);
    // Dummy function do nothing here
    return;
}

void CAN_worker::insert_bin_data() {
    LOG_D(TAG, "In %s", __func__);

    all_thread_keepalive_status &= ~(1 << get_signal_mask());

    CanData curr_data = {};
    curr_data.can_data[eCAN_DATA_RPM] = get_status_from_sysfs_source(PowermonParam::eCAN_RPM);
    curr_data.can_data[eCAN_DATA_ENG_STAT] = get_status_from_sysfs_source(PowermonParam::eCAN_ENGINE_STAT);
    curr_data.can_data[eCAN_DATA_SPEED] = get_status_from_sysfs_source(PowermonParam::eCAN_SPEED);

    // Insert CAN data into bin data structure
    getWorkerBinDataObj()->set_bin_data( get_index(), (void*)&curr_data );
    incr_index();

    if(true == update_reader_count) {
        // If device type is D2XX call update_data_validity again to increment reader count properly, As obd updating valid entry at 2 hz rate.
        // If power volt worker is disabled.
        std::string valid_sysfs_path = get_sysfs_path_from_enum(PowermonParam::eOBD_VALID);
        update_data_validity(valid_sysfs_path, false, is_event_driven, get_decision_interval(eDecisionWindow_Valid));
    }
}

void* CAN_worker::intr_func() {
    LOG_I(TAG, "In %s", __func__);
    detect_engine_status();
    return NULL;
}

const char* CAN_worker::get_TAG(){
    return TAG;
}