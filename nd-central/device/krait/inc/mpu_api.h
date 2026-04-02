/******************************************************************************************************
 * @file      : mpu_api.h
 *
 * @brief     : This file is the include file for ICM20602 api
 *
 * @author    : Varshini Rajendran (varshini.rajendran@vvdntech.in)
 *
 ***********************************************************************************************************/
#ifndef __MPU_API_H__
#define __MPU_API_H__

#include "mpu_func.h"

#define IMU_DEBUG 0

#define IMU_SUCCESS 0
#define IMU_FAILURE -1
#define SELF_TEST_PASS_RESULT 3

#define GYRO_SCALE_250  0
#define GYRO_SCALE_500  1
#define GYRO_SCALE_1000 2
#define GYRO_SCALE_2000 3

#define ACCEL_SCALE_2 0
#define ACCEL_SCALE_4 1
#define ACCEL_SCALE_8 2
#define ACCEL_SCALE_16 3

#define SAMPLE_RATE_500 500
#define SAMPLE_RATE_200 200
#define SAMPLE_RATE_100 100
#define SAMPLE_RATE_50 50
#define SAMPLE_RATE_20 20
#define SAMPLE_RATE_10 10

struct imu_data{
float gyro_x;
float gyro_y;
float gyro_z;
float accel_x;
float accel_y;
float accel_z;
unsigned long long gyro_time;
unsigned long long accel_time;
};

extern struct imu_data imu_global_data;
extern int imu_fd;
extern struct imu_config imu_config_data;
int poll_mode, sync_mode, wom_mode;

/********************************************************************************
 * FUNCTION NAME:       imu_open
 * DESCRIPTION:         Handler call for the imu_device  open
 * ARGS:                imu_fd ->handle for the mpu_api
 * OUTPUT:              fills the imu_fd with the value 1000
 * RETURN VALUE:        returns the imu_fd
 * ******************************************************************************/
int imu_open(int *imu_fd);

/********************************************************************************
 * FUNCTION NAME:       imu_close
 * DESCRIPTION:         Handler call for the imu_device close
 * ARGS:                imu_fd ->Handle for the mpu_api
 * OUTPUT:              Fill the imu_fd with the value
 * RETURN VALUE:        SUCESS/FALIURE
 * ******************************************************************************/
int imu_close(int *imu_fd);

/********************************************************************************
 * FUNCTION NAME:       imu_enable
 * DESCRIPTION:         Enables the sensor in 6 axis mode
 * ARGS:                imu_fd ->handler to identify the mpu_device
                        gyro_scale is 0->+250dps 1->+500dps 2-->+1000dps 3->+2000dps
                        accel_scale is 0->[-2g +2g] 1->[ -4g +4g ] 2--> [-8g +8g] 3-> [-16g +16g]
 * OUTPUT:              Enable the imu_sensor
 * RETURN VALUE:        FAIL/SUCCESS
 * ******************************************************************************/
int imu_enable(int *imu_fd,int gyro_scale, int accel_scale);

/******************************************************************************************************
 * FUNCTION NAME:       imu_disable                                                                    *
 * DESCRIPTION:         Disables the sensor in 6 axis mode and return to sleep mode                    *
 * ARGS:                imu_fd -> handler to identify the mpu_device                                   *
 * OUTPUT:              Disable the imu_sensor                                                         *
 * RETURN VALUE:        FAIL/SUCCESS                                                                   *
 * ****************************************************************************************************/
int imu_disable(int *imu_fd);

/********************************************************************************
 * FUNCTION NAME:       set_sample_rate                                         *
 * DESCRIPTION:         Sets the sample rate divider value                      *
                        Allowed sample rates are 10 20 50 100 200 500           *
 * ARGS:                sample_rate_div_value                                   *
 * OUTPUT:              NONE                                                    *
 * RETURN VALUE:        FAIL/SUCCESS                                            *
 * ******************************************************************************/
int set_sample_rate(int *imu_fd,int sample_rate_div_value);

/********************************************************************************
 * FUNCTION NAME:       imu_register_cb
 * DESCRIPTION:         Registers the callback with the callback function passed and
 *                      creates a thread which can read the data when the interrupt triggers
 *                      and pass the values in the callback function
 * ARGS:                imu_fd -> HAndle to ensure the als device functions
 *                      imu_read_cb ->callback fuction to be handled which accepts one arguments
                        Argument 1-- > Structure where the updated values will be present

 * OUTPUT:              Callback function will be called once the interrupt triggers.
 * RETURN VALUE:        SUCEESS/FAILURE
 * ******************************************************************************/

int imu_register_cb(int *imu_fd,int (*imu_call_back_fn)(struct imu_data *));

/********************************************************************************
 * FUNCTION NAME:       imu_read_data
 * DESCRIPTION:         Read the latest values of gyro, accelerometer
 * ARGS:                imu_fd -> HAndle to ensure the als device functions
                        user_data ->structure pointer to update with latest value
 * OUTPUT:              Returns the updated structure
 * RETURN VALUE:        SUCCESS
 * ******************************************************************************/
int imu_read_data(int *imu_fd,struct imu_data *user_data);

/**************************************************************************************************
 * FUNCTION NAME:       imu_temp_read
 * DESCRIPTION:         Reads the  temperature and timestamp from imu sensor
 * ARGS:                imu_fd->To ensure imu_fd
                        temp_data-> Pointer to update temperature
                        temp_timestamp-> Pointer to  temperature timestamp
 * OUTPUT:              NONE
 * RETURN VALUE:        FAIL/SUCCESS
 * *************************************************************************************************/
int imu_temp_read(int *imu_fd ,long *temp_data, unsigned long long *temp_timestamp);

/********************************************************************************
 * FUNCTION NAME:       imu_enable_poll_mode
 * DESCRIPTION:         Enables the poll mode for data read
 * ARGS:                imu_fd ->handler to identify the mpu_device
 * OUTPUT:              Enable the imu_sensor poll mode
 * RETURN VALUE:        FAIL/SUCCESS
 * ******************************************************************************/
int imu_enable_poll_mode(int *imu_fd);

/********************************************************************************
 * FUNCTION NAME:       imu_disable_poll_mode
 * DESCRIPTION:         Disable the poll mode for data read
 * ARGS:                imu_fd ->handler to identify the mpu_device
 * OUTPUT:              Disables the imu_sensor poll mode
 * RETURN VALUE:        FAIL/SUCCESS
 * ******************************************************************************/
int imu_disable_poll_mode(int *imu_fd);

/********************************************************************************
 * FUNCTION NAME:       imu_enable_wom_mode
 * DESCRIPTION:         Enable the wake on motion mode
 * ARGS:                imu_fd ->handler to identify the mpu_device
 * OUTPUT:              Enables Wake on motion interrupt and enables low power
 *						mode in IMU sensor
 * RETURN VALUE:        FAIL/SUCCESS
 * ******************************************************************************/
int imu_wom_enable(int *imu_fd, int x_threshold, int y_threshold,int z_threshold, int wakeup_freq);

/********************************************************************************
 * FUNCTION NAME:       imu_disable_wom_mode
 * DESCRIPTION:         Disable the wake on motion mode
 * ARGS:                imu_fd ->handler to identify the mpu_device
 * OUTPUT:              Disables Wake on motion interrupt and disables low power
 *						mode in IMU sensor
 * RETURN VALUE:        FAIL/SUCCESS
 * ******************************************************************************/
int imu_wom_disable(int *imu_fd);

/********************************************************************************
 * FUNCTION NAME:       imu_sync_mode_enable
 * DESCRIPTION:         Enable the sync mode
 * ARGS:                imu_fd ->handler to identify the mpu_device
 * OUTPUT:              Enables sync mode in IMU sensor
 * RETURN VALUE:        FAIL/SUCCESS
 * ******************************************************************************/
int imu_sync_mode_enable(int *imu_fd);

/********************************************************************************
 * FUNCTION NAME:       imu_sync_mode_disable
 * DESCRIPTION:         Disable the sync mode
 * ARGS:                imu_fd ->handler to identify the mpu_device
 * OUTPUT:              Disables sync mode in IMU sensor
 * RETURN VALUE:        FAIL/SUCCESS
 * ******************************************************************************/
int imu_sync_mode_disable(int *imu_fd);

#endif
