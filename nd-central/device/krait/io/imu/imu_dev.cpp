/* Copyright (C) 2016 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Arun V <arun.vj@netradyne.com>, October 2016
 */

#include <cstdio>

#include <iostream>
#include <iomanip>
#include <sstream>
#include <sys/syscall.h>
#include <imu_dev.h>
#include <nd_msp_utils.h>
#include <system_utils.h>
#include <nd_task.h>

#include <log.h>
//#include <utils.h>
#include "nd_time.h" 
#include <config_parser.h>

#include <sys/time.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "mpu_api.h"

#ifdef __cplusplus
}
#endif

#define BAGHEERACONFIG_INI "/home/ubuntu/.nddevice/latest/bagheera_config.ini"
#define IMU_REG_BASE 0x68
#define REG_ADDR_ACCEL_CONFIG2 0x1d
#define IMU_I2C_CONTROLLER 1
#define REG_CONFIG 0x1A
#define REG_GYRO_CONFIG 0x1B

#define finit_module(fd, param_values, flags) syscall(__NR_finit_module, fd, param_values, flags)
#define delete_module(name, flags) syscall(__NR_delete_module, name, flags)

static Imu::imu_callback_t *cb=NULL;
static int imu_handle = -1;

static bool open_port();
static bool close_port();
static bool config_port();
static bool create_thread();
static void *read_thread( void *p );
static void parse(char *buf);

static int fd;
static volatile int enable_mask = 0;
static pthread_t imu_thread;
static bool thread_alive=false;

static const int accel_enable_mask = 1<<0;
static const int gyro_enable_mask = 1<<1;
static const int magneto_enable_mask = 1<<2;

static const string IMU_ICM20602_PATH = "/usr/lib/modules/4.9.160-perf/kernel/drivers/iio/imu/inv_mpu_20602/inv-mpu-iio-i2c-icm20602.ko";
static const string IMU_MPU_PATH = "/usr/lib/modules/4.9.160-perf/kernel/drivers/iio/imu/inv_mpu_20602/inv-mpu-iio.ko";
static const string IMU_ICM20602 = "inv-mpu-iio-i2c-icm20602";
static const string IMU_MPU = "inv-mpu-iio";

static int imu_enable_flag = 0;

// gyro_scale: 0 ->250dps ,1 ->500dps  ,2 ->1000dps ,3 ->2000dps
int gyro_scale = 1;
// accel_scale: 0->[-2g +2g] 1->[ -4g +4g ] 2--> [-8g +8g] 3-> [-16g +16g]
int accel_scale = 1;
// samples per seconds
// Sample rates available are 10,20,50,100,200,500
int sample_rate = 20;
// DLPF rates available are 460, 184, 92, 41, 20, 10, 5
int accel_dlpf_bw = 5;

//DLPF rates available are 250, 176, 92, 41, 20, 10, 5
int gyro_dlpf_bw = 5;
// Sign changes required
//int invert_accel_x = 1;
//int invert_accel_y = 1;
//int invert_accel_z = 1;
//int invert_gyro_x = 1;
//int invert_gyro_y = 1;
//int invert_gyro_z = 1;

const unsigned int temp_decimation_factor = sample_rate;
//#endif

// Sign changes required
int invert_accel_x = 1;
int invert_accel_y = 1;
int invert_accel_z = 1;
int invert_gyro_x = 1;
int invert_gyro_y = 1;
int invert_gyro_z = 1;

static const float FPS_THRESHOLD_FOR_IMU = 5.0;
static const int IMU_CHECK_INTERVAL_IN_SECS = 60;
static const int IMU_MODULE_INSERT_RETRY_COUNT = 3;
static const int IMU_MODULE_INSERT_TIMEOUT = 10;

pthread_t imu_recover_tid = -1;
pthread_mutex_t imu_count_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t imu_err_det_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t imu_recover_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t imu_recover_cond;


static bool imu_dev_enable( );
static bool imu_dev_disable( );
static int imu_callback_new ( struct imu_data *data_cb );

#define TAG "IMUD"

using namespace std;

static const int IMU_RECOVERY_TIME_MAX = 30;

static bool imu_recover_task(void *args) {
    LOG_I(TAG,"MSP reset getting called");

    nd_msp_reset();

    sleep(2);

    if(imu_handle != -1) {
        if( false == imu_dev_disable_accel(imu_handle) ) {
            LOG_E(TAG,"Cannot disable Accel");
        }

        if( false == imu_dev_disable_gyro(imu_handle) ) {
            LOG_E(TAG,"Cannot disable Gyro");
        }
    }

    imu_dev_close(imu_handle);
    sleep(2);

    LOG_I(TAG, "%s: recovering IMU started \n", __func__);

    nd_remove_kernel_module(IMU_ICM20602, IMU_MODULE_INSERT_RETRY_COUNT, IMU_MODULE_INSERT_TIMEOUT);
    nd_remove_kernel_module(IMU_MPU, IMU_MODULE_INSERT_RETRY_COUNT, IMU_MODULE_INSERT_TIMEOUT);

    sleep(1);

    nd_insert_kernel_module(IMU_MPU, IMU_MODULE_INSERT_RETRY_COUNT, IMU_MODULE_INSERT_TIMEOUT);
    nd_insert_kernel_module(IMU_ICM20602, IMU_MODULE_INSERT_RETRY_COUNT, IMU_MODULE_INSERT_TIMEOUT);

    sleep(2);

    // Logic to initialize IMU

    imu_dev_open();

    if( imu_handle == -1) {
        LOG_C(TAG,"Cannot open IMU !!");
    }
 
    sleep(2);

    int value=imu_register_cb(&imu_handle, imu_callback_new);
    if(value) {
        LOG_I(TAG, "Failed in imu_register_cb");
    }
    else {
        if(imu_handle != -1) {
            if( false == imu_dev_enable_accel(imu_handle) ) {
                LOG_C(TAG,"Cannot enable Accel");
            }
            if( false == imu_dev_enable_gyro(imu_handle) ) {
                LOG_C(TAG,"Cannot enable gyro");
            }
        }
        else {
            LOG_I(TAG, "IMu callback is not registered");
        }
    }
    LOG_I(TAG, "%s: recovering IMU done \n", __func__);
    return true;

}

void *imu_recover_thread(void *args) {

    struct timespec time_to_wait = {5, 0};
    struct timeval now;

    while(1) {
        gettimeofday(&now, NULL);
        time_to_wait.tv_sec = now.tv_sec + 60;
        pthread_mutex_lock(&imu_recover_mutex);
        if(ETIMEDOUT == pthread_cond_timedwait(&imu_recover_cond, &imu_recover_mutex, &time_to_wait)) {
            LOG_D(TAG, "%s: hitting timer \n", __func__);
            pthread_mutex_unlock(&imu_recover_mutex);
        }
        else {

            pthread_mutex_unlock(&imu_recover_mutex);

            task_result_t task_result = nd_timed_task(imu_recover_task,
                    IMU_RECOVERY_TIME_MAX, NULL, "imu_recovery");
            if (task_result != TASK_SUCCESS) {
                LOG_E (TAG, "nd_timed_task for imu_recover_task timedout");
            }

        }
    }
}

static bool update_from_config() {
    // Marks which all cameras need to be enabled
    // after reading config file.
    Config_parser c(BAGHEERACONFIG_INI);
    bool get_override_val = true;
    bool is_val_overridden = false;
    if (c.getParseStatus() != true)
        return false;


    if (c.isPresent ("imu","gyro_scale"))
    {
        int grate;
        string_to_integer(c.getConfig("imu","gyro_scale","1", get_override_val, is_val_overridden), grate);
        if (grate >= 0 && grate <= 3) {
                gyro_scale = grate;
        }
        else
             LOG_I (TAG, "No valid gyro_scale config found, using default: %d", gyro_scale);
    }
    else
    {
        LOG_I (TAG, "No gyro_scale found, using default: %d", gyro_scale);
    }

    if (c.isPresent ("imu","accel_scale"))
    {
        bool get_override_val = true;
        bool is_val_overridden = false;
        int arate;
        string_to_integer(c.getConfig("imu","accel_scale","1", get_override_val, is_val_overridden), arate);
        if (arate >= 0 && arate <= 3) {
                accel_scale = arate;
        }
        else
             LOG_I (TAG, "No valid accel_scale config found, using default: %d", accel_scale);
    }
    else
    {
        LOG_I (TAG, "No accel_scale found, using default: %d", accel_scale);
    }

    if (c.isPresent ("imu","sample_rate"))
    {
        int srate;
        string_to_integer(c.getConfig("imu","sample_rate","20", get_override_val, is_val_overridden), srate);

        if (srate == 10 || srate == 20 || srate == 50 || srate == 100 || srate == 200 || srate == 500)
            sample_rate = srate;
        else
            LOG_I (TAG, "No valid sample_rate found, using default: %d", sample_rate);
    }
    else
    {
        LOG_I (TAG, "No sample-rate found, using default: %d", sample_rate);
    }

    if (c.isPresent ("imu","accel_dlpf_bw"))
    {
        int drate;
        string_to_integer(c.getConfig("imu","accel_dlpf_bw","5", get_override_val, is_val_overridden), drate);
        if (drate >= 1 && drate <= 7)
            accel_dlpf_bw = drate;
        else
             LOG_I (TAG, "No valid accel_dlpf_bw found, using default: %d", accel_dlpf_bw);
    }
    else
    {
        LOG_I (TAG, "No accel_dlpf_bw found, using default: %d", accel_dlpf_bw);
    }

    if (c.isPresent ("imu", "invert_accel_x"))
    {
        string str_invert_accel_x = c.getConfig("imu", "invert_accel_x","", get_override_val, is_val_overridden);
        if (str_invert_accel_x == "true"){
		invert_accel_x = -1; 
        }
    }
    if (c.isPresent ("imu", "invert_accel_y"))
    {
       string str_invert_accel_y = c.getConfig("imu", "invert_accel_y","", get_override_val, is_val_overridden);
        if (str_invert_accel_y == "true"){
		invert_accel_y = -1; 
        }
    }
    if (c.isPresent ("imu", "invert_accel_z"))
    {
        string str_invert_accel_z = c.getConfig("imu", "invert_accel_z","", get_override_val, is_val_overridden);
        if (str_invert_accel_z == "true"){
		invert_accel_z = -1; 
        }
    }
    if (c.isPresent ("imu", "invert_gyro_x"))
    {
        string str_invert_gyro_x = c.getConfig("imu", "invert_gyro_x","", get_override_val, is_val_overridden);
        if (str_invert_gyro_x == "true"){
		invert_gyro_x = -1; 
        }
    }
    if (c.isPresent ("imu", "invert_gyro_y"))
    {
        string str_invert_gyro_y = c.getConfig("imu", "invert_gyro_y","", get_override_val, is_val_overridden);
        if (str_invert_gyro_y == "true"){
		invert_gyro_y = -1; 
        }
    }
    if (c.isPresent ("imu", "invert_gyro_z"))
    {
        string str_invert_gyro_z = c.getConfig("imu", "invert_gyro_z","", get_override_val, is_val_overridden);
        if (str_invert_gyro_z == "true"){
		invert_gyro_z = -1; 
        }
    }

    LOG_I(TAG, "Sample Rate: %d", sample_rate);
    LOG_I(TAG, "Accel DLPF BW: %d", accel_dlpf_bw);
    LOG_I(TAG, "Accel Scale", accel_scale);
    LOG_I(TAG, "Gyro Scale: %d", gyro_scale);
    LOG_I(TAG, "Gyro DLPF BW: %d", gyro_dlpf_bw);
    LOG_I(TAG, "Invert Accel: x %d y %d z %d", invert_accel_x, invert_accel_y, invert_accel_z );
    LOG_I(TAG, "Invert Gyro: x %d y %d z %d", invert_gyro_x, invert_gyro_y, invert_gyro_z );

    return true;
}

int imu_dev_open()
{
    int value;
    value = imu_open(&imu_handle);
    if( IMU_SUCCESS != value ) {
        LOG_E(TAG, "Failed in imu_open");
        return -1;
    }

    update_from_config();


    if(imu_recover_tid == -1) {
        if (!pthread_create (&imu_recover_tid, NULL, imu_recover_thread, NULL)) {
            LOG_I (TAG, "Thread created for IMU recovery");
        }
    }

    return imu_handle;
}

bool imu_dev_close(int handle) {

    if( handle != imu_handle ) {
        return false;
    }

    if( IMU_SUCCESS != imu_close(&imu_handle) ) {
        return false;
    }

    return true;
}


bool imu_dev_config( int handle, string key, string value ) {

    if( handle != imu_handle ) {
        return false;
    }

    return true;
}

bool set_accel_dlpf_bw() {

    FILE *pipe;
    string cmd = "i2cget -f -y 1 0x68 0x1d";
    if( NULL == (pipe = popen(cmd.c_str(),"r") ) ) {
        LOG_E(TAG, "Cannot read accel dlpf value");
        return false;
    }

    char buff[10];
    int read_val = 0;

    while (fgets(buff, sizeof(buff), pipe) != NULL) {
        LOG_I(TAG, "Detected port: %s", buff);
        read_val = strtol(buff,NULL,16);
    }

    pclose(pipe);

    read_val &= ~7; //clear last three bits
    stringstream writecmd;
    writecmd << "i2cset -f -y 1 " << IMU_REG_BASE << " " << REG_ADDR_ACCEL_CONFIG2 << " " << (read_val | accel_dlpf_bw);
    system(writecmd.str().c_str());

    return true;
}

bool set_gyro_dlpf_bw() {

    FILE *pipe;
    stringstream readcmd;
    stringstream writecmd;

    char buff[10] = {0};
    int read_reg_val = 0;
    int read_gyro_val = 0;

    readcmd << "i2cget -f -y " << IMU_I2C_CONTROLLER << " " << IMU_REG_BASE << " " << REG_GYRO_CONFIG;
    if( NULL == (pipe = popen(readcmd.str().c_str(),"r") ) ) {
        LOG_E(TAG, "Cannot read accel dlpf value");
        return false;
    }
    LOG_I(TAG, "gyro_dlpf_bw readcmd = %s", readcmd.str().c_str());
    while (fgets(buff, sizeof(buff), pipe) != NULL) {
        LOG_I(TAG, "Detected port: %s", buff);
        read_gyro_val = strtol(buff,NULL,16);
    }

    pclose(pipe);
    memset(buff, 0, sizeof(buff));

    read_gyro_val &= ~3; //clear last two bits
    writecmd << "i2cset -f -y " << IMU_I2C_CONTROLLER << " " << IMU_REG_BASE << " " << REG_GYRO_CONFIG << " " << read_gyro_val;
    system(writecmd.str().c_str());
    LOG_I(TAG, "gyro_dlpf_bw writecmd = %s", writecmd.str().c_str());

    readcmd.str("");
    writecmd.str("");

    readcmd << "i2cget -f -y " << IMU_I2C_CONTROLLER << " " << IMU_REG_BASE << " " << REG_CONFIG;
    if( NULL == (pipe = popen(readcmd.str().c_str(),"r") ) ) {
        LOG_E(TAG, "Cannot read accel dlpf value");
        return false;
    }
    LOG_I(TAG, "gyro_dlpf_bw readcmd = %s", readcmd.str().c_str());
    while (fgets(buff, sizeof(buff), pipe) != NULL) {
        LOG_I(TAG, "Detected port: %s", buff);
        read_reg_val = strtol(buff,NULL,16);
    }

    pclose(pipe);

    read_reg_val &= ~7; //clear last three bits
    writecmd << "i2cset -f -y " << IMU_I2C_CONTROLLER << " " << IMU_REG_BASE << " " << REG_CONFIG << " " << (read_reg_val | gyro_dlpf_bw);
    system(writecmd.str().c_str());
    LOG_I(TAG, "gyro_dlpf_bw writecmd = %s", writecmd.str().c_str());

    return true;
}

bool imu_dev_enable_accel(int handle) {

    if( handle != imu_handle ) {
        return false;
    }

    if( false == imu_dev_enable() ) {
        return false;
    }

    if( false == set_accel_dlpf_bw() ) {
        return false;
    } 

    imu_enable_flag |= accel_enable_mask;

    return true;
}

bool imu_dev_enable_gyro(int handle) {

    if( handle != imu_handle ) {
        return false;
    }

    if( false == imu_dev_enable() ) {
        return false;
    }

    if( false == set_gyro_dlpf_bw() ) { 
        return false;
    } 

    imu_enable_flag |= gyro_enable_mask;

    return true;
}

bool imu_dev_enable_magneto( int handle ) {

    if( handle != imu_handle ) {
        return false;
    }

    if( false == imu_dev_enable() ) {
        return false;
    } 

    imu_enable_flag |= magneto_enable_mask;

    return true;
}

bool imu_dev_disable_accel( int handle ) {

    if( handle != imu_handle ) {
        return false;
    }

    imu_enable_flag &= (~accel_enable_mask);

    if( false == imu_dev_disable() ) {
        return false;
    }

    return true;
}

bool imu_dev_disable_gyro(int handle) {

    if( handle != imu_handle ) {
        return false;
    }

    imu_enable_flag &= (~gyro_enable_mask);

    if( false == imu_dev_disable() ) {
        return false;
    }

    return true;
}

bool imu_dev_disable_magneto(int handle) {

    if( handle != imu_handle ) {
        return false;
    }

    imu_enable_flag &= (~magneto_enable_mask);

    if( false == imu_dev_disable() ) {
        return false;
    }

    return true;
}

static int imu_callback_new (struct imu_data *data_cb)
{
    Imu::val_t val_a, val_g, val_m;
    uint64_t time = get_system_time_ns();
    uint64_t monotonic_time = get_system_monotonic_time_ns();

    // changing sign and axis to align with Analytics
    val_a.x = data_cb->accel_z * 9.80665f;
    // to address the issue discussed in JIRA::BHAGEERA-439
    // observed that we need to swap left and right to inline with akhela implementation
    val_a.y = data_cb->accel_y * 9.80665f * -1;
    val_a.z = data_cb->accel_x * 9.80665f;

    val_g.x = data_cb->gyro_z;
    val_g.y = data_cb->gyro_y;
    val_g.z = data_cb->gyro_x;

    //val_m.x = data_cb->compass_z;
    //val_m.y = data_cb->compass_y;
    //val_m.z = data_cb->compass_x;

    val_a.clock_time = time;
    val_g.clock_time = time;
    val_m.clock_time = time;

    val_a.raw_time = monotonic_time;
    val_g.raw_time = monotonic_time;
    val_m.raw_time = monotonic_time;

    val_a.x = invert_accel_x * val_a.x;
    val_a.y = invert_accel_y * val_a.y;
    val_a.z = invert_accel_z * val_a.z;
    
    val_g.x = invert_gyro_x * val_g.x;
    val_g.y = invert_gyro_y * val_g.y;
    val_g.z = invert_gyro_z * val_g.z;

   // send a 1 message to ndc and seperate there
    if (imu_enable_flag)
        cb(val_a, val_g, val_m);

    return true;
}

bool imu_dev_reg_cb( int handle, Imu::imu_callback_t *cb ) {

    if( handle != imu_handle ) {
        return false;
    }

    if( cb == NULL ) {
        return false;
    }

    ::cb = cb;

    int value=imu_register_cb(&imu_handle, imu_callback_new);
    if(value) {
        LOG_I(TAG, "Failed in imu_register_cb");
        pthread_mutex_lock(&imu_recover_mutex);
        pthread_cond_signal(&imu_recover_cond);
        pthread_mutex_unlock(&imu_recover_mutex);

        return false;
    }

    return true;
}


static bool imu_dev_enable( ) {
    if( 0 == imu_enable_flag ) {
        if( IMU_SUCCESS != set_sample_rate( &imu_handle, sample_rate ) ) {
            return false;
        }

        if( IMU_SUCCESS != imu_enable( &imu_handle, gyro_scale, accel_scale ) ) {
            return false;
        }
    }
    return true;
}

static bool imu_dev_disable( ) {
    if( 0 == imu_enable_flag ) {
        if( IMU_SUCCESS != imu_disable( &imu_handle ) ) {
            return false;
        }
    }

    return true;
}


