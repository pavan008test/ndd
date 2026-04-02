#!/bin/sh

# This is the script file which will run at system starts up.
# The commands needs to be run at startup can be added in this file.

mkdir -p /image_upgrade/tmp
mkdir -p /image_upgrade/tmp2
mkdir -p /image_upgrade/media

mkdir -p /fru_check/tmp
mkdir -p /fru_check/tmp2
mkdir -p /fru_check/read_bias

echo 186 >  /sys/class/gpio/export
echo 187 >  /sys/class/gpio/export

insmod /lib/modules/misc/gpio_icdc.ko
mw_test 0x70000924 0x202000
mw_test 0x70003058 0x8040

TEMP_DRIVER=/lib/modules/`uname -r`/kernel/drivers/hwmon/tmp102.ko
ADS7924_DRIVER=/lib/modules/`uname -r`/kernel/drivers/misc/ads7924.ko
 
#To confirm the detection of GPIO EXPANDER in I2C bus 1

echo "Executing configure lte modem binary"
/bin/config_uart_lte_modem &

i2cget -y -f 1 0x70 0x00

if [ $? -eq 0 ]; then

#To read the Revision from the hardware pins
    /bin/revision_icdc
    REVISION=$?
    echo "The hardware revision is $REVISION"

#To insert the drivers based on revision
#Drivers inserted are tmp102 and ads7924
    if [ $REVISION -ge 1 ]; then

        if [ -f "$TEMP_DRIVER" ];then
            insmod /lib/modules/`uname -r`/kernel/drivers/hwmon/tmp102.ko

            echo tmp102 0x49>/sys/class/i2c-adapter/i2c-1/new_device

            echo 70000 >/sys/class/i2c-dev/i2c-1/device/1-0049/temp1_max
                if [ $? -ne 0 ]; then
                    echo "Setting min threshold Failed";
                fi;
            echo 65000 >/sys/class/i2c-dev/i2c-1/device/1-0049/temp1_max_hyst

                if [ $? -ne 0 ]; then
                    echo "Setting min threshold Failed";
                fi;
        fi

        if [ -f "$ADS7924_DRIVER" ];then
        insmod /lib/modules/`uname -r`/kernel/drivers/misc/ads7924.ko

        echo ads7924 0x48>/sys/class/i2c-adapter/i2c-1/new_device
        fi

    else

        if [ -f "$TEMP_DRIVER" ];then
            insmod /lib/modules/`uname -r`/kernel/drivers/hwmon/tmp102.ko

            echo tmp102 0x48>/sys/class/i2c-adapter/i2c-1/new_device

            echo 70000 >/sys/class/i2c-dev/i2c-1/device/1-0048/temp1_max

            if [ $? -ne 0 ]; then
                  echo "Setting max threshold Failed";
            fi;

            echo 65000 >/sys/class/i2c-dev/i2c-1/device/1-0048/temp1_max_hyst

            if [ $? -ne 0 ]; then
                echo "Setting min threshold Failed";
            fi;
        fi;
    fi;
fi;

#To monitor the carrier release pin
/bin/carrier_reset_check &

#U-boot,Kernel Logs
/bin/boot_log &

# installing spi driver for obd
modprobe spidev

#hwclock -s

killall -s 9 gpio_toggle_thread

rfkill unblock bluetooth
sleep 1
bt-adapter -s Powered 1
bt-adapter -s Discoverable on
bt-adapter -s DiscoverableTimeout 0
bt-adapter -s Pairable on
bt-adapter -s PairableTimeout 0

#To program the FRU in EMMC based on revision
#If revision is less than 2 , FRU will be programmed from SPI FLASH
#else FRU will be programmed from EEPROM

if [ $REVISION -lt 2 ]; then

    /image_upgrade/isp/spi4_enable.sh
    gpio_test -n 148 -s 0 >> /dev/null 2>&1
    gpio_test -n 151 -s 0 >> /dev/null 2>&1
    gpio_test -n 174 -s 1 >> /dev/null 2>&1

    /bin/fru_check

    if [ $? -ne 0 ]; then
        echo "Fru Failed";
    fi;
    /image_upgrade/isp/spi4_disable.sh
else

    /bin/fru_check

    if [ $? -ne 0 ]; then
        echo "Fru Failed";
    fi;
fi;

gpio_test -n 174 -s 0 >> /dev/null 2>&1
sleep 0.5
gpio_test -n 151 -s 0 >> /dev/null 2>&1
sleep 0.5
gpio_test -n 151 -s 1 >> /dev/null 2>&1
sleep 0.5
gpio_test -n 148 -s 0 >> /dev/null 2>&1
sleep 0.5
gpio_test -n 148 -s 1 >> /dev/null 2>&1
sleep 0.5

chmod 777 /image_upgrade/isp/ov491_isp_upgrade.sh
chmod 777 /image_upgrade/sierra/lte_gps_sierra_upgrade.sh

amixer -c 0 sset "ADMAIF1 Mux" "I2S3"
amixer -c 0 sset "I2S3 Mux" "ADMAIF1"
amixer -c 0 sset "ADMAIF4 Mux" "I2S1"
amixer -c 0 sset "I2S1 Mux" "ADMAIF4"

/bin/diag_check
if [ $? -ne 0 ]; then
    echo "Diag Check Failed";
fi;

ps -aux |grep bt-agent |awk '{print $2}'|xargs kill -9
yes | bt-agent >> /dev/null &

touch /dev/shm/ntdi_icdc.done
exit 0
