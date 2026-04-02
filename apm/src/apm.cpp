/* Copyright (C) 2019 - 2020 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Devendra Yadav <devendra.yadav@netradyne.com>
 * Written by Hari Seenivasan <hari.seenivasan@netradyne.com>
 */

#include <iostream>
#include <stdio.h>
#include <unistd.h> 
#include <stdlib.h>
#include <string>
#include <errno.h>
#include <fstream>
#include <pthread.h>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <sys/time.h>
#include "service_utils.h"
#include "apm.h"
#include "apm_imu.h"
#include "apm_gps.h"
#include "apm_can.h"
#include "apm_ignition.h"
#include "apm_supercap.h"
#include "apm_power_volt.h"
#include "apm_worker.h"
#include "nd_time.h"
#include "svc.h"
#include "wake_up_reason.h"

#define TAG "APM"
std::mutex sysfs_file_lock;
bool ready = false;

vector<thread> poll_thread_ptr;
vector<thread> intr_thread_ptr;

extern NDService *nd_service_obj;
extern ND_DeviceFactory *nd_device_obj;
extern string apm_igns_enable_str;
extern string apm_imu_enable_str;
extern string apm_sc_enable_str;
extern int all_thread_enable_status;
extern int vehicle_idle_time;

extern bool apm_motion_detection_enable;

extern int pow_on_off_reason;
extern bool apm_wom_enable;
extern bool apm_ign_wake_enable;
extern bool glitch_suppression_support;

extern bool apm_power_volt_enable;
extern bool apm_ign_volt_enable;
extern bool apm_can_enable;

APM* APM::apm_context = NULL;
APM_worker*  APM::pWorkers[eIgnSrcMax] = {NULL};

std::atomic<int> all_thread_keepalive_status(0);
std::mutex keepalive_status_mutex;

static const int32_t COND_VAR_TIMEOUT = 2;
volatile int bit_mask = 0;

pthread_mutex_t m_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_condattr_t m_attr;
pthread_cond_t m_cond = PTHREAD_COND_INITIALIZER;

struct SrcStatus {

    // Pseudo Ignition Status based on all sources
    motion_status_t pseudo_ignition_status;

    // Ignition Source Status (Non Motion)
    bool ign_sensor_enable;
    motion_status_t ign_line_status;
    motion_status_t ign_sensor_status;

    bool power_volt_sensor_enable;
    motion_status_t power_volt_status;
    motion_status_t power_volt_sensor_status;

    // Motion Source Status
    bool imu_sensor_enable;
    motion_status_t imu_status;
    motion_status_t imu_sensor_status;

    bool gps_sensor_enable;
    motion_status_t gps_status;
    motion_status_t gps_sensor_status;

    bool can_sensor_enable;
    motion_status_t can_status;
    motion_status_t can_sensor_status;

    // Power Failure Indicator
    bool sc_sensor_enable;
    motion_status_t sc_status;
    motion_status_t sc_sensor_status;
};

using namespace std;


void reset_bit(int pos) {
    pthread_mutex_lock(&m_mutex);
    ready = true;
    bit_mask &= ~(1<<pos);
    pthread_cond_signal(&m_cond);
    pthread_mutex_unlock(&m_mutex);
}

void set_bit(int pos) {
    pthread_mutex_lock(&m_mutex);
    ready = true;
    bit_mask |= (1<<pos);
    pthread_cond_signal(&m_cond);
    pthread_mutex_unlock(&m_mutex);
}


bool read_from_sysfs(ignition_status_t *value) {

    int local_value = 0;

    std::lock_guard<std::mutex> lk(sysfs_file_lock);
    std::string crank_level_info_file = nd_device_obj->gpio_crank_level_info_file();

    if( false == read_from_sysfs_entry(crank_level_info_file, local_value)) {
        LOG_E(TAG, "unable to read ignition sysfs file");
        nd_service_obj->send_err_msg(SM_E_APM_FILE_WRITE_FAIL, 0, "sysfs file read failed");
        return false;
    }

    *value = (ignition_status_t)local_value;
    LOG_I(TAG, "Read value %d from pseudo ignition", local_value);
    return true;

}

/*
 * function name : write_to_sysfs
 * function is to write ignition status to sysfs
 * arg1 : is the value need to write on sysfs
 * returns true to success and false on failure
 */
bool write_to_sysfs(ignition_status_t value) {
    std::lock_guard<std::mutex> lk(sysfs_file_lock);
    std::string crank_level_info_file = nd_device_obj->gpio_crank_level_info_file();

    int prev_value = 0;
    if( false == read_from_sysfs_entry(crank_level_info_file, prev_value) ) {
        LOG_E(TAG, "failed in read_from_sysfs");
        return false;
    }

    if(prev_value != (int)value) {
        if( false == write_into_sysfs_entry(crank_level_info_file, (int)value)) {
            LOG_E(TAG, "unable to write in ignition sysfs file");
            nd_service_obj->send_err_msg(SM_E_APM_FILE_WRITE_FAIL, 0, "sysfs file write failed");
            return false;
        }
    }
    else {
        // LOG_D(TAG, "No change in value");
    }

    return true;
}

// flag to check all status
int status_flag = 0; // 0th bit for igns, 1st bit for imu, 2nd bit for sc, 3rd bit for gps

void set_status_flag(motion_status_t status, int pos) {
    if(status == MOVING) {
        // Set the bit
        status_flag |= (1 << pos);
    } else {
        // Reset the bit
        status_flag &= ~(1 << pos);
    }
}

void APM::set_wom_thresholds(unsigned int wom_x_thres,unsigned int wom_y_thres,unsigned int wom_z_thres) {
    wom_x_threshold = wom_x_thres;
    wom_y_threshold = wom_y_thres;
    wom_z_threshold = wom_z_thres;
    return;
}

void APM::get_wom_thresholds(unsigned int& wom_x_thres, unsigned int& wom_y_thres, unsigned int& wom_z_thres) {
    wom_x_thres = wom_x_threshold;
    wom_y_thres = wom_y_threshold;
    wom_z_thres = wom_z_threshold;
    return;
}

void APM::set_veh_class(VehicleClass& vehi_class) {
    veh_class = vehi_class;
    return;
}

VehicleClass APM::get_veh_class() {
    return veh_class;
}

/*
 * Func name: APM::start_monitor()
 * function track bit mask set reset by imu, igns & SC
 * Any worker can say STATIONARY if there is any error in analysing the data also
 */


void APM::start_monitor() {
    uint64_t start_time = get_system_monotonic_time();
    motion_status_t igns_status = STATIONARY;
    motion_status_t imu_status = STATIONARY;
    motion_status_t gps_status = STATIONARY;
    motion_status_t can_status = STATIONARY;
    motion_status_t power_volt_status = STATIONARY;
    motion_status_t ign_volt_status = STATIONARY;
    motion_status_t sc_status = MOVING;
    motion_status_t aggregate_status = TRANSITION;

    static int64_t target_keep_alive_time = 0;

    // Writing the default ignition status for cases where default pseudo ignition entry from kernel is always 0

    ignition_status_t init_ignition_status = IGNITION_ON;
    if(read_from_sysfs(&init_ignition_status) == false) {
        LOG_E(TAG, "Could not read the initial ignition status");
        // In case read fails, set to OFF
        // To avoid false wakeup ignition on scenarios
        init_ignition_status = IGNITION_OFF;
    }

    IGNS_worker *cl_w = static_cast<IGNS_worker*>(APM::WorkerInstance(eCrankLine));
    IMU_worker *imu_w = static_cast<IMU_worker*>(APM::WorkerInstance(eInertial));
    SC_worker *sc_w = static_cast<SC_worker*>(APM::WorkerInstance(ePowerFail));
    GPS_worker *gps_w = static_cast<GPS_worker*>(APM::WorkerInstance(eSatellite));
    CAN_worker *can_w = static_cast<CAN_worker*>(APM::WorkerInstance(eCANBus));
    POWER_VOLT_worker *volt_w = static_cast<POWER_VOLT_worker*>(APM::WorkerInstance(eCrankVolt));

    SrcStatus ign_src =  {

        .pseudo_ignition_status = STATIONARY,

        .ign_sensor_enable = ( cl_w && !(cl_w->sensor_decision_disabled) ),
        .ign_line_status = STATIONARY,
        .ign_sensor_status = ( cl_w ? cl_w->getMotionState() : STATIONARY ),

        .power_volt_sensor_enable = ( volt_w && !(volt_w->sensor_decision_disabled) ),
        .power_volt_status = STATIONARY,
        .power_volt_sensor_status = ( volt_w ? volt_w->getMotionState() : STATIONARY ),

        .imu_sensor_enable = ( imu_w && !(imu_w->sensor_decision_disabled) ),
        .imu_status = STATIONARY,
        .imu_sensor_status = ( imu_w ? imu_w->getMotionState() : STATIONARY ),

        .gps_sensor_enable = ( gps_w && !(gps_w->sensor_decision_disabled) ),
        .gps_status = STATIONARY,
        .gps_sensor_status = ( gps_w ? gps_w->getMotionState() : STATIONARY ),

        .can_sensor_enable = ( can_w && !(can_w->sensor_decision_disabled) ),
        .can_status = STATIONARY,
        .can_sensor_status = ( can_w ? can_w->getMotionState() : STATIONARY ),

        .sc_sensor_enable = ( sc_w && !(sc_w->sensor_decision_disabled) ),
        .sc_status = MOVING,
        .sc_sensor_status = ( sc_w ? sc_w->getMotionState() : MOVING )
    };

    if(cl_w && cl_w->enabled == false){
        init_ignition_status = IGNITION_OFF; // In case ignition disabled, override physical ignition status to OFF
    }

    //if(init_ignition_status == IGNITION_OFF)
    {
        /* IGN is INTERRUPT Based trigger.
         * If device wakeup due to IGNITION toggle, then we have to update the pseudo ignition status to IGNITION_ON but actual ignition status is 0.
         * In the above case if there is no vehicle movement,(no delta in imu and gps) vehicle continues to be in IGNITION_ON,
         * as there no event from the ignition line and uses up battery.
         * So we have to update this event before we start monitoring for proper ignition status update.
         */
	    if( true == (cl_w && cl_w->enabled) ) {
		    ign_src.ign_line_status = cl_w->read_status();
		    if(MOVING == ign_src.ign_line_status) {
		        init_ignition_status = IGNITION_ON;
			    //Set IGNS bit
			    set_bit(IGNS_MASK_POS);

		    }else {
			    //Reset IGNS bit
			    reset_bit(IGNS_MASK_POS);
		    }
	    }

        if(true == (volt_w && volt_w->enabled)) {
            ign_src.power_volt_status = volt_w->read_status();
            if(MOVING == ign_src.power_volt_status){
                init_ignition_status = IGNITION_ON;
                //Set POWER_VOLT bit
                set_bit(POWER_VOLT_MASK_POS);
            }else{
                //Reset POWER_VOLT bit
                reset_bit(POWER_VOLT_MASK_POS);
            }
        }

        LOG_C(TAG, "Initial ignition status of IMU WOM is %d, IGN WAK is %d, CRANK_VOLT WAK is %d",
                ( pow_on_off_reason & ( 1 << PowerOnTriggerT::WAKEonMOTION_IMU ) ), ( pow_on_off_reason & ( 1 << PowerOnTriggerT::WAKEonIGNITION ) ),
                ( pow_on_off_reason & ( 1 << PowerOnTriggerT::WAKEonCRANK_VOLT ) ) );

	    if(true == apm_ign_wake_enable) {
            if(pow_on_off_reason & (1 << PowerOnTriggerT::WAKEonIGNITION)) {
                init_ignition_status = IGNITION_ON;
                if((true == glitch_suppression_support)) {
                    init_ignition_status = ((MOVING == igns_status) || (MOVING == power_volt_status)) ? IGNITION_ON : IGNITION_OFF;
                    LOG_C(TAG, "Wake On IGN Glitch detected, igns_status: %d, setting initial ignition status to %s", igns_status,
                    (IGNITION_ON == init_ignition_status) ? "IGNITION_ON" : "IGNITION_OFF");
                }
            }
	    }
        else {
            if(pow_on_off_reason & (1 << PowerOnTriggerT::WAKEonIGNITION)) {
                string err_msg = "apm_ign_wake_enable : false. WAKE on IGNITION occurred";
                LOG_C(TAG, "%s", err_msg.c_str());
                nd_service_obj->send_err_msg(SM_E_APM_EVENT_STATUS, (int)PowerOnTriggerT::WAKEonIGNITION, err_msg);
            }
        }

	    if(false == write_to_sysfs(init_ignition_status)) {
		    LOG_E(TAG, "write to sysfs failed");
	    }
    }

    LOG_I(TAG, "Initial ignition status is %d", init_ignition_status);

    // Set clock to monotonic
    pthread_condattr_init(&m_attr);
    pthread_condattr_setclock(&m_attr, CLOCK_MONOTONIC);
    pthread_cond_init(&m_cond, &m_attr);

    struct timespec ts = {0};
    int prev_status_flag = -1;

    {
        std::stringstream msg_src("");
        msg_src << "IGN: " << ( (true == ign_src.ign_sensor_enable) ? "E" : "D" ) << ( (MOVING == ign_src.ign_sensor_status) ? " ON" : " OFF" )
        << ", IMU: " << ( (true == ign_src.imu_sensor_enable) ? "E" : "D" ) << ( (MOVING == ign_src.imu_sensor_status) ? " ON" : " OFF" )
        << ", GPS: " << ( (true == ign_src.gps_sensor_enable) ? "E" : "D" ) << ( (MOVING == ign_src.gps_sensor_status) ? " ON" : " OFF" )
        << ", SCAP: " << ( (true == ign_src.sc_sensor_enable) ? "E" : "D" ) << ( (MOVING == ign_src.sc_sensor_status) ? " ON" : " OFF" )
        << ", CAN: " << ( (true == ign_src.can_sensor_enable) ? "E" : "D" ) << ( (MOVING == ign_src.can_sensor_status) ? " ON" : " OFF" )
        << ", CRANK_VOLT: " << ( (true == ign_src.power_volt_sensor_enable) ? "E" : "D" ) << ( (MOVING == ign_src.power_volt_sensor_status) ? " ON" : " OFF" );

        LOG_I(TAG, "Initial Sensor Status: %s", msg_src.str().c_str());
        nd_service_obj->send_err_msg(SM_E_APM_EVENT_STATUS, 0, msg_src.str());
    }

    while(1) {
        // LOG_D(TAG, "BIT_MASK : %d",bit_mask);

        pthread_mutex_lock(&m_mutex);

        if ( FAILURE == clock_gettime(CLOCK_MONOTONIC, &ts) ) {
            LOG_E(TAG, "clock_gettime failed, setting the wait time to 0 so that pthread_cond_timedwait will unblock immediately");
            // set time to 0, so pthread_cond_timedwait will unblock immediately
            ts.tv_sec = 0;
            ts.tv_nsec = 0;
        }
        else {
            ts.tv_sec += COND_VAR_TIMEOUT;
        }

        if (ETIMEDOUT == pthread_cond_timedwait(&m_cond, &m_mutex, &ts)) {
            // LOG_D(TAG, "No change in bit mask in last %d seconds", COND_VAR_TIMEOUT);
        }
        pthread_mutex_unlock(&m_mutex);

        // Send keep alive to SVC every 30 seconds
        if(target_keep_alive_time <= get_system_monotonic_time()) {
            // send keepalive to SVC
            std::lock_guard<std::mutex> lk(keepalive_status_mutex);
            LOG_I(TAG, "start_monitor_fn all_thread_keepalive_status:  %u", (int)all_thread_keepalive_status);
            if( !svc_util_send_keepalive((int)all_thread_keepalive_status) ) {
                LOG_E(TAG, "Something went wrong in sending keepalive to SVC");
            }
            all_thread_keepalive_status = all_thread_enable_status; // initialize bit to 1 for all enabled threads
            //increase time limit
            target_keep_alive_time = get_system_monotonic_time() + (SVC_SEND_KEEP_ALIVE_TIME*MULTIPLY_BY_THOUSAND);

        }

        // No worker triggered the CV and not in transition state
        if((ready == false) && (aggregate_status != TRANSITION)) {
            // LOG_D(TAG, "No notification received");

            continue;
        }

        ready = false;

        // notification is triggered or in TRANSITION state
        LOG_I(TAG, "notification received or in transition state, ready %d, mask = 0x%02x, state = %d",  ready, bit_mask, aggregate_status);

        // IMU, GPS and CAN are part of motion detection
        if(true == (imu_w && imu_w->enabled)) {
            ign_src.imu_status = imu_w->read_status();
            set_status_flag(ign_src.imu_status, IMU_MASK_POS);
        }

        if(true == (gps_w && gps_w->enabled)) {
            ign_src.gps_status = gps_w->read_status();
            set_status_flag(ign_src.gps_status, GPS_MASK_POS);
        }

        if(true == (can_w && can_w->enabled)) {
            ign_src.can_status = can_w->read_status();
            set_status_flag(ign_src.can_status, CAN_MASK_POS);
        }

        // Super Cap has higher priority than IGN line and Crank Voltage
        if(true == (sc_w && sc_w->enabled)) {
            ign_src.sc_status = sc_w->read_status();
            if (MOVING == ign_src.sc_status) {
                // If SC is MOVING, then reset the bit as it is battery powered
                set_status_flag(STATIONARY, SC_MASK_POS);
            } else {
                // If SC is STATIONARY, then set the bit as it is super cap powered
                set_status_flag(MOVING, SC_MASK_POS);
            }
        }

        {
            ignition_status_t ignition_status = IGNITION_OFF;

            ign_src.ign_line_status = (cl_w && cl_w->enabled) ? cl_w->read_status() : STATIONARY;
            ign_src.power_volt_status = (volt_w && volt_w->enabled) ? volt_w->read_status() : STATIONARY;

            set_status_flag(ign_src.ign_line_status, IGNS_MASK_POS);
            set_status_flag(ign_src.power_volt_status, POWER_VOLT_MASK_POS);

            if((MOVING == ign_src.ign_line_status) || (MOVING == ign_src.power_volt_status)) {
                ignition_status = IGNITION_ON;
            }

            // send critical info for all events
            if (prev_status_flag != status_flag) {

                stringstream msg_src("");
                msg_src << ( (MOVING == ign_src.pseudo_ignition_status) ? "ON " : "OFF " )
                << ", IGN: " << ( (status_flag & (1 << IGNS_MASK_POS)) ? "ON" : "OFF" )
                << ", IMU: " << ( (status_flag & (1 << IMU_MASK_POS)) ? "ON" : "OFF" )
                << ", GPS: " << ( (status_flag & (1 << GPS_MASK_POS)) ? "ON" : "OFF" )
                << ", SCAP: " << ( (status_flag & (1 << SC_MASK_POS)) ? "ON" : "OFF" )
                << ", CAN: " << ( (status_flag & (1 << CAN_MASK_POS)) ? "ON" : "OFF" )
                << ", CRANK: " << (  (status_flag & (1 << POWER_VOLT_MASK_POS)) ? "ON" : "OFF" );

                // Store status flag into sysfs for so power monitor can read it and send it to health stat
                if(false == push_data_to_sysfs(PowermonParam::eIGN_SRC_STAT, status_flag)) {
                    LOG_E(TAG, "Failed to push ignition source status to sysfs");
                }

                LOG_I(TAG, "%s", msg_src.str().c_str());
                nd_service_obj->send_err_msg(SM_E_APM_EVENT_STATUS, status_flag, msg_src.str());
                LOG_I(TAG, "prev_status_flag = %d, status_flag = %d", prev_status_flag, status_flag);
                prev_status_flag = status_flag;
            }

            // This has been moved above to enusre that for bagheera2, we give supercap priority
            // If super cap is active, then we should not write pseudo ignition OFF
            // This was added as there is no status from the gpio for bagheera2 PFI ... It is an interrupt from ADC
            //Write pseudo ignition OFF immediately if super cap is active
            // if(sc_status == STATIONARY) {
                // aggregate_status = STATIONARY;
                // if(false == write_to_sysfs(IGNITION_OFF)) {
                //     LOG_E(TAG, "write to sysfs failed");
                // }
                // continue;
                // LOG_C(TAG, "Supercap is active, not pulling down ignition status");
            // }

            // Make sure to immediately write the ignition status if motion detection feature is disabled
            // If motion detection is disabled, and none of non-motion sources are enabled, consider motion_detection is enabled, and go for transition state. Otherwsie wirte the ignition status as per non-motion sources
            if((false == apm_motion_detection_enable) && ( ( true == (cl_w && cl_w->enabled) ) || ( true == (volt_w && volt_w->enabled) ) ) ) {
                LOG_I(TAG, "imu_status = %d, gps_status = %d, igns_status = %d, sc_status = %d, can_status = %d, crank_status = %d, aggregate_status = %d", ign_src.imu_status, ign_src.gps_status, ign_src.ign_line_status, ign_src.sc_status, ign_src.can_status, ign_src.power_volt_status, aggregate_status);
                if(false == write_to_sysfs(ignition_status)) {
                    LOG_E(TAG, "write to sysfs failed");
                }
                continue;
            }
        }

        //Write pseudo ignition OFF immediately if super cap is active
        // if(sc_status == STATIONARY) {
            // aggregate_status = STATIONARY;
            // if(false == write_to_sysfs(IGNITION_OFF)) {
            //     LOG_E(TAG, "write to sysfs failed");
            // }
            // continue;
        // }

        // Check if all the workers say STATIONARY
        if( (STATIONARY == ign_src.ign_line_status)
            && (STATIONARY == ign_src.power_volt_status)
            && (STATIONARY == ign_src.gps_status)
            && (STATIONARY == ign_src.imu_status)
            && (STATIONARY == ign_src.can_status) ) {

            // If first time condition hit for all workers stationary, move to TRANSITION state
            if(aggregate_status == MOVING) {
                start_time = get_system_monotonic_time();
                LOG_I(TAG, "imu_status = %d, gps_status = %d, igns_status = %d, sc_status = %d, can_status = %d, crank_status = %d, aggregate_status = %d, TRANSITION STATE Start Time %lld", ign_src.imu_status, ign_src.gps_status, ign_src.ign_line_status, ign_src.sc_status, ign_src.can_status, ign_src.power_volt_status, aggregate_status, start_time);
                aggregate_status = TRANSITION;
                continue;
            }

            LOG_I(TAG, "imu_status = %d, gps_status = %d, igns_status = %d, sc_status = %d, can_status = %d, crank_status = %d, aggregate_status = %d, TRANSITION STATE Elapsed Time %lld", ign_src.imu_status, ign_src.gps_status, ign_src.ign_line_status, ign_src.sc_status, ign_src.can_status, ign_src.power_volt_status, aggregate_status, (get_system_monotonic_time() - start_time));
            /* Come here when in TRANSITION state.
             * Check if spent enough time in transition,
             * move to STATIONARY state and write pseudo igntion to 0
             */
            if(get_system_monotonic_time() > (start_time + vehicle_idle_time*1000)) {
                aggregate_status = STATIONARY;
                if(false == write_to_sysfs(IGNITION_OFF)) {
                    LOG_E(TAG, "write to sysfs failed");
                }
            }
            continue;
        }

        // Come here when some worker triggered and when we have to move to MOVING state
        aggregate_status = MOVING;
        if(false == write_to_sysfs(IGNITION_ON)) {
            LOG_E(TAG, "write to sysfs failed");
        }
    }
}


/*
 * Func name: APM::apm_register()
 * function is to register worker such as imu, igns & SC
 * returns 0 on success and -1 on failure
 */
int APM::apm_register(char *tag, APM_worker *apm_worker, IgnitonSource ignSrc) {

	if(NULL == apm_worker) {
		LOG_E(TAG, "Didn't recived apm_worker");
	}
	LOG_I(TAG, " apm register called for : %s, %d",tag, ignSrc);

 
	if( (ignSrc >= ePowerFail) && (ignSrc < eIgnSrcMax) ){
		APM::pWorkers[ignSrc] = apm_worker;
    }

    // std::thread t1 = thread(&APM_worker::poll_func, apm_worker);
    // std::thread t2 = thread(&APM_worker::intr_func, apm_worker);

    // if(eEngineStatus == ignSrc) {
    //     eng_stat_poll_thread = &t1;
    //     eng_stat_intr_thread = &t2;
    // }

    // poll_thread_ptr.push_back(move(t1));
    // intr_thread_ptr.push_back(move(t2));

	poll_thread_ptr.push_back(thread(&APM_worker::poll_func, apm_worker));
	intr_thread_ptr.push_back(thread(&APM_worker::intr_func, apm_worker));

    return 0;
}

void APM::apm_unregister_all() {
	int worker_size = poll_thread_ptr.size();
	for(int i=0; i<worker_size; i++) {
		poll_thread_ptr[i].join();
		intr_thread_ptr[i].join();
	}
}
