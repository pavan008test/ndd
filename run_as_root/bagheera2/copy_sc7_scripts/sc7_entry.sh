#!/bin/sh
#OUTCAM
gpio_test -n 456 -o 0 #CAM0_PWR
gpio_test -n 461 -o 0 #CAM0_RST

#For wifi and external eMMc
rmmod brcmfmac.ko brcmutil.ko cfg80211.ko compat.ko
echo -n "3440000.sdhci" > /sys/bus/platform/drivers/sdhci-tegra/unbind

#For LTE
gpio_test -n 389 -o 0

#For ADC
rmmod ads7924.ko

#For IMU
gpio_test -n 298 -o 0 #MOTION_INT
rmmod inv_mpu_iio_i2c_icm20602 inv_mpu_iio_new

#OBD
gpio_test -n 255 -o 0 #OBD RESET
gpio_test -n 303 -o 0 #MC_JTX1_STATUS
#For Side CAM
gpio_test -n 457 -o 0
  
#For INCAM
gpio_test -n 277 -o 0 #Reset
sleep 0.02
gpio_test -n 426 -o 1 #PWDN
sleep 0.02
gpio_test -n 393 -o 0 #Power
