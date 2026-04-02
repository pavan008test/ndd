#!/bin/sh

#For FAN
rpm=`cat /sys/devices/pwm-fan/target_pwm`
if [ $rpm -eq 0 ];then
    echo 255 > /sys/devices/pwm-fan/target_pwm
    sleep 1
    echo 0 > /sys/devices/pwm-fan/target_pwm
fi

#Configuring I2C-8 with 100 KHz
echo 100000 > /sys/bus/i2c/devices/i2c-8/bus_clk_rate
#Configuring MIPI Re-timer after reset
gpio_test -n 443 -o 1
sleep 1
gpio_test -n 443 -o 0
sleep 1
i2cset -f -y 8 0x6c 0x09 0x00
i2cset -f -y 8 0x6c 0x0a 0x51
i2cset -f -y 8 0x6c 0x0b 0x55
i2cset -f -y 8 0x6c 0x0e 0x11

#Configuring I2C-6 with 100 KHz
echo 100000 > /sys/bus/i2c/devices/i2c-6/bus_clk_rate
#Configuring MIPI Re-timer after reset
gpio_test -n 267 -o 1
sleep 1
gpio_test -n 267 -o 0
sleep 1
i2cset -f -y 6 0x6c 0x09 0x00
i2cset -f -y 6 0x6c 0x0a 0x51
i2cset -f -y 6 0x6c 0x0b 0x55
i2cset -f -y 6 0x6c 0x0e 0x11

#For LTE
gpio_test -n 389 -o 1

#For ADC
insmod /lib/modules/4.9.140-tegra/kernel/drivers/misc/ads7924.ko

#For IMU
insmod /lib/modules/4.9.140-tegra/kernel/drivers/iio/imu/inv_mpu/inv_mpu_20602/inv-mpu-iio-new.ko 
insmod /lib/modules/4.9.140-tegra/kernel/drivers/iio/imu/inv_mpu/inv_mpu_20602/inv-mpu-iio-i2c-icm20602.ko

#For Wi-Fi and external eMMc
echo -n "3440000.sdhci" > /sys/bus/platform/drivers/sdhci-tegra/unbind
echo -n "3400000.sdhci" > /sys/bus/platform/drivers/sdhci-tegra/unbind
sleep 2
echo -n "3440000.sdhci" > /sys/bus/platform/drivers/sdhci-tegra/bind
echo -n "3400000.sdhci" > /sys/bus/platform/drivers/sdhci-tegra/bind
sleep 2
umount /dev/mmcblk2
sleep 2
mount /dev/mmcblk2
