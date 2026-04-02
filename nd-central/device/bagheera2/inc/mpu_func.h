/******************************************************************************************************
 * @file      : mpu_func.h
 *
 * @brief     : This file is the include file for ICM20602 api
 *
 * @author    : Varshini Rajendran (varshini.rajendran@vvdntech.in)
 *
 ***********************************************************************************************************/
#ifndef __MPU_FUNC_H__
#define __MPU_FUNC_H__

#include <stdbool.h>

#define IMU_FD_OPEN_VALUE 1000
#define IMU_FD_CLOSE_VALUE -1

#define SYS_SAMPLING_FREQUENCY  "/sys/bus/iio/devices/iio:device2/sampling_frequency"
#define SYS_RAW_DATA_READ  "/sys/bus/iio/devices/iio:device2/raw_data_read"
#define SYS_BUFFERED_RAW_DATA_READ  "/sys/bus/iio/devices/iio:device2/buffered_raw_data_read"
#define SYS_TEMPERATURE  "/sys/bus/iio/devices/iio:device2/temperature"
#define SYS_GYRO_ENABLE  "/sys/bus/iio/devices/iio:device2/gyro_enable"
#define SYS_ACCEL_ENABLE  "/sys/bus/iio/devices/iio:device2/accel_enable"
#define SYS_SELF_TEST  "/sys/bus/iio/devices/iio:device2/self_test"
#define SYS_INTERRUPT_ENABLE  "/sys/bus/iio/devices/iio:device2/interrupt_enable"
#define SYS_INTERRUPT_DISABLE  "/sys/bus/iio/devices/iio:device2/interrupt_disable"
#define SYS_BIAS_CALCULATION  "/sys/bus/iio/devices/iio:device2/bias_calculation"
#define SYS_IN_GYRO_SCALE  "/sys/bus/iio/devices/iio:device2/in_anglvel_scale"
#define SYS_IN_ACCEL_SCALE  "/sys/bus/iio/devices/iio:device2/in_accel_scale"
#define SYS_BIAS_SET  "/sys/bus/iio/devices/iio:device2/bias_set"
#define SYS_GYRO_X_CALIBBIAS  "/sys/bus/iio/devices/iio:device2/in_anglvel_x_calibbias"
#define SYS_GYRO_Y_CALIBBIAS  "/sys/bus/iio/devices/iio:device2/in_anglvel_y_calibbias"
#define SYS_GYRO_Z_CALIBBIAS  "/sys/bus/iio/devices/iio:device2/in_anglvel_z_calibbias"
#define SYS_ACCEL_X_CALIBBIAS  "/sys/bus/iio/devices/iio:device2/in_accel_x_calibbias"
#define SYS_ACCEL_Y_CALIBBIAS  "/sys/bus/iio/devices/iio:device2/in_accel_y_calibbias"
#define SYS_ACCEL_Z_CALIBBIAS  "/sys/bus/iio/devices/iio:device2/in_accel_z_calibbias"
#define SYS_POLL_MODE_DATA "/sys/bus/iio/devices/iio:device2/poll_mode_read_data"
#define SYS_SYNC_MODE_CONFIG  "/sys/bus/iio/devices/iio:device2/sync_mode_config"
#define SYS_WOM_MODE_CONFIG  "/sys/bus/iio/devices/iio:device2/wom_mode_config"
#define SYS_WOM_MODE_THRESHOLD_X  "/sys/bus/iio/devices/iio:device2/wom_mode_threshold_x"
#define SYS_WOM_MODE_THRESHOLD_Y  "/sys/bus/iio/devices/iio:device2/wom_mode_threshold_y"
#define SYS_WOM_MODE_THRESHOLD_Z  "/sys/bus/iio/devices/iio:device2/wom_mode_threshold_z"
#define SYS_WOM_MODE_FREQ  "/sys/bus/iio/devices/iio:device2/wom_mode_wakeup_freq"
#define SYS_READ_NOTIFY "/sys/bus/iio/devices/iio:device2/read_notify"
#define SYS_BUFF_READ_FLAG "/sys/module/inv_mpu_iio_i2c_icm20602/parameters/buffered_read"

#define BIAS_BIN "/bin/vendor/sys_imuwrite"
#define SYS_NOTIFY "/sys/bus/iio/devices/iio:device2/read_notify"
#define MPU_GYRO_LSB_250    131
#define MPU_GYRO_LSB_500    65.5
#define MPU_GYRO_LSB_1000   32.8
#define MPU_GYRO_LSB_2000   16.4

#define MPU_ACCEL_LSB_2 16384
#define MPU_ACCEL_LSB_4 8192
#define MPU_ACCEL_LSB_8 4096
#define MPU_ACCEL_LSB_16 2048
#define RW_FILE_PERM    0666
#define MPU_FSYNC_DEBUG_LOG 0
struct imu_data;
int imu_gyro_enable(int *imu_fd,int enable);
int (*imu_call_back)(struct imu_data *);
extern bool isIMUMonitorThreadRunning;

struct imu_config
{
    int gyro_scale;
    int accel_scale;
    int sample_rate;
    int accel_lpf;
    int gyro_lpf;
    float gyro_sensitivity;
    float accel_sensitivity;
};

enum inv_filter_accel {
	INV_FILTER_ACCEL_0_460HZ = 0,
	INV_FILTER_ACCEL_1_184HZ,
	INV_FILTER_ACCEL_2_92HZ,
	INV_FILTER_ACCEL_3_41HZ,
	INV_FILTER_ACCEL_4_20HZ,
	INV_FILTER_ACCEL_5_10HZ,
	INV_FILTER_ACCEL_6_5HZ,
	INV_FILTER_ACCEL_7_460HZ,
};

enum inv_filter_e {
	INV_FILTER_256HZ_NOLPF2 = 0,
	INV_FILTER_188HZ,
	INV_FILTER_98HZ,
	INV_FILTER_42HZ,
	INV_FILTER_20HZ,
	INV_FILTER_10HZ,
	INV_FILTER_5HZ,
	INV_FILTER_2100HZ_NOLPF,
};

/********************************************************************************
 * FUNCTION NAME:       imu_accel_enable                                            *
 * DESCRIPTION:         Enables and disables the accelerometer                  *
 * ARGS:                enable 1-->turns on 0--> turns off		                *
 * OUTPUT:              NONE                                                    *
 * RETURN VALUE:        FAIL/SUCCESS                                            *
 * ******************************************************************************/
int imu_accel_enable(int *imu_fd,int enable);

/********************************************************************************
 * FUNCTION NAME:       self_test_enable
 * DESCRIPTION:        Trigger gyro/accel self-test for ICM20602
 *  On success/error, the self-test returns a mask representing the sensor(s)
 *  that failed. For each bit, a one (1) represents a "pass" case; conversely,
 *  a zero (0) indicates a failure.
 *
 *  \n The mask is defined as follows:
 *  \n Bit 0:   Gyro.
 *  \n Bit 1:   Accel.
 *  @param      imu_fd ->To identify the IMU device
 *  @param      st_result 1->Do self test 0->Do self test for bias caluculation
 *  @return     Result mask (see above).
 * ******************************************************************************/
int imu_self_test_enable(int *imu_fd,int st_result);

/*Helper functions used to turn on the imu devices */

/********************************************************************************
 * FUNCTION             imu_interrupt_thread_fn                             *
 * DESCRIPTION:         Helper function used  imu_register_cb
 * ******************************************************************************/
void *imu_interrupt_thread_fn(void *arg);

/********************************************************************************
 * FUNCTION NAME:       power_enable                                            *
 * DESCRIPTION:         Enables and disables the power                          *
                        -->dummy turn on id used in driver                      *
 * ARGS:                enable 1-->turns on 0--> turns off		                *
 * OUTPUT:              NONE                                                    *
 * RETURN VALUE:        FAIL/SUCCESS                                            *
 * ******************************************************************************/
int power_enable(int *imu_fd,int enable);

/********************************************************************************
 * FUNCTION NAME:       master_enable                                           *
 * DESCRIPTION:         Enables and disables the sensor.                        *
                        -->Kind of master control                               *
 * ARGS:                enable 1-->turns on 0--> turns off		                *
 * OUTPUT:              NONE                                                    *
 * RETURN VALUE:        FAIL/SUCCESS                                            *
 * ******************************************************************************/
int master_enable(int *imu_fd,int enable);

/********************************************************************************
 * FUNCTION NAME:       interrupt_enable                                           *
 * DESCRIPTION:         Enables and disables the enable.                        *
 * ARGS:                enable 1-->turns on 0--> turns off		                *
 * OUTPUT:              NONE                                                    *
 * RETURN VALUE:        FAIL/SUCCESS                                            *
 * ******************************************************************************/
int interrupt_enable(int *imu_fd,int enable);

int write_sysfs_int_and_verify(char *filename, char *basedir, int val);

/********************************************************************************
 * FUNCTION NAME:       set_gyro_scale
 * DESCRIPTION:         Sets the gyro scale
                        0->+250dps 1->+500dps 2-->+1000dps 3->+2000dps
 * ARGS:                gyro_scale
 * OUTPUT:              NONE                                                    *
 * RETURN VALUE:        FAIL/SUCCESS                                            *
 * ******************************************************************************/
int set_gyro_scale(int *imu_fd,int gyro_scale);

/********************************************************************************
 * FUNCTION NAME:       set_accel_scale
 * DESCRIPTION:         Sets the gyro scale
                        0->[-2g +2g] 1->[ -4g +4g ] 2--> [-8g +8g] 3-> [-16g +16g]
 * ARGS:                accel_scale
 * OUTPUT:              NONE                                                    *
 * RETURN VALUE:        FAIL/SUCCESS                                            *
 * ******************************************************************************/
int set_accel_scale(int *imu_fd,int accel_scale);

/********************************************************************************
 * FUNCTION NAME:       imu_read_bias_from_fru
 * DESCRIPTION:         Read the bias values from FRU and store.
 * ARGS:                *gyro_bias, *accel_bias
 * RETURN VALUE:        FAIL/SUCCESS                                            *
 * ******************************************************************************/
int imu_read_bias_from_fru(long *gyro_bias,long *accel_bias);

/********************************************************************************
 * FUNCTION NAME:       imu_write_bias_to_sysfs_and_set
 * DESCRIPTION:         Write the IMU Bias which is read from FRU to Sysfs entries ans set the offset registers
 * ARGS:                imu_fd --> To identify the IMU app
 * RETURN VALUE:        FAIL/SUCCESS                                            *
 * ******************************************************************************/
int imu_write_bias_to_sysfs_and_set(int *imu_fd);

/********************************************************************************
 * FUNCTION NAME:       imu_read_from_sysfs_and_generate
 * DESCRIPTION:         Caluculate the bias values and generate the bin and
 *                      place it in /bin/vendor/imu_bias.bin which can be used for further programming of FRU
 * ARGS:                imu_fd --> To identify the IMU app
 * RETURN VALUE:        FAIL/SUCCESS                                            *
 * ******************************************************************************/
int imu_read_from_sysfs_and_generate();

/*******************************************************************************************
 * FUNCTION NAME:       set_dlpf_rate                                                       *
 * DESCRIPTION:         Sets the Accel and Gyro DLPF settings                               *
 * ARGS:                gyro_lpf 0-250 1-184 2-92 3-41 4-20 5-10 6-5 7-3600(In HZ)          *
 * ARGS:                accel_lpf 0-460 1-184 2-92 3-41 4-20 5-10 6-5 7-460 (In HZ)         *
 * OUTPUT:              Sets the LPF settings of Accel and Gyro                             *
 * RETURN VALUE:        FAIL/SUCCESS                                                        *
 * ******************************************************************************************/
int set_dlpf_rate(int *imu_fd,int gyro_lpf , int accel_lpf);

/*******************************************************************************************
 * FUNCTION NAME:       get_buff_read_flag                                                  *
 * DESCRIPTION:         get the buffered read flag for imu module                           *
 * OUTPUT:              bufferred read flag value of imu module                             *
 * RETURN VALUE:        FAIL/SUCCESS                                                        *
 * ******************************************************************************************/
int get_buff_read_flag();
#endif
