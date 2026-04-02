/* Copyright (C) 2016 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Arun V <arun.vj@netradyne.com>, October 2016
 */

#ifndef IMU_DEV_H
#define IMU_DEV_H

#include <imu.h>

int  imu_dev_open();
bool imu_dev_close( int handle );

bool imu_dev_config( int handle, string key, string value );

bool imu_dev_enable_accel( int handle );
bool imu_dev_disable_accel( int handle );

bool imu_dev_enable_gyro( int handle );
bool imu_dev_disable_gyro( int handle );

bool imu_dev_enable_magneto( int handle );
bool imu_dev_disable_magneto( int handle );

bool imu_dev_reg_cb( int handle, Imu::imu_callback_t *cb );

#endif
