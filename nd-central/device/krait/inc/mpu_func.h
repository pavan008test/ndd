/******************************************************************************************************
 * @file       mpu_func.h
 * 
 * @brief      This file is  the include file for  MPU9250 api
 *
 * @author     Sherin Jasper.s (sherinjasper.s@vvdntech.in)
 *      
 ***********************************************************************************************************/
#ifndef __MPU_FUNC_H__
#define __MPU_FUNC_H__

#include <stdbool.h>

#define IMU_FD_OPEN_VALUE 1000
#define IMU_FD_CLOSE_VALUE -1

#define SYS_SAMPLING_FREQUENCY  "/sys/bus/iio/devices/iio:device1/sampling_frequency"
#define SYS_RAW_DATA_READ  "/sys/bus/iio/devices/iio:device1/raw_data_read"
#define SYS_TEMPERATURE  "/sys/bus/iio/devices/iio:device1/temperature"
#define SYS_GYRO_ENABLE  "/sys/bus/iio/devices/iio:device1/gyro_enable"
#define SYS_DMP_ON  "/sys/bus/iio/devices/iio:device1/dmp_on"
#define SYS_ACCEL_ENABLE  "/sys/bus/iio/devices/iio:device1/accel_enable"
#define SYS_COMPASS_ENABLE  "/sys/bus/iio/devices/iio:device1/compass_enable"
#define SYS_SELF_TEST  "/sys/bus/iio/devices/iio:device1/self_test"
#define SYS_INTERRUPT_ENABLE  "/sys/bus/iio/devices/iio:device1/interrupt_enable"
#define SYS_INTERRUPT_DISABLE  "/sys/bus/iio/devices/iio:device1/interrupt_disable"
#define SYS_BIAS_CALCULATION  "/sys/bus/iio/devices/iio:device1/bias_calculation"
#define SYS_IN_GYRO_SCALE  "/sys/bus/iio/devices/iio:device1/in_anglvel_scale"
#define SYS_IN_ACCEL_SCALE  "/sys/bus/iio/devices/iio:device1/in_accel_scale"
#define SYS_BIAS_SET  "/sys/bus/iio/devices/iio:device1/bias_set"
#define SYS_GYRO_X_CALIBBIAS  "/sys/bus/iio/devices/iio:device1/in_anglvel_x_calibbias"
#define SYS_GYRO_Y_CALIBBIAS  "/sys/bus/iio/devices/iio:device1/in_anglvel_y_calibbias"
#define SYS_GYRO_Z_CALIBBIAS  "/sys/bus/iio/devices/iio:device1/in_anglvel_z_calibbias"
#define SYS_ACCEL_X_CALIBBIAS  "/sys/bus/iio/devices/iio:device1/in_accel_x_calibbias"
#define SYS_ACCEL_Y_CALIBBIAS  "/sys/bus/iio/devices/iio:device1/in_accel_y_calibbias"
#define SYS_ACCEL_Z_CALIBBIAS  "/sys/bus/iio/devices/iio:device1/in_accel_z_calibbias"

#define BIAS_BIN "/bin/sys_imuwrite"

#define MPU_GYRO_LSB_250    131
#define MPU_GYRO_LSB_500    65.5
#define MPU_GYRO_LSB_1000   32.8
#define MPU_GYRO_LSB_2000   16.4

#define MPU_ACCEL_LSB_2 16384
#define MPU_ACCEL_LSB_4 8192
#define MPU_ACCEL_LSB_8 4096
#define MPU_ACCEL_LSB_16 2048


struct imu_data;
int imu_gyro_enable(int *imu_fd,int enable);
//int (*imu_call_back)(struct imu_data *);
extern bool isIMUMonitorThreadRunning;

struct imu_config
{
    int gyro_scale;
    int accel_scale;
    int sample_rate;
    float gyro_sensitivity;
    float accel_sensitivity;
};


/********************************************************************************
 * FUNCTION NAME:       imu_compass_enable                                          *
 * DESCRIPTION:         Enables and disables the compass                        *
 * ARGS:                enable 1-->turns on 0--> turns off		                *	
 * OUTPUT:              NONE                                                    *     
 * RETURN VALUE:        FAIL/SUCCESS                                            *
 * ******************************************************************************/
int imu_compass_enable(int *imu_fd,int enable);

/********************************************************************************
 * FUNCTION NAME:       imu_accel_enable                                            *
 * DESCRIPTION:         Enables and disables the accelerometer                  *
 * ARGS:                enable 1-->turns on 0--> turns off		                *	
 * OUTPUT:              NONE                                                    *     
 * RETURN VALUE:        FAIL/SUCCESS                                            *
 * ******************************************************************************/
int imu_accel_enable(int *imu_fd,int enable);

/********************************************************************************
 * FUNCTION NAME:       imu_dmp_enable                                              *
 * DESCRIPTION:         Enables and disables the dmp                            *
 * ARGS:                enable 1-->turns on 0--> turns off		                *	
 * OUTPUT:              NONE                                                    *     
 * RETURN VALUE:        FAIL/SUCCESS                                            *
 * ******************************************************************************/
int imu_dmp_enable(int *imu_fd,int enable);

/********************************************************************************
 * FUNCTION NAME:       self_test_enable                                         
 * DESCRIPTION:        Trigger gyro/accel/compass self-test for MPU9250
 *  On success/error, the self-test returns a mask representing the sensor(s)
 *  that failed. For each bit, a one (1) represents a "pass" case; conversely,
 *  a zero (0) indicates a failure.
 *
 *  \n The mask is defined as follows:
 *  \n Bit 0:   Gyro.
 *  \n Bit 1:   Accel.
 *  \n Bit 2:   Compass.
 *  @param      imu_fd ->To identify the IMU device
 *  @param      st_result 1->Do self test 0->Do self test for bias caluculation
 *  @return     Result mask (see above).
 * ******************************************************************************/
int imu_self_test_enable(int *imu_fd,int st_result);

/*Helper functions used to turn on the imu devices */
/********************************************************************************
 * FUNCTION NAME:       update_imu_data                                         *
 * DESCRIPTION:         Get the readings of all data  of compass,gyrometer and  *
 *                      accelerometer.                                          *
 * ARGS:                *data	                                                *	
 * OUTPUT:              NONE                                                    *     
 * RETURN VALUE:        FAIL/SUCCESS                                            *
 * ******************************************************************************/
int update_imu_data(struct imu_data *imu_global_data,int rawdata_fd);

/********************************************************************************
 * FUNCTION             gpio186_interrupt_thread_fn                             *
 * DESCRIPTION:         Helper function used  imu_register_cb 
 * ******************************************************************************/
void *gpio186_interrupt_thread_fn(void *arg);

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
 *                      place it in /bin/imu_bias.bin which can be used for further programming of FRU
 * ARGS:                imu_fd --> To identify the IMU app
 * RETURN VALUE:        FAIL/SUCCESS                                            *
 * ******************************************************************************/
int imu_read_from_sysfs_and_generate();
#endif
