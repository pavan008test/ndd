/* Copyright (C) 2019 - 2020 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Devendra Yadav <devendra.yadav@netradyne.com>
 */

#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <config_parser.h>
#include <string>
#include <sys/time.h>
#include <deque>
#include <atomic>
#include "ndmb/nd_msg_interface.h"
#include "ndmb/nd_mbclient.h"
#include "apm.h"
#include "apm_imu.h"
#include "apm_worker.h"
#include "nd_time.h"
#include "system_utils.h"
//#include <nd_msp_utils.h>

#define TAG "A_IMU"

#define nd_device_obj (ND_DeviceFactory::Create_NDDevice())

extern NDService *nd_service_obj;
extern int all_thread_keepalive_status;
pthread_condattr_t imu_data_attr;
pthread_cond_t imu_data_cv = PTHREAD_COND_INITIALIZER;
pthread_mutex_t imu_data_lock = PTHREAD_MUTEX_INITIALIZER;
extern float imu_threshold;
static const int32_t COND_VAR_TIMEOUT = 3;
int32_t imu_data_outage_count = 0;
// Interval at which we want to evaluate the motion status 
static const int motion_detect_interval = 10;
// Maximum time the IMU data is not available continuously
static const int MAX_DATA_OUTAGE_TIME = 60 * 1000; // 60 seconds

using namespace std;
static std::atomic<motion_status_t> g_vehicle_state(STATIONARY);
static apm_imu_data_t prev_data, curr_data;

// Flip flop queues. When testing on one queue, other queue gets updated
static deque<apm_imu_data_t> imu_data_q[2];
static std::atomic<bool> flipflop(true);
// Flip flop queues. When testing on one queue, other queue gets updated
static deque<apm_imu_data_t> imu_bin_data_q[2];
static std::atomic<bool> flipflop_bin_data(true);
/*Average of sum of absolute difference in chunks of 10 seconds for 200 seconds
 * Hence, the columns will be 20
 * The SAD average should be calculated for 6 axes. Hence, the rows would be 6
 */
static const int NUM_SAD_CHUNKS = 20;
static const int NUM_AXES = 6;
static float SAD_avg[NUM_AXES][NUM_SAD_CHUNKS] = {0.0};
static int sad_idx = 0; // index for SAD columns

constexpr float sampling_rate = 20.0f; // 50 Hz
// Initial delay to start detecting the motion status
// By this time, all the Queues will be filled with IMU data
static const int STATIONARY_DETECT_DELAY = NUM_SAD_CHUNKS * motion_detect_interval * 1000; // 200 seconds delay

NDDeviceTypeT device_type = ND_DeviceFactory::getBuildDeviceType();
unsigned int g_wom_x_thr, g_wom_y_thr, g_wom_z_thr;
/*Adaptive WOM thresholding*/
extern int adaptive_wom_trigger_count;

struct adaptive_wom_axis_data_t {

    std::string status_sys;
    std::string threshold_sys;

    int default_threshold;
    int adapt_inc_dec;
    int min_threshold;
    int max_threshold;

    int cached_status;
    int cached_threshold;
    bool valid_axis;
    //default constructor
    adaptive_wom_axis_data_t()
       :status_sys(""),
        threshold_sys(""),
        default_threshold(DEF_WOM_THRESHOLD),
        adapt_inc_dec(DEF_ADAPT_INC_DEC),
        min_threshold(DEF_WOM_THRESHOLD),
        max_threshold(DEF_WOM_THRESHOLD),
        cached_status(eSYSFS_RET_ZERO),
        cached_threshold(DEF_WOM_THRESHOLD),
        valid_axis(false)
    {}
    //parameterized constructor
    adaptive_wom_axis_data_t(
        const std::string &status,
        const std::string &threshold,
        int def_thr,
        int adpt_inc_dec,
        int min_thr,
        int max_thr
    )
        : status_sys(status),
          threshold_sys(threshold),
          default_threshold(def_thr),
          adapt_inc_dec(adpt_inc_dec),
          min_threshold(min_thr),
          max_threshold(max_thr),
          cached_status(eSYSFS_RET_ZERO),
          cached_threshold(DEF_WOM_THRESHOLD),
          valid_axis(true)
    {}
};

std::vector<adaptive_wom_axis_data_t> adaptive_wom_axis_data;
std::vector<std::vector<int>> max_threshold_map(NDDeviceTypeT::sDeviceTypeMax, std::vector<int>(eWOM_AXIS_MAX, DEF_WOM_THRESHOLD));
std::vector<std::vector<int>> min_threshold_map(NDDeviceTypeT::sDeviceTypeMax, std::vector<int>(eWOM_AXIS_MAX, DEF_WOM_THRESHOLD));


/*
 * Func name: msg_cb()
 * function update imu data published by nd-central
 * returns true on success and false on failure
 */
static bool msg_cb(ndmb_generic_msg_t *msg) {

    if(msg == NULL) {
        LOG_E(TAG, "The NDMB message for IMU is NULL");
        return false;
    }

    ndmbmsg_apm_imu_data_t *ptr1=NULL;
    ptr1 = reinterpret_cast<ndmbmsg_apm_imu_data_t *>( msg );
    if( msg->topic != TOPIC_APM_IMU_DATA ) {
        LOG_I(TAG, "****Unkown topic: ->%s",msg->topic);
	    return false;
    }

    pthread_mutex_lock(&imu_data_lock);
    bool ret = memcpy(&curr_data,&ptr1->imu_data_ptr, sizeof(apm_imu_data_t));
    if (false == ret) {
        LOG_E(TAG,"Memory copy failed for imu_data_ptr with return:%d",ret);
        pthread_mutex_unlock(&imu_data_lock);
        return false;
    }
    pthread_cond_signal(&imu_data_cv);
    pthread_mutex_unlock(&imu_data_lock);

    imu_data_q[flipflop.load()].push_back(curr_data);
    imu_bin_data_q[flipflop_bin_data.load()].push_back(curr_data);
    return true;
}

/*
 * Func name: filter_func()
 * function checks for device state
 * write set to imu bit on device moving state
 * write reset t imu bit mask if device is idle
 */
void IMU_worker::filter_func ()
{
    if(use_legacy == false){
        return;
    }

    bool above_threshold = false;


    // If already in IDLE, check the latest 10 seconds data to check if device is MOVING
    if(g_vehicle_state == STATIONARY) {

        for(int i = 0; i < NUM_AXES; i++) {
            if(SAD_avg[i][sad_idx] > imu_threshold) {
                above_threshold = true;
                break;
            }
        }
        if(above_threshold == true) {
            g_vehicle_state = MOVING;
            set_bit(IMU_MASK_POS);
            return;
        }
        return;
    }

    // We come here when we are in MOVING state
    // SAD_avg check for all the axes for last 200 seconds
    for(int i = 0; i < NUM_AXES; i++) {
        // SAD check for all the intervals of data
        for(int j = 0; j < NUM_SAD_CHUNKS; j++) {
            if(SAD_avg[i][j] > imu_threshold) {
                above_threshold = true;
                break;
            }
        }
        if(above_threshold == true) {
            break;
        }
    }

    if(above_threshold == false) {
        g_vehicle_state = STATIONARY;
        reset_bit(IMU_MASK_POS);
    }
    else {
        g_vehicle_state = MOVING;
        set_bit(IMU_MASK_POS);
    }


    return;
}

static void wom_glitch_report() {
    int pow_on_off_reason = 0;
    string reason("");
    nd_device_obj->get_reset_wake_reason(pow_on_off_reason, reason);
    bool wom_detected = pow_on_off_reason & ( 1 << PowerOnTriggerT::WAKEonMOTION_IMU );
    bool imu_detected = (g_vehicle_state == MOVING);
    if(wom_detected ^ imu_detected) {
        string err_msg = "Glitch Detected: WOM(" + std::to_string(wom_detected) + ") IMU(" + std::to_string(imu_detected) + ")";
        LOG_C(TAG, "%s", err_msg.c_str());
        nd_service_obj->send_err_msg(SM_E_APM_EVENT_STATUS, static_cast<int>(eIMU_GLITCH_AUX_CODE), err_msg);
    }
    return;
}

static bool get_adaptive_wom_thres(){ //returns true if all axis have WOM, else false

    //setting min threshold map
    min_threshold_map = {
        {DEF_WOM_THRESHOLD, DEF_WOM_THRESHOLD, DEF_WOM_THRESHOLD}, // 0: bagheera
        {DEF_WOM_THRESHOLD, DEF_WOM_THRESHOLD, DEF_WOM_THRESHOLD}, // 1: bagheera2
        {static_cast<int>(g_wom_x_thr) - (MAX_MISC_WAKEUP_COUNT * adaptive_inc_dec_map[NDDeviceTypeT::bagheera3][eWOM_X_AXIS]), static_cast<int>(g_wom_y_thr) - (MAX_MISC_WAKEUP_COUNT * adaptive_inc_dec_map[NDDeviceTypeT::bagheera3][eWOM_Y_AXIS]), static_cast<int>(g_wom_z_thr) - (MAX_MISC_WAKEUP_COUNT * adaptive_inc_dec_map[NDDeviceTypeT::bagheera3][eWOM_Z_AXIS])}, // 2: bagheera3
        {static_cast<int>(g_wom_x_thr) - (MAX_MISC_WAKEUP_COUNT * adaptive_inc_dec_map[NDDeviceTypeT::krait][eWOM_X_AXIS]), static_cast<int>(g_wom_y_thr) - (MAX_MISC_WAKEUP_COUNT * adaptive_inc_dec_map[NDDeviceTypeT::krait][eWOM_Y_AXIS]), static_cast<int>(g_wom_z_thr) - (MAX_MISC_WAKEUP_COUNT * adaptive_inc_dec_map[NDDeviceTypeT::krait][eWOM_Z_AXIS])}, // 3: krait
        {static_cast<int>(g_wom_x_thr) - (MAX_MISC_WAKEUP_COUNT * adaptive_inc_dec_map[NDDeviceTypeT::krait2][eWOM_X_AXIS]), static_cast<int>(g_wom_y_thr) - (MAX_MISC_WAKEUP_COUNT * adaptive_inc_dec_map[NDDeviceTypeT::krait2][eWOM_Y_AXIS]), static_cast<int>(g_wom_z_thr) - (MAX_MISC_WAKEUP_COUNT * adaptive_inc_dec_map[NDDeviceTypeT::krait2][eWOM_Z_AXIS])}  // 4: krait2
    };
    //setting max threshold map
    max_threshold_map = {
        {DEF_WOM_THRESHOLD, DEF_WOM_THRESHOLD, DEF_WOM_THRESHOLD}, // 0: bagheera
        {DEF_WOM_THRESHOLD, DEF_WOM_THRESHOLD, DEF_WOM_THRESHOLD}, // 1: bagheera2
        {static_cast<int>(g_wom_x_thr) + (MAX_MISC_WAKEUP_COUNT * adaptive_inc_dec_map[NDDeviceTypeT::bagheera3][eWOM_X_AXIS]), static_cast<int>(g_wom_y_thr) + (MAX_MISC_WAKEUP_COUNT * adaptive_inc_dec_map[NDDeviceTypeT::bagheera3][eWOM_Y_AXIS]), static_cast<int>(g_wom_z_thr) + (MAX_MISC_WAKEUP_COUNT * adaptive_inc_dec_map[NDDeviceTypeT::bagheera3][eWOM_Z_AXIS])}, // 2: bagheera3
        {static_cast<int>(g_wom_x_thr) + (MAX_MISC_WAKEUP_COUNT * adaptive_inc_dec_map[NDDeviceTypeT::krait][eWOM_X_AXIS]), static_cast<int>(g_wom_y_thr) + (MAX_MISC_WAKEUP_COUNT * adaptive_inc_dec_map[NDDeviceTypeT::krait][eWOM_Y_AXIS]), static_cast<int>(g_wom_z_thr) + (MAX_MISC_WAKEUP_COUNT * adaptive_inc_dec_map[NDDeviceTypeT::krait][eWOM_Z_AXIS])}, // 3: krait
        {static_cast<int>(g_wom_x_thr) + (MAX_MISC_WAKEUP_COUNT * adaptive_inc_dec_map[NDDeviceTypeT::krait2][eWOM_X_AXIS]), static_cast<int>(g_wom_y_thr) + (MAX_MISC_WAKEUP_COUNT * adaptive_inc_dec_map[NDDeviceTypeT::krait2][eWOM_Y_AXIS]), static_cast<int>(g_wom_z_thr) + (MAX_MISC_WAKEUP_COUNT * adaptive_inc_dec_map[NDDeviceTypeT::krait2][eWOM_Z_AXIS])}  // 4: krait2
    };

    //getting adaptive wom axis data for every axis
    adaptive_wom_axis_data.resize(eWOM_AXIS_MAX);
    // X axis
    adaptive_wom_axis_data[(int)wom_axis_t::eWOM_X_AXIS] = adaptive_wom_axis_data_t(
        wom_x_status_sys[(int)device_type],                                                             //status sysfs path
        wom_x_thres_sys[(int)device_type],                                                              //threshold sysfs path
        g_wom_x_thr,                                                                                    //default wom threshold (read from config)
        adaptive_inc_dec_map[(int)device_type][(int)wom_axis_t::eWOM_X_AXIS],                           //adaptive increment/decrement
        std::max(min_threshold_map[(int)device_type][(int)wom_axis_t::eWOM_X_AXIS], MIN_WOM_THRESHOLD), //min wom threshold
        std::min(max_threshold_map[(int)device_type][(int)wom_axis_t::eWOM_X_AXIS], MAX_WOM_THRESHOLD)  //max wom threshold
    );

    // Y axis
    adaptive_wom_axis_data[(int)wom_axis_t::eWOM_Y_AXIS] = adaptive_wom_axis_data_t(
        wom_y_status_sys[(int)device_type],
        wom_y_thres_sys[(int)device_type],
        g_wom_y_thr,
        adaptive_inc_dec_map[(int)device_type][(int)wom_axis_t::eWOM_Y_AXIS],
        std::max(min_threshold_map[(int)device_type][(int)wom_axis_t::eWOM_Y_AXIS], MIN_WOM_THRESHOLD),
        std::min(max_threshold_map[(int)device_type][(int)wom_axis_t::eWOM_Y_AXIS], MAX_WOM_THRESHOLD)
    );

    // Z axis
    adaptive_wom_axis_data[(int)wom_axis_t::eWOM_Z_AXIS] = adaptive_wom_axis_data_t(
        wom_z_status_sys[(int)device_type],
        wom_z_thres_sys[(int)device_type],
        g_wom_z_thr,
        adaptive_inc_dec_map[(int)device_type][(int)wom_axis_t::eWOM_Z_AXIS],
        std::max(min_threshold_map[(int)device_type][(int)wom_axis_t::eWOM_Z_AXIS], MIN_WOM_THRESHOLD),
        std::min(max_threshold_map[(int)device_type][(int)wom_axis_t::eWOM_Z_AXIS], MAX_WOM_THRESHOLD)
    );

    const std::string imu_device_sysfs = nd_factory_utils::get_imu_device_sysfs_path();

    uint wom_read_thr[wom_axis_t::eWOM_AXIS_MAX] = {0};
    uint idx = wom_axis_t::eWOM_X_AXIS;

    for (adaptive_wom_axis_data_t &axis : adaptive_wom_axis_data) {
        axis.valid_axis = true;

        if (false == read_from_sysfs_entry(imu_device_sysfs + axis.status_sys, axis.cached_status)) {//storing driver wom status
            LOG_E(TAG, "Failed to read status %s", axis.status_sys.c_str());
            axis.valid_axis = false;//read failure
        } else {
            LOG_I(TAG, "Read WOM status for %s as : %d", axis.status_sys.c_str(), axis.cached_status);
        }

        if (false == read_from_sysfs_entry(imu_device_sysfs + axis.threshold_sys, axis.cached_threshold)) {//storing driver wom threshold
            LOG_E(TAG, "Failed to read threshold %s", axis.threshold_sys.c_str());
            axis.valid_axis = false;//read failure
        } else {
            LOG_I(TAG, "Read WOM threshold for %s as : %d", axis.threshold_sys.c_str(), axis.cached_threshold);
        }

        wom_read_thr[idx++] = axis.cached_threshold;

        if(axis.cached_threshold < axis.min_threshold || axis.cached_threshold > axis.max_threshold){
            LOG_I(TAG, "WOM threshold %s read as %d is out of range [%d - %d]. Caching default %d", axis.threshold_sys.c_str(), axis.cached_threshold, axis.min_threshold, axis.max_threshold, axis.default_threshold);
            axis.valid_axis = false;//out of range
            axis.cached_threshold = std::max(axis.min_threshold, std::min(axis.default_threshold, MAX_WOM_THRESHOLD));//to clamp default threshold in range if default value is out of range
        }
    }

    //boot wom threshold
    int &wom_x_thres = adaptive_wom_axis_data[(int)wom_axis_t::eWOM_X_AXIS].cached_threshold;
    int &wom_y_thres = adaptive_wom_axis_data[(int)wom_axis_t::eWOM_Y_AXIS].cached_threshold;
    int &wom_z_thres = adaptive_wom_axis_data[(int)wom_axis_t::eWOM_Z_AXIS].cached_threshold;
    //boot wom status
    int &wom_x_status = adaptive_wom_axis_data[(int)wom_axis_t::eWOM_X_AXIS].cached_status;
    int &wom_y_status = adaptive_wom_axis_data[(int)wom_axis_t::eWOM_Y_AXIS].cached_status;
    int &wom_z_status = adaptive_wom_axis_data[(int)wom_axis_t::eWOM_Z_AXIS].cached_status;

    if((false == adaptive_wom_axis_data[(int)wom_axis_t::eWOM_X_AXIS].valid_axis) ||
       (false == adaptive_wom_axis_data[(int)wom_axis_t::eWOM_Y_AXIS].valid_axis) ||
       (false == adaptive_wom_axis_data[(int)wom_axis_t::eWOM_Z_AXIS].valid_axis) ) {
        wom_metrics_msg_t wom_metrics = {0};
        wom_metrics.ts = get_system_time();//time stamp of payload
        wom_metrics.uptime = get_system_monotonic_time();//uptime
        if(get_file_xattr(ND_UDID_SID, FileMetadataKey::udid, &wom_metrics.udid, sizeof(wom_metrics.udid))!= XATTR_OK){//udid
            LOG_E(TAG, "Read fail. udid is: %lld", wom_metrics.udid);
        }
        if(get_file_xattr(ND_UDID_SID, FileMetadataKey::sid, &wom_metrics.sid, sizeof(wom_metrics.sid))!= XATTR_OK){//sid
            LOG_E(TAG, "Read fail. sid is: %lld", wom_metrics.sid);
        }
        wom_metrics.veh_class = static_cast<int>(APM::Instance()->get_veh_class());//vehclass
        wom_metrics.event = wom_event_type_t::eWOMCorrectionEvent;
        //wom status
        wom_metrics.x_stat = adaptive_wom_axis_data[(int)wom_axis_t::eWOM_X_AXIS].cached_status;
        wom_metrics.y_stat = adaptive_wom_axis_data[(int)wom_axis_t::eWOM_Y_AXIS].cached_status;
        wom_metrics.z_stat = adaptive_wom_axis_data[(int)wom_axis_t::eWOM_Z_AXIS].cached_status;
        //read boot wom thresholds from IMU hardware
        wom_metrics.x_thr_boot = wom_read_thr[(int)wom_axis_t::eWOM_X_AXIS];
        wom_metrics.y_thr_boot = wom_read_thr[(int)wom_axis_t::eWOM_Y_AXIS];
        wom_metrics.z_thr_boot = wom_read_thr[(int)wom_axis_t::eWOM_Z_AXIS];
        //boot threshold after correction value
        wom_metrics.x_thr_set = wom_x_thres;
        wom_metrics.y_thr_set = wom_y_thres;
        wom_metrics.z_thr_set = wom_z_thres;
        //sending message to diagnostic to publish WOM health metrics on correction event
        LOG_I(TAG,"Sending WOM health metrics on correction event");
        send_msg ((generic_msg_t *)&wom_metrics, RES_WOM_METRICS, sizeof(wom_metrics), APM::Instance()->apm_q_name, diagnostic_q_name, 0);
    }

    std::string err_msg = "WOM Status X:" + std::to_string(wom_x_status) + ",Y:" + std::to_string(wom_y_status) + ",Z:" + std::to_string(wom_z_status) +
                          " Boot Thresholds X:" + std::to_string(wom_x_thres) + ",Y:" + std::to_string(wom_y_thres) + ",Z:" + std::to_string(wom_z_thres);
    LOG_I(TAG, "%s", err_msg.c_str());
    int aux_code = wom_x_thres + (wom_y_thres << 10) + (wom_z_thres << 20);
    nd_service_obj->send_err_msg(SM_E_APM_AON_VERSION_INFO, aux_code, err_msg);

    if((wom_x_status == WAKE_ON_MOTION_TRUE) && (wom_y_status == WAKE_ON_MOTION_TRUE) && (wom_z_status == WAKE_ON_MOTION_TRUE)){
        return true;
    }
    return false;
}

static void persist_boot_wom_thres() {

    int &wom_x_thr_boot = adaptive_wom_axis_data[(int)wom_axis_t::eWOM_X_AXIS].cached_threshold;
    int &wom_y_thr_boot = adaptive_wom_axis_data[(int)wom_axis_t::eWOM_Y_AXIS].cached_threshold;
    int &wom_z_thr_boot = adaptive_wom_axis_data[(int)wom_axis_t::eWOM_Z_AXIS].cached_threshold;

    LOG_I(TAG, "Persisting boot WOM thresholds - X: %d Y: %d Z: %d", wom_x_thr_boot, wom_y_thr_boot, wom_z_thr_boot);
    APM::Instance()->set_wom_thresholds(wom_x_thr_boot, wom_y_thr_boot, wom_z_thr_boot);
    nd_device_obj->configure_wom_thresholds(wom_x_thr_boot, wom_y_thr_boot, wom_z_thr_boot);

    wom_metrics_msg_t wom_metrics = {0};
    wom_metrics.ts = get_system_time();//time stamp of payload
    wom_metrics.uptime = get_system_monotonic_time();//uptime
    if(get_file_xattr(ND_UDID_SID, FileMetadataKey::udid, &wom_metrics.udid, sizeof(wom_metrics.udid))!= XATTR_OK){//udid
        LOG_E(TAG, "Read fail. udid is: %lld", wom_metrics.udid);
    }
    if(get_file_xattr(ND_UDID_SID, FileMetadataKey::sid, &wom_metrics.sid, sizeof(wom_metrics.sid))!= XATTR_OK){//sid
        LOG_E(TAG, "Read fail. sid is: %lld", wom_metrics.sid);
    }
    wom_metrics.veh_class = static_cast<int>(APM::Instance()->get_veh_class());//vehclass
    wom_metrics.event = wom_event_type_t::eWOMPersistEvent;
    //wom status
    wom_metrics.x_stat = adaptive_wom_axis_data[(int)wom_axis_t::eWOM_X_AXIS].cached_status;
    wom_metrics.y_stat = adaptive_wom_axis_data[(int)wom_axis_t::eWOM_Y_AXIS].cached_status;
    wom_metrics.z_stat = adaptive_wom_axis_data[(int)wom_axis_t::eWOM_Z_AXIS].cached_status;
    //persisted boot wom thresholds
    wom_metrics.x_thr_boot = wom_x_thr_boot;
    wom_metrics.y_thr_boot = wom_y_thr_boot;
    wom_metrics.z_thr_boot = wom_z_thr_boot;
    //config read wom thresholds
    wom_metrics.x_thr_set = g_wom_x_thr;
    wom_metrics.y_thr_set = g_wom_y_thr;
    wom_metrics.z_thr_set = g_wom_z_thr;
    //sending message to diagnostic to publish WOM health metrics on boot data
    LOG_I(TAG,"Sending WOM health metrics on boot data");
    send_msg ((generic_msg_t *)&wom_metrics, RES_WOM_METRICS, sizeof(wom_metrics), APM::Instance()->apm_q_name, diagnostic_q_name, 0);
    return;
}

static void set_adaptive_wom_thres(wom_trigger_status_t wom_trigger_status) {

    //Reference variables for thresholds
    int &wom_x_thres = adaptive_wom_axis_data[(int)wom_axis_t::eWOM_X_AXIS].cached_threshold;
    int &wom_y_thres = adaptive_wom_axis_data[(int)wom_axis_t::eWOM_Y_AXIS].cached_threshold;
    int &wom_z_thres = adaptive_wom_axis_data[(int)wom_axis_t::eWOM_Z_AXIS].cached_threshold;

    LOG_I(TAG, "Initial WOM Thres X: %d Y: %d Z: %d", wom_x_thres, wom_y_thres, wom_z_thres);

    wom_metrics_msg_t wom_metrics = {0};
    wom_metrics.ts = get_system_time();//time stamp of payload
    wom_metrics.uptime = get_system_monotonic_time();//uptime
    if(get_file_xattr(ND_UDID_SID, FileMetadataKey::udid, &wom_metrics.udid, sizeof(wom_metrics.udid))!= XATTR_OK){//udid
        LOG_E(TAG, "Read fail. udid is: %lld", wom_metrics.udid);
    }
    if(get_file_xattr(ND_UDID_SID, FileMetadataKey::sid, &wom_metrics.sid, sizeof(wom_metrics.sid))!= XATTR_OK){//sid
        LOG_E(TAG, "Read fail. sid is: %lld", wom_metrics.sid);
    }
    wom_metrics.veh_class = static_cast<int>(APM::Instance()->get_veh_class());//vehclass
    wom_metrics.x_stat = adaptive_wom_axis_data[(int)wom_axis_t::eWOM_X_AXIS].cached_status;
    wom_metrics.y_stat = adaptive_wom_axis_data[(int)wom_axis_t::eWOM_Y_AXIS].cached_status;
    wom_metrics.z_stat = adaptive_wom_axis_data[(int)wom_axis_t::eWOM_Z_AXIS].cached_status;
    //boot wom thresholds after last persistance
    wom_metrics.x_thr_boot = wom_x_thres;
    wom_metrics.y_thr_boot = wom_y_thres;
    wom_metrics.z_thr_boot = wom_z_thres;

    for(adaptive_wom_axis_data_t &axis : adaptive_wom_axis_data) {
        //increment if trigger is misc wakeup and axis WOM status is true. reset decrement to config defaults.
        if( MISC_WAKEUP_TRUE == wom_trigger_status ) {
            wom_metrics.event = wom_event_type_t::eWOMIncreaseEvent;
            int adapt_inc = ( WAKE_ON_MOTION_TRUE == axis.cached_status ) ? axis.adapt_inc_dec : 0;
            axis.cached_threshold = std::max( axis.default_threshold, axis.cached_threshold + adapt_inc);
        }
        //decrement if trigger is IMU status is MOVING. reset increment to config defaults.
        else if( MISC_WAKEUP_IMU_MOVING_ALL_AXIS == wom_trigger_status ) {
            wom_metrics.event = wom_event_type_t::eWOMDecreaseEvent;
            axis.cached_threshold = std::min( axis.default_threshold, axis.cached_threshold - axis.adapt_inc_dec);
        }

        //Range Check
        if(axis.cached_threshold < axis.min_threshold){
            LOG_I(TAG, "%s threshold low (%d). Clamping to min %d", axis.threshold_sys.c_str(), axis.cached_threshold, axis.min_threshold);
            axis.cached_threshold = axis.min_threshold;
        }

        if(axis.cached_threshold > axis.max_threshold){
            LOG_I(TAG, "%s threshold high (%d). Clamping to max %d", axis.threshold_sys.c_str(), axis.cached_threshold,  axis.max_threshold);
            axis.cached_threshold = axis.max_threshold;
        }
        else {
            LOG_I(TAG, "Adaptive %s threshold changed to %d", axis.threshold_sys.c_str(), axis.cached_threshold);
        }
    }

    //adaptive wom thresholds after increment/decrement
    wom_metrics.x_thr_set = wom_x_thres;
    wom_metrics.y_thr_set = wom_y_thres;
    wom_metrics.z_thr_set = wom_z_thres;

    LOG_I(TAG, "Configuring WOM Thres X: %d Y: %d Z: %d", wom_x_thres, wom_y_thres, wom_z_thres);
    APM::Instance()->set_wom_thresholds(wom_x_thres, wom_y_thres, wom_z_thres);

    bool status = nd_device_obj->configure_wom_thresholds(wom_x_thres, wom_y_thres, wom_z_thres);
    if(false == status) {// retry configure wom thresholds
        LOG_E(TAG, "Retrying configure WOM thresholds");
        status = nd_device_obj->configure_wom_thresholds(wom_x_thres, wom_y_thres, wom_z_thres);
    }

    std::string err_msg = std::string("Adaptive WOM Set Status: ") + (status ?"PASS":"FAIL") + " Thresholds X:" + std::to_string(wom_x_thres) + ",Y:" + std::to_string(wom_y_thres) + ",Z:" + std::to_string(wom_z_thres);
    LOG_I(TAG, "%s", err_msg.c_str());
    int aux_code = wom_x_thres + (wom_y_thres << 10) + (wom_z_thres << 20);
    nd_service_obj->send_err_msg(SM_E_APM_AON_VERSION_INFO, aux_code, err_msg);
    //sending message to diagnostic to publish WOM health metrics on adaptive event
    LOG_I(TAG,"Sending WOM health metrics on adaptive event");
    send_msg ((generic_msg_t *)&wom_metrics, RES_WOM_METRICS, sizeof(wom_metrics), APM::Instance()->apm_q_name, diagnostic_q_name, 0);
    return;
}


static void detect_motion_imu() {

    apm_imu_data_t data;
    bool reported_once = false; //flag to report glitch only once for false positive WOM
    while(1) {
        if((nullptr != APM::WorkerInstance(eInertial)) && (true == APM::WorkerInstance(eInertial)->use_legacy)){
            all_thread_keepalive_status &= ~(1 << IMU_MASK_POS);
        }
        sleep(motion_detect_interval);

        // Switch the queue to capture data from NDC
        flipflop.store(!flipflop.load());
        // Take filled queue for analysis
        int queue_size = (imu_data_q[!flipflop.load()]).size();

        LOG_I(TAG, "queue_size = %d", queue_size);
        // init the SAD for sad_idx
        for(int i = 0; i < NUM_AXES; i++) {
            SAD_avg[i][sad_idx] = 0.0;
        }

        if(queue_size == 0) {
            // If A SuperCap Active Clear Event is missed, then IMU Data Loss will be captured by imu_data_outage_count,
            // will be used by supercap thread to re-initialize the IMU.
            imu_data_outage_count++;
            LOG_I(TAG, "No data from IMU");
            LOG_I(TAG, "imu_data_outage_count :%d", imu_data_outage_count);
            continue;
        }
        imu_data_outage_count = 0;

        bool first_time = true;
        while (!imu_data_q[!flipflop.load()].empty()) {
            data = imu_data_q[!flipflop.load()].front();
            imu_data_q[!flipflop.load()].pop_front();


            if(first_time == false) {
                SAD_avg[0][sad_idx] += fabs(data.accel_x - prev_data.accel_x);
                SAD_avg[1][sad_idx] += fabs(data.accel_y - prev_data.accel_y);
                SAD_avg[2][sad_idx] += fabs(data.accel_z - prev_data.accel_z);
                SAD_avg[3][sad_idx] += fabs(data.gyro_x - prev_data.gyro_x);
                SAD_avg[4][sad_idx] += fabs(data.gyro_y - prev_data.gyro_y);
                SAD_avg[5][sad_idx] += fabs(data.gyro_z - prev_data.gyro_z);
            }

            prev_data = data;
            first_time = false;
        }

        // calculate average
        for(int i = 0; i < NUM_AXES; i++) {
            SAD_avg[i][sad_idx] = SAD_avg[i][sad_idx] / queue_size;
            LOG_I(TAG, "sad_avg = %f, sad_idx = %d", SAD_avg[i][sad_idx], sad_idx);
        }


        // call filter function on data
	    APM::WorkerInstance(eInertial)->filter_func();

        sad_idx = (sad_idx + 1) % NUM_SAD_CHUNKS;

        if(false == reported_once) {
            wom_glitch_report();
            reported_once = true;
        }
    }

}

void detect_wom_trigger_fn() {

    APM::Instance()->get_wom_thresholds(g_wom_x_thr, g_wom_y_thr, g_wom_z_thr);

    //Getting the initial adaptive WOM thresholds during startup
    bool are_all_wom = false;
    if (true == nd_device_obj->is_wake_on_motion_supported()) {
        are_all_wom = get_adaptive_wom_thres();
    } else {
        LOG_C(TAG,"exiting detect_wom_trigger_fn. is_wake_on_motion_supported:%d", nd_device_obj->is_wake_on_motion_supported());
        return;
    }

    bool wom_enable = ((g_wom_x_thr != MAX_WOM_THRESHOLD) || (g_wom_y_thr != MAX_WOM_THRESHOLD) || (g_wom_z_thr != MAX_WOM_THRESHOLD));
    if ( (true == wom_enable) && (adaptive_wom_trigger_count >= MIN_MISC_WAKEUP_ENABLE_ADAPTIVE_WOM) ) {
        LOG_I(TAG, "inside %s", __func__);
    } else {
        LOG_C(TAG, "exiting detect_wom_trigger_fn. apm_wom_enable:%d , adaptive_wom_trigger_count:%d", wom_enable, adaptive_wom_trigger_count);
        return;
    }

    //persist read WOM thresholds (from IMU driver) in case of ignition/lpw/misc wakeups
    persist_boot_wom_thres();

    {
        //Check for pseudo ignition & imu status after 30 seconds wait for event
        constexpr int DETECT_PSEUDO_IGN_DELAY_SEC = 30;
        sleep(DETECT_PSEUDO_IGN_DELAY_SEC);
    }

    //reading initial imu vehicle status for decrement case
    motion_status_t imu_vehicle_status = APM::WorkerInstance(eInertial)->read_status();

    //WOM Trigger sysfs file setup
    int trigger_fd = eSYSFS_READ_ERROR;
    int ret = eSYSFS_READ_ERROR;

    char trigger_val = '\0';
    fd_set fds;

    int prev_val = eSYSFS_READ_ERROR;

    //Check for WOM trigger sysfs path
    std::string wom_trigger_sysfs_path = nd_factory_utils::get_wom_trigger_sysfs_path();

    trigger_fd = open(wom_trigger_sysfs_path.c_str(), O_RDWR);
    if (trigger_fd < eSYSFS_RET_ZERO) {
        LOG_C(TAG, "Failed to open WOM trigger sysfs file");
        return;
    } 
    else {
        //writing initial trigger as 0
        LOG_I(TAG, "Initialising WOM trigger as %d", MISC_WAKEUP_FALSE);
        write_into_sysfs_entry(wom_trigger_sysfs_path, static_cast<int>(MISC_WAKEUP_FALSE));

        //Drain initial state to avoid pending event for select unblock
        if (lseek(trigger_fd, 0, SEEK_SET) < eSYSFS_RET_ZERO) {
            LOG_E(TAG, "initial lseek failed: %s", strerror(errno));
        } else {
            ret = read(trigger_fd, &trigger_val, 1);//initial read to clear the prior interrupts
            if(ret <= eSYSFS_RET_ZERO){
                LOG_E(TAG, "failed to read appropriate value; %d, %s", ret, strerror(errno));
            }
            else {
                prev_val = (int)(trigger_val - '0');
                if(prev_val != static_cast<int>(MISC_WAKEUP_FALSE)){
                    LOG_E(TAG, "malfunctioning WOM trigger entry = %d", prev_val);
                    close(trigger_fd);
                    return;
                }
                LOG_I(TAG, "initial WOM trigger value = %d", prev_val);
            }
        }
    }

    //Getting reset wake reason if supported
    int pow_on_off_reason = 0;
    string reason_str = "";
    if (nd_device_obj->get_reset_wake_reason(pow_on_off_reason, reason_str) == false) {
        LOG_E(TAG, "get_reset_wake_reason failed");
    }
    LOG_I(TAG, "pow_on_off_reason %d, reason_str %s", pow_on_off_reason, reason_str.c_str());

    //check for low power wakeup
    bool is_lpw = (pow_on_off_reason & WAKE_ON_RTC_MASK);
    LOG_I(TAG, "is_lpw: %d", is_lpw);

    power_crank_levels_t crank_level = nd_device_obj->get_crank_level();//pseudo ignition status

    //if pseudo ignition ON or LPW
    if((CRANK_HIGH == crank_level)||(true == is_lpw)) {

        //reset incremented cached thresholds to default value (config thresholds)
        adaptive_wom_axis_data[(int)wom_axis_t::eWOM_X_AXIS].cached_threshold = min( adaptive_wom_axis_data[(int)wom_axis_t::eWOM_X_AXIS].cached_threshold, adaptive_wom_axis_data[(int)wom_axis_t::eWOM_X_AXIS].default_threshold);
        adaptive_wom_axis_data[(int)wom_axis_t::eWOM_Y_AXIS].cached_threshold = min( adaptive_wom_axis_data[(int)wom_axis_t::eWOM_Y_AXIS].cached_threshold, adaptive_wom_axis_data[(int)wom_axis_t::eWOM_Y_AXIS].default_threshold);
        adaptive_wom_axis_data[(int)wom_axis_t::eWOM_Z_AXIS].cached_threshold = min( adaptive_wom_axis_data[(int)wom_axis_t::eWOM_Z_AXIS].cached_threshold, adaptive_wom_axis_data[(int)wom_axis_t::eWOM_Z_AXIS].default_threshold);

        //persisting min as config threshold
        persist_boot_wom_thres();

        if( MOVING == imu_vehicle_status ) {// incase vehicle is moving, decrement thresholds
            //Setting wom trigger sysfs to MISC_WAKEUP_IMU_MOVING_ALL_AXIS to decrement adaptive wom thres
            LOG_I(TAG, "Setting %s to %d for moving vehicle status", wom_trigger_sysfs_path.c_str(), static_cast<int>(MISC_WAKEUP_IMU_MOVING_ALL_AXIS));
            write_into_sysfs_entry(wom_trigger_sysfs_path, static_cast<int>(MISC_WAKEUP_IMU_MOVING_ALL_AXIS));
        } else {
            LOG_C(TAG, "IMU stationary or all axis not WOM. Exiting adaptive WOM monitoring");
            close(trigger_fd);
            return;
        }
    }

    //if misc LPW
    else {
        int misc_wakeup_count =  eSYSFS_RET_ZERO;
        if(false == read_from_sysfs_entry(nd_factory_utils::get_misc_wakeup_count_sysfs_path(), misc_wakeup_count)) {
            LOG_E(TAG, "Failed to read misc wakeup count");
            return;
        }

        //exiting from thread if misc wakeup count is outside adaptive wom trigger event window
        if((misc_wakeup_count < adaptive_wom_trigger_count) || (misc_wakeup_count > (adaptive_wom_trigger_count + MAX_MISC_WAKEUP_COUNT))){
            LOG_I(TAG, "Misc WakeUp Count(%d) Outside Adaptive WOM Trigger Event Window(%d<->%d)", misc_wakeup_count, adaptive_wom_trigger_count , (adaptive_wom_trigger_count+MAX_MISC_WAKEUP_COUNT) );
            return;
        }

        //wom trigger only for same parity (even,even) or (odd,odd) pairs of (misc_wakeup_count, adaptive_wom_trigger_count)
        //exiting from thread if misc wakeup count is odd and adaptive wom trigger count is even and vice versa
        bool is_even_odd_mismatch = ((adaptive_wom_trigger_count&1) != (misc_wakeup_count&1));
        if(true == is_even_odd_mismatch){
            LOG_I(TAG, "Misc WakeUp Count(%d) Parity Mismatch with Adaptive WOM Trigger Count(%d). Exiting adaptive WOM monitoring", misc_wakeup_count, adaptive_wom_trigger_count);
            return;
        }
    }

    LOG_I(TAG, "Monitoring WOM trigger Status");

    while (1) {
        FD_ZERO(&fds);
        FD_SET(trigger_fd, &fds);

        ret = select(trigger_fd+1, NULL, NULL, &fds, NULL);// blocking call for wom_trigger_sysfs_path

        if(ret < eSYSFS_RET_ZERO) {//select check
            LOG_E(TAG, "Failed in select function");
            if((-1 == ret) && (EINTR == errno)){
                LOG_C(TAG, "ret == -1 && errno == EINTR");
                continue;
            }
            else {
                LOG_E(TAG, "Failed in select. Returning from detect_wom_trigger_fn.");
                break;
            }
        }

        if (false == FD_ISSET(trigger_fd, &fds)) {//FD_ISSET check
            LOG_E(TAG, "FD_ISSET returning false");
            continue;
        }

        ret = lseek(trigger_fd, 0, SEEK_SET);//reposition to start of file
        if(ret == -1) {
            LOG_E(TAG, "lseek error %d, %s", ret, strerror(errno));
            continue;
        }

        ret = read(trigger_fd, &trigger_val, 1);//read into trigger_val
        if(ret != 1){
            LOG_E(TAG, "failed to read from %s", wom_trigger_sysfs_path.c_str());
            continue;
        }

        int curr_val = (int)(trigger_val - '0');

        if(curr_val == prev_val){//edge-triggered WOM status check
            LOG_C(TAG, "No change in WOM trigger value: %d", curr_val);
            continue;
        }

        prev_val = curr_val;//update previous value

        LOG_C(TAG, "WOM trigger status changed to %c", trigger_val);

        set_adaptive_wom_thres(static_cast<wom_trigger_status_t>(curr_val));//call to set adaptive wom thresholds
        //once adaptive wom thres are set, we exit the thread
        break;
    }

    LOG_I(TAG, "Exiting IMU worker poll thread");
    close(trigger_fd);
    return;
}

/*
 * Func name: IMU_worker::poll_func()
 * dummy function
 * returns NULL on success and APM_ERROR on failure
 */
void* IMU_worker::poll_func() {

    if(enabled == false) {
        LOG_D(TAG, "IMU worker disabled, running wom trigger thread only");
        return NULL;
    }

	detect_engine_status();
    return NULL;
}

void IMU_worker::compute_sad_avg_bin_data(apm_imu_data_t &data, float &correct_distance, float &correct_velocity) {
    int queue_size = (imu_bin_data_q[!flipflop_bin_data.load()]).size();

    // Safety check: prevent division by zero
    if(queue_size == 0) {
        LOG_W(TAG, "IMU bin data queue is empty, skipping SAD computation");
        data.accel_time = -1;
        data.gyro_time = -1;
        imu_bin_data_q[!flipflop_bin_data.load()].clear();
        if((nullptr != eng_stat_worker) && (true == eng_stat_worker->is_worker_enabled())) {
            eng_stat_worker->prev_accel_rms = 0.0f; // Reset RMS to zero for invalid data
            eng_stat_worker->prev_gyro_rms = 0.0f; // Reset RMS to zero for invalid data
            eng_stat_worker->insert_bin_data(data); // Insert invalid data to maintain sync with engine state worker
            eng_stat_worker->detect_engine_status(eng_stat_worker->is_first_sample); // Detect engine status with invalid data
            if(eng_stat_worker->is_first_sample) {
                eng_stat_worker->is_first_sample = false;
            }
        }
        return;
    }

    bool first_time = true;
    apm_imu_data_t prev_data_local;
    uint64_t last_accel_time = 0, last_gyro_time = 0;
    float step_time = 1.0f / sampling_rate;
    float SAD_avg[NUM_AXES] = {0.0};
    float velocity[NUM_AXES] = {0.0}; //diff in accel/gyro.
    float distance[NUM_AXES] = {0.0}; //diff in accel/gyro.
    float corrected_velocity = 0.0f;
    float corrected_distance = 0.0f;

    while(!imu_bin_data_q[!flipflop_bin_data.load()].empty()) {
        data = imu_bin_data_q[!flipflop_bin_data.load()].front();
        imu_bin_data_q[!flipflop_bin_data.load()].pop_front();

        // Call insert_bin_data for engine state worker for each sample
        if((nullptr != eng_stat_worker) && (true == eng_stat_worker->is_worker_enabled())) {
            eng_stat_worker->insert_bin_data(data);
            eng_stat_worker->detect_engine_status(eng_stat_worker->is_first_sample);
            if(eng_stat_worker->is_first_sample) {
                eng_stat_worker->is_first_sample = false;
            }
        }

        if(first_time == false) {
            SAD_avg[eIMU_Accel_X] += fabs(data.accel_x - prev_data_local.accel_x);
            SAD_avg[eIMU_Accel_Y] += fabs(data.accel_y - prev_data_local.accel_y);
            SAD_avg[eIMU_Accel_Z] += fabs(data.accel_z - prev_data_local.accel_z);
            SAD_avg[eIMU_Gyro_X] += fabs(data.gyro_x - prev_data_local.gyro_x);
            SAD_avg[eIMU_Gyro_Y] += fabs(data.gyro_y - prev_data_local.gyro_y);
            SAD_avg[eIMU_Gyro_Z] += fabs(data.gyro_z - prev_data_local.gyro_z);

            // Remove gravity component from X axis acceleration
            // Remove bias from all axes if needed (assuming bias is zero here)
            // Read bias to be removed
            float corrected_accel_x = (data.accel_x - 9.81f);
            float prev_corrected_accel_x = (prev_data_local.accel_x - 9.81f);

            // Integrate acceleration to get velocity (simple trapezoidal integration)
            float correct_accel = sqrt(
                corrected_accel_x * corrected_accel_x +
                data.accel_y * data.accel_y +
                data.accel_z * data.accel_z
            );

            corrected_velocity += (correct_accel * step_time);
            corrected_distance += (corrected_velocity * step_time);

        }
        first_time = false;
        last_accel_time = data.accel_time;
        last_gyro_time = data.gyro_time;
        prev_data_local = data;
    }

    if(queue_size != 0){
        data.accel_x = SAD_avg[eIMU_Accel_X] / queue_size;
        data.accel_y = SAD_avg[eIMU_Accel_Y] / queue_size;
        data.accel_z = SAD_avg[eIMU_Accel_Z] / queue_size;
        data.gyro_x = SAD_avg[eIMU_Gyro_X] / queue_size;
        data.gyro_y = SAD_avg[eIMU_Gyro_Y] / queue_size;
        data.gyro_z = SAD_avg[eIMU_Gyro_Z] / queue_size;

        correct_velocity = corrected_velocity;
        correct_distance = corrected_distance;
    }
    data.accel_time = last_accel_time;
    data.gyro_time = last_gyro_time;

    imu_bin_data_q[!flipflop_bin_data.load()].clear();

    // Increment the valid data count
    push_data_to_sysfs(eIMU_VALID,0); // 0 will read the current count and increment
}

void IMU_worker::insert_bin_data() {

    if(false == use_legacy) {
        all_thread_keepalive_status &= ~(1 << get_signal_mask());
    }
    flipflop_bin_data.store(!flipflop_bin_data.load());
    apm_imu_data_t data = {0};
    float correct_distance = 0.0f, correct_velocity = 0.0f;
    compute_sad_avg_bin_data(data, correct_distance, correct_velocity);
    ImuData imu_bin_data;
    if(data.accel_time == -1 && data.gyro_time == -1) {
        for(int i = 0; i < eIMU_AxisMax; ++i) {
            imu_bin_data.axis[i] = static_cast<float>(SysfsErrorCodes::eSYSFS_INVALID_DATA);
        }
    }else {
        // Populate imu_bin_data structure
        // 1 sec SAD average data
        imu_bin_data.axis[eIMU_Accel_X] = data.accel_x;
        imu_bin_data.axis[eIMU_Accel_Y] = data.accel_y;
        imu_bin_data.axis[eIMU_Accel_Z] = data.accel_z;
        imu_bin_data.axis[eIMU_Gyro_X] = data.gyro_x;
        imu_bin_data.axis[eIMU_Gyro_Y] = data.gyro_y;
        imu_bin_data.axis[eIMU_Gyro_Z] = data.gyro_z;
        // Corrected distance and velocity
        imu_bin_data.corrected_distance = correct_distance;
        imu_bin_data.corrected_velocity = correct_velocity;
    }

    getWorkerBinDataObj()->set_bin_data(get_index(), (void*)&imu_bin_data);

    // Update index for next data
    incr_index();
}


/*
 * Func name: IMU_worker::intr_func()
 * function subs. the imu data from nd-central
 * returns NULL on success and APM_ERROR on failure
 */
void* IMU_worker::intr_func() {

    std::thread adaptive_wom_th (detect_wom_trigger_fn);

    if(enabled == false) {
        LOG_I(TAG, "IMU worker disabled, running wom trigger thread only");
        if(adaptive_wom_th.joinable()){
            adaptive_wom_th.join();
        }
        return NULL;
    }

    std::string s = "NDMB_APM_SERVICE";
    NDMBClient msg_client(s);
    bool ret = msg_client.subscribe(TOPIC_APM_IMU_DATA, msg_cb);
    if(false == ret)
    {
        LOG_E(TAG,"msg_client.subscribe failed");
        nd_service_obj->send_err_msg(SM_E_APM_SUBS_FAIL, 0, "Imu subscriber failed");
        g_vehicle_state = STATIONARY;
	    return APM_ERROR;
    }

    thread t1 (detect_motion_imu);

    int64_t latest_data_timestamp = get_system_monotonic_time();

    pthread_condattr_init(&imu_data_attr);
    pthread_condattr_setclock(&imu_data_attr, CLOCK_MONOTONIC);
    pthread_cond_init(&imu_data_cv, &imu_data_attr);

    struct timespec timeToWait = {0};

    while(1) {

        if(get_system_monotonic_time() > (latest_data_timestamp + MAX_DATA_OUTAGE_TIME)) {
            LOG_I(TAG, "Data outage timeout");
            g_vehicle_state = STATIONARY;
            reset_bit(IMU_MASK_POS);
        }

        pthread_mutex_lock(&imu_data_lock);
        if ( FAILURE == clock_gettime(CLOCK_MONOTONIC, &timeToWait) ) {
            LOG_E(TAG, "clock_gettime failed, setting the wait time to 0 so that pthread_cond_timedwait will unblock immediately");
            // set time to 0, so pthread_cond_timedwait will unblock immediately
            timeToWait.tv_sec = 0;
            timeToWait.tv_nsec = 0;
        }
        else {
            timeToWait.tv_sec += COND_VAR_TIMEOUT;
        }

        if (ETIMEDOUT == pthread_cond_timedwait (&imu_data_cv,&imu_data_lock, &timeToWait)) {
            LOG_W (TAG, "CV Timeout when trying to Memory copy for imu_data_ptr.");
        }
        else {
            latest_data_timestamp = get_system_monotonic_time();
        }
        pthread_mutex_unlock(&imu_data_lock);
    }

    if(t1.joinable()){
        t1.join();
    }

    if(adaptive_wom_th.joinable()){
        adaptive_wom_th.join();
    }

    return NULL;
}

/*
 * Func name: IMU_worker::read_status()
 * function returns the motion status from IMU
 */
motion_status_t IMU_worker::read_status() {

    if((false == enabled) || (true == sensor_decision_disabled)) {
        LOG_D(TAG, "IMU worker(%d), sensor_decision_disabled(%d). Returning STATIONARY", enabled, sensor_decision_disabled);
        return STATIONARY;
    }

    string ign_imu_sysfs_path = nd_device_obj->get_imu_ign_sysfs_path();

    if(false == APM::WorkerInstance(eInertial)->use_legacy){

        motion_status_t status =  STATIONARY;

        if(false == init_state_read) {
            status = getInitialState();
            init_state_read = true;
        }
        else {
            status = getMotionState();
        }

        if (STATIONARY == status) {
            write_into_sysfs_entry(ign_imu_sysfs_path, IGNITION_OFF);
        }
        else if(MOVING == status) {
            write_into_sysfs_entry(ign_imu_sysfs_path, IGNITION_ON);
        }
        else {
            LOG_E(TAG, "Unknown motion state : %d", status);
            write_into_sysfs_entry(ign_imu_sysfs_path, IGNITION_OFF);
        }
        return status;
    }

    if (STATIONARY == g_vehicle_state) {
        write_into_sysfs_entry(ign_imu_sysfs_path, IGNITION_OFF);
    }
    else if(MOVING == g_vehicle_state) {
        write_into_sysfs_entry(ign_imu_sysfs_path, IGNITION_ON);
    }
    else {
        LOG_E(TAG, "Unknown motion state : %d", g_vehicle_state.load());
        write_into_sysfs_entry(ign_imu_sysfs_path, IGNITION_OFF);
    }
    return g_vehicle_state;
}

bool IMU_worker::update_data_outage_count(){
    bool data_outage_incemented = APM_worker::update_data_outage_count();
    if(false == data_outage_incemented){
        eng_stat_worker->getWorkerDataObj().resetDataOutageCnt();
    }
    return data_outage_incemented;
}

motion_status_t IMU_worker::getEngineState(){
    // Only share the motion state if worker is enabled and sensor decision is not disabled.
    if((eng_stat_worker != nullptr) && (true == eng_stat_worker->is_worker_enabled()) && (false == eng_stat_worker->sensor_decision_disabled) && (true == getWorkerDataObj().getFusionActivated())) {
        return eng_stat_worker->getMotionState();
    }
    return STATIONARY;
}

void* ENG_STAT_worker::intr_func() {
    return NULL;
}

void* ENG_STAT_worker::poll_func() {
    return NULL;
}

motion_status_t ENG_STAT_worker::read_status() {
    return STATIONARY;
}

void ENG_STAT_worker::filter_func() {
    return;
}

void ENG_STAT_worker::insert_bin_data() {
    return;
}

void ENG_STAT_worker::insert_bin_data(apm_imu_data_t &data) {
    EngStatData eng_stat_bin_data;
    if((data.accel_time == -1) && (data.gyro_time == -1)){
        for(int i = 0; i < eENG_STAT_AxisMax; ++i) {
            eng_stat_bin_data.axis[i] = static_cast<float>(SysfsErrorCodes::eSYSFS_INVALID_DATA);
        }
        getWorkerBinDataObj()->set_bin_data(get_index(), (void*)&eng_stat_bin_data);
        incr_index();
        return;
    }

    eng_stat_bin_data.axis[eENG_STAT_Accel_X] = data.accel_x;
    eng_stat_bin_data.axis[eENG_STAT_Accel_Y] = data.accel_y;
    eng_stat_bin_data.axis[eENG_STAT_Accel_Z] = data.accel_z;
    eng_stat_bin_data.axis[eENG_STAT_Gyro_X] = data.gyro_x;
    eng_stat_bin_data.axis[eENG_STAT_Gyro_Y] = data.gyro_y;
    eng_stat_bin_data.axis[eENG_STAT_Gyro_Z] = data.gyro_z;
    float accel_rms = sqrt(data.accel_x * data.accel_x + data.accel_y * data.accel_y + data.accel_z * data.accel_z);
    float gyro_rms = sqrt(data.gyro_x * data.gyro_x + data.gyro_y * data.gyro_y + data.gyro_z * data.gyro_z);
    eng_stat_bin_data.axis[eENG_STAT_Accel_RMS] = accel_rms;
    eng_stat_bin_data.axis[eENG_STAT_Gyro_RMS] = gyro_rms;

    if(prev_accel_rms > 0.0 && prev_gyro_rms > 0.0) {
        float accel_rms_diff = fabs(accel_rms - prev_accel_rms);
        float gyro_rms_diff = fabs(gyro_rms - prev_gyro_rms);
        eng_stat_bin_data.axis[eENG_STAT_Accel_RMS_Delta] = accel_rms_diff;
        eng_stat_bin_data.axis[eENG_STAT_Gyro_RMS_Delta] = gyro_rms_diff;
        eng_stat_bin_data.axis[eENG_STAT_Motion_Score] = alpha * accel_rms_diff + (1 - alpha) * gyro_rms_diff; // simple linear combination for motion score

        if(prev_accel_rms_delta > 0.0 && prev_gyro_rms_delta > 0.0 && prev_motion_score > 0.0) {
            eng_stat_bin_data.axis[eENG_STAT_Accel_RMS_Delta_Factor] = fabs(accel_rms_diff - prev_accel_rms_delta) / prev_accel_rms_delta; // percentage change in accel RMS delta
            eng_stat_bin_data.axis[eENG_STAT_Gyro_RMS_Delta_Factor] = fabs(gyro_rms_diff - prev_gyro_rms_delta) / prev_gyro_rms_delta; // percentage change in gyro RMS delta
            float motion_score_factor = fabs(eng_stat_bin_data.axis[eENG_STAT_Motion_Score] - prev_motion_score) / prev_motion_score; // percentage change in motion score
            eng_stat_bin_data.axis[eENG_STAT_Motion_Score_Factor] = motion_score_factor;
        } else {
            eng_stat_bin_data.axis[eENG_STAT_Accel_RMS_Delta_Factor] = accel_rms_diff;
            eng_stat_bin_data.axis[eENG_STAT_Gyro_RMS_Delta_Factor] = gyro_rms_diff;
            eng_stat_bin_data.axis[eENG_STAT_Motion_Score_Factor] = eng_stat_bin_data.axis[eENG_STAT_Motion_Score];
        }

        prev_accel_rms_delta = accel_rms_diff;
        prev_gyro_rms_delta = gyro_rms_diff;
        prev_motion_score = eng_stat_bin_data.axis[eENG_STAT_Motion_Score];
    } else {
        eng_stat_bin_data.axis[eENG_STAT_Accel_RMS_Delta] = accel_rms;
        eng_stat_bin_data.axis[eENG_STAT_Gyro_RMS_Delta] = gyro_rms;
        eng_stat_bin_data.axis[eENG_STAT_Motion_Score] = alpha * accel_rms + (1 - alpha) * gyro_rms; // initial motion score based on RMS values
    }

    getWorkerBinDataObj()->set_bin_data(get_index(), (void*)&eng_stat_bin_data);

    prev_accel_rms = accel_rms;
    prev_gyro_rms = gyro_rms;
    // reset in outage

    incr_index();

    return;
}

void ENG_STAT_worker::check_data_validity(){
    if( true == getWorkerBinDataObj()->is_data_outage(get_decision_index()) ) {
        getWorkerDataObj().incrDataOutageCnt();
    }
    return;
}