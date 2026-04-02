/* Copyright (C) 2020 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Devendra Yadav <devendra.yadav@netradyne.com>
 */

#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <config_parser.h>
#include <string>
#include <sys/time.h>
#include <deque>
#include "ndmb/nd_msg_interface.h"
#include "ndmb/nd_mbclient.h"
#include "apm.h"
#include "apm_gps.h"
#include "apm_worker.h"
#include "nd_time.h"
#include "system_utils.h"

#define TAG "A_GPS"

#define nd_device_obj (ND_DeviceFactory::Create_NDDevice())

extern NDService *nd_service_obj;
extern int all_thread_keepalive_status;
extern float gps_accuracy;
extern float gps_speed_threshold;
// These thresholds are defined as double in apm_main but here it was float, changing to double for consistency
extern double gps_latitude_threshold;
extern double gps_longitude_threshold;

pthread_condattr_t gps_data_attr;
pthread_cond_t gps_data_cv = PTHREAD_COND_INITIALIZER;
pthread_mutex_t gps_data_lock = PTHREAD_MUTEX_INITIALIZER;
static const int32_t COND_VAR_TIMEOUT = 3;



/*Average of sum of absolute difference in chunks of 10 seconds for 200 seconds
 * Hence, the columns will be 20
 * The SAD average should be calculated for 3 params(lat, long, speed). Hence, the rows would be 3
 */
static const int NUM_SAD_CHUNKS = 20;
static const int NUM_PARAM = eGPS_ParamMax;//SPEED, LAT, LONG
static double SAD_avg[NUM_PARAM][NUM_SAD_CHUNKS] = {0.0};
static int sad_idx = 0; // index for SAD columns



// Interval at which we want to evaluate the motion status 
static const int motion_detect_interval = 10;
// Initial delay to start detecting the motion status
static const int STATIONARY_DETECT_DELAY = NUM_SAD_CHUNKS * motion_detect_interval * 1000; // 200 seconds delay
// Maximum time the GPS data is not available continuously
static const int MAX_DATA_OUTAGE_TIME = 60 * 1000; // 60 seconds
// Maximum GPS zero speed time to detect IDLE
static const int MAX_GPS_ZERO_SPEED_TIME = 200 * 1000; // 200 seconds
// Maximum GPS non zero speed time to detect MOVING
static const int MAX_GPS_NON_ZERO_SPEED_TIME = 10 * 1000; // 10 seconds

using namespace std;
static std::atomic<motion_status_t> g_vehicle_state(STATIONARY);
static std::atomic<bool> data_outage(false);
static apm_gps_data_t prev_data, curr_data;

// Flip flop queues. When testing on one queue, other queue gets updated
static deque<apm_gps_data_t> gps_data_q[2];
static std::atomic<bool> flipflop(true);

static deque<apm_gps_data_t> gps_bin_data_q[2];
static std::atomic<bool> flipflop_bin_data(true);


constexpr float MIN_GPS_SPEED = 0.0;   // 0 mph
constexpr float MAX_GPS_SPEED = 200.0; // 200 mph

// GPS available time
#if 0
static int64_t latest_non_zero_speed_time = get_system_monotonic_time();
static int64_t latest_zero_speed_time = get_system_monotonic_time();

static int zero_speed_count = 0, non_zero_speed_count = 0;
static const int CONSECUTIVE_ZERO_SPEED_COUNT = 3;
static const int CONSECUTIVE_NON_ZERO_SPEED_COUNT = 3;
#endif
/*
 * Func name: msg_cb()
 * function update gps data published by nd-central
 * returns true on success and false on failure
 */
static bool msg_cb(ndmb_generic_msg_t *msg) {
    if(msg == NULL) {
        LOG_E(TAG, "The NDMB message for GPS is NULL");
        return false;
    }

    ndmbmsg_apm_gps_data_t *ptr1=NULL;
    ptr1 = reinterpret_cast<ndmbmsg_apm_gps_data_t *>( msg );
    if( msg->topic != TOPIC_APM_GPS_DATA ) {
        LOG_I(TAG, "****Unkown topic: ->%s",msg->topic);
	    return false;
    }
    pthread_mutex_lock(&gps_data_lock);
    bool ret = memcpy(&curr_data,&ptr1->gps_data_ptr, sizeof(apm_gps_data_t));
    if (false == ret) {
        LOG_E(TAG,"Memory copy failed for gps_data_ptr with return:%d",ret);
        pthread_mutex_unlock(&gps_data_lock);
        return false;
    }
    pthread_cond_signal(&gps_data_cv);
    pthread_mutex_unlock(&gps_data_lock);

    gps_data_q[flipflop.load()].push_front(curr_data);
    gps_bin_data_q[flipflop_bin_data.load()].push_front(curr_data);
    data_outage = false;
    return true;
}

/*
 * Func name: GPS_worker::filter_func()
 * Check the GP status and change the motion status
 */
void GPS_worker::filter_func ()
{

    if(false == use_legacy) {
        return;
    }

    static double gps_threshold[NUM_PARAM] = {gps_speed_threshold, gps_latitude_threshold, gps_longitude_threshold};
    gps_threshold[eGPS_Speed] = gps_speed_threshold;
    gps_threshold[eGPS_Latitude] = gps_latitude_threshold;
    gps_threshold[eGPS_Longitude] = gps_longitude_threshold;

    bool above_threshold = false;

    //If data_outage is true, move to STATIONARY
    if(true == data_outage) {
        if( MOVING == g_vehicle_state ) {
            LOG_I(TAG,"Vehicle is moving and data_outage == true");
            g_vehicle_state = STATIONARY;
            reset_bit(GPS_MASK_POS);
        }
        return;
    }


    // If already in IDLE, check the latest 10 seconds data to check if device is MOVING
    if(g_vehicle_state == STATIONARY) {
        // If (LAT or LONG) and SPEED SAD Average for last 10 seconds are above threshold then we move to MOVING
        // If (LAT or LONG) for movement along latitude or longitude where SAD_avg along the other line will be zero.
        if( (SAD_avg[eGPS_Speed][sad_idx] > gps_threshold[eGPS_Speed]) &&
                ( (SAD_avg[eGPS_Latitude][sad_idx] > gps_threshold[eGPS_Latitude]) ||
                  (SAD_avg[eGPS_Longitude][sad_idx] > gps_threshold[eGPS_Longitude]) ) ){

                g_vehicle_state = MOVING;
                set_bit(GPS_MASK_POS);
            }
            return;
    }
    // We come here when we are in MOVING state
    // SAD check for all the intervals of data
    for(int j = 0; j < NUM_SAD_CHUNKS; j++) {
        // SAD_avg check for all the params for last 10 seconds(Speed and Lat or Long should all collectively be above/below their respective threshold for state change decision)
        if( (SAD_avg[eGPS_Speed][j] > gps_threshold[eGPS_Speed]) &&
                ( (SAD_avg[eGPS_Latitude][j] > gps_threshold[eGPS_Latitude]) ||
                  (SAD_avg[eGPS_Longitude][j] > gps_threshold[eGPS_Longitude]) ) ) {
            above_threshold  = true;
        }

        if(above_threshold == true) {
            break;
        }
    }

    if(above_threshold == false) {
        g_vehicle_state = STATIONARY;
        reset_bit(GPS_MASK_POS);
    } else {
        g_vehicle_state = MOVING;
        set_bit(GPS_MASK_POS);
    }

    return;
}

static void detect_motion_gps() {

    apm_gps_data_t data;
    while(1) {

        if ( (nullptr != APM::WorkerInstance(eSatellite)) && (true == APM::WorkerInstance(eSatellite)->use_legacy) ) {
            all_thread_keepalive_status &= ~(1 << GPS_MASK_POS);
        }
        sleep(motion_detect_interval);

        // Switch the queue to capture data from NDC
        flipflop.store(!flipflop.load());
        // Take filled queue for analysis
        int queue_size = (gps_data_q[!flipflop.load()]).size();

        LOG_I(TAG, "queue_size = %d", queue_size);
        // init the SAD for sad_idx
        for(int i = 0; i < NUM_PARAM; i++) {
            SAD_avg[i][sad_idx] = 0.0;
        }
        if(queue_size == 0) {
            LOG_I(TAG, "No data from GPS");
            continue;
        }

        bool first_time = true;
        while (!gps_data_q[!flipflop.load()].empty()) {
            data = gps_data_q[!flipflop.load()].front();
            gps_data_q[!flipflop.load()].pop_front();

            LOG_I(TAG, "valid = %d, accuracy = %f, speed = %f, lattitude = %.10lf, longitude = %.10lf ", data.valid, data.accuracy, data.speed, data.latitude, data.longitude);

            //If Only the GPS Data os Valid and GPS Accuracy is within the configured thresholds, we consider the GPS Data for SAD_Avg else we ignore the data.
            if((true == data.valid) && (data.accuracy < gps_accuracy )) {

                if ((data.speed >= MIN_GPS_SPEED) && (data.speed <= MAX_GPS_SPEED)) {
                    SAD_avg[eGPS_Speed    ][sad_idx] += (static_cast<double>(data.speed));
                }

                if(first_time == false) {
                    SAD_avg[eGPS_Latitude ][sad_idx] += fabs(data.latitude - prev_data.latitude);
                    SAD_avg[eGPS_Longitude][sad_idx] += fabs(data.longitude - prev_data.longitude);
                }
            }
            prev_data = data;
            first_time = false;
        }

        // calculate average
        for(int i = 0; i < NUM_PARAM; i++) {
            LOG_I(TAG, "* sad_avg = %.10lf, sad_idx = %d", SAD_avg[i][sad_idx], sad_idx);
            SAD_avg[i][sad_idx] = SAD_avg[i][sad_idx] / queue_size;
            LOG_I(TAG, "sad_avg = %.10lf, sad_idx = %d", SAD_avg[i][sad_idx], sad_idx);
        }

        // call filter function on data
        APM_worker* gps_worker = APM::WorkerInstance(eSatellite);
        if(gps_worker != NULL) {
            gps_worker->filter_func();
        } else {
            LOG_E(TAG, "GPS worker not initialized yet, skipping filter_func call");
        }

        sad_idx = (sad_idx + 1) % NUM_SAD_CHUNKS;
    }
}

/*
 * Func name: GPS_worker::poll_func()
 * dummy function
 * returns NULL on success and APM_ERROR on failure
 */
void* GPS_worker::poll_func() {
	LOG_I(TAG, "Dummy function do nothing here");
    detect_engine_status();
    return NULL;
}


/*
 * Func name: GPS_worker::intr_func()
 * function subscribes to the gps data from nd-central
 * returns NULL on success and APM_ERROR on failure
 */
void* GPS_worker::intr_func() {
    std::string s = "NDMB_APM_SERVICE";
    NDMBClient msg_client(s);
    bool ret = msg_client.subscribe(TOPIC_APM_GPS_DATA, msg_cb);
    if(false == ret)
    {
        LOG_E(TAG,"msg_client.subscribe failed");
        nd_service_obj->send_err_msg(SM_E_APM_SUBS_FAIL, 0, "GPS subscriber failed");
        g_vehicle_state = STATIONARY;
	    return APM_ERROR;
    }

    thread t1 (detect_motion_gps);
    int64_t latest_data_timestamp = get_system_monotonic_time();

    pthread_condattr_init(&gps_data_attr);
    pthread_condattr_setclock(&gps_data_attr, CLOCK_MONOTONIC);
    pthread_cond_init(&gps_data_cv, &gps_data_attr);

    struct timespec timeToWait = {0};

    while(1) {

        if(get_system_monotonic_time() > (latest_data_timestamp + MAX_DATA_OUTAGE_TIME)) {
            if(false == data_outage.load()){
                LOG_I(TAG, "Data outage timeout");
                data_outage = true;
                g_vehicle_state = STATIONARY;
                reset_bit(GPS_MASK_POS);
            }
        }

        pthread_mutex_lock(&gps_data_lock);
        if ( FAILURE == clock_gettime(CLOCK_MONOTONIC, &timeToWait) ) {
            LOG_E(TAG, "clock_gettime failed, setting the wait time to 0 so that pthread_cond_timedwait will unblock immediately");
            // set time to 0, so pthread_cond_timedwait will unblock immediately
            timeToWait.tv_sec = 0;
            timeToWait.tv_nsec = 0;
        }
        else {
            timeToWait.tv_sec += COND_VAR_TIMEOUT;
        }

        if (ETIMEDOUT == pthread_cond_timedwait (&gps_data_cv, &gps_data_lock, &timeToWait)) {
            if(true == data_outage.load()){
                LOG_W (TAG, "CV Timeout when trying to Memory copy for gps_data_ptr.");
            }
        }
        else {
            latest_data_timestamp = get_system_monotonic_time();
        }
        pthread_mutex_unlock(&gps_data_lock);
    }

    if(t1.joinable()){
        t1.join();
    }

    return NULL;
}

/*
 * Func name: GPS_worker::read_status()
 * function returns the motion status from GPS
 */
motion_status_t GPS_worker::read_status() {

    if((false == enabled) || (true == sensor_decision_disabled)) {
        LOG_D(TAG, "GPS worker(%d), sensor_decision_disabled(%d). Returning STATIONARY", enabled, sensor_decision_disabled);
        return STATIONARY;
    }

    string gps_ign_sysfs_path = nd_device_obj->get_gps_ign_sysfs_path();

    if(false == APM::WorkerInstance(eSatellite)->use_legacy){

        motion_status_t status =  STATIONARY;

        if(false == init_state_read) {
            status = getInitialState();
            init_state_read = true;
        }
        else {
            status = getMotionState();
        }

        if (STATIONARY == status) {
            write_into_sysfs_entry(gps_ign_sysfs_path, IGNITION_OFF);
        }
        else if (MOVING == status) {
            write_into_sysfs_entry(gps_ign_sysfs_path, IGNITION_ON);
        }
        else {
            LOG_E(TAG, "Unknown motion state : %d", status);
            write_into_sysfs_entry(gps_ign_sysfs_path, IGNITION_OFF);
        }
        return status;
    }

    if (STATIONARY == g_vehicle_state) {
        write_into_sysfs_entry(gps_ign_sysfs_path, IGNITION_OFF);
    }
    else if (MOVING == g_vehicle_state) {
        write_into_sysfs_entry(gps_ign_sysfs_path, IGNITION_ON);
    }
    else {
        LOG_E(TAG, "Unknown motion state : %d", g_vehicle_state.load());
        write_into_sysfs_entry(gps_ign_sysfs_path, IGNITION_OFF);
    }
    return g_vehicle_state;
}


void GPS_worker::insert_bin_data() {
    if (false == use_legacy) {
        all_thread_keepalive_status &= ~(1 << GPS_MASK_POS);
    }

    // Switch the queue to capture data from NDC
    flipflop_bin_data.store(!flipflop_bin_data.load());
    // Take filled queue for analysis
    int queue_size = (gps_bin_data_q[!flipflop_bin_data.load()]).size();

    // In case of size is 0, then insert invalid data
    GpsData gps_bin_data;
    gps_bin_data.valid = false;
    gps_bin_data.accuracy = static_cast<float>(SysfsErrorCodes::eSYSFS_INVALID_DATA);
    gps_bin_data.param[eGPS_Speed] = static_cast<float>(SysfsErrorCodes::eSYSFS_INVALID_DATA);
    gps_bin_data.param[eGPS_Latitude] = DEF_LAT_INVALID;
    gps_bin_data.param[eGPS_Longitude] = DEF_LONG_INVALID;
    apm_gps_data_t gps_data;
    while (!gps_bin_data_q[!flipflop_bin_data.load()].empty()) {
        gps_data = gps_bin_data_q[!flipflop_bin_data.load()].front();
        gps_bin_data_q[!flipflop_bin_data.load()].pop_front();

        if(true == gps_data.valid){
            gps_bin_data.valid = true;
            gps_bin_data.accuracy = gps_data.accuracy;
            gps_bin_data.param[eGPS_Speed] = gps_data.speed;
            gps_bin_data.param[eGPS_Latitude] = gps_data.latitude;
            gps_bin_data.param[eGPS_Longitude] = gps_data.longitude;
        }else{
            gps_bin_data.valid = false;
            gps_bin_data.accuracy = static_cast<float>(SysfsErrorCodes::eSYSFS_INVALID_DATA);
            gps_bin_data.param[eGPS_Speed] = static_cast<float>(SysfsErrorCodes::eSYSFS_INVALID_DATA);
            gps_bin_data.param[eGPS_Latitude] = DEF_LAT_INVALID;
            gps_bin_data.param[eGPS_Longitude] = DEF_LONG_INVALID;
        }

        // LOG_D(TAG, "Bin Data valid = %d, accuracy = %f, speed = %f, lattitude = %.10lf, longitude = %.10lf ", gps_bin_data.valid, gps_bin_data.accuracy, gps_bin_data.param[eGPS_Speed], gps_bin_data.param[eGPS_Latitude], gps_bin_data.param[eGPS_Longitude] );
    }

    if((queue_size != 0) && (true == gps_bin_data.valid)){
        push_data_to_sysfs(eGPS_VALID,0); // 0 will increment the generator count
    }

    getWorkerBinDataObj()->set_bin_data(get_index(), (void*) &gps_bin_data);

    incr_index();

    return ;
}
