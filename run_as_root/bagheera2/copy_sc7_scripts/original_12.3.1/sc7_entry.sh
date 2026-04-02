#!/bin/sh

#For LTE
gpio_test -n 389 -o 0

#For ADC
rmmod ads7924.ko

#For IMU
rmmod inv_mpu_iio_i2c_icm20602 inv_mpu_iio_new
