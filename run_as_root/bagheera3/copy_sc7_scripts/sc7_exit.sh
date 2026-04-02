#!/bin/sh

AZUREWAVE_MODULE=0
REALTEK_MODULE=1
UNKNOWN_MODULE=2

WIFI_BT_MODULE=1
AZUREWAVE_WIFI_BT_MODULE_STR="Broadcom"
REALTEK_WIFI_BT_MODULE_STR="Realtek"

check_wifi_bt_module() {
    	# Checks to See AzureWave or RealTek Wifi/BT module
	lspci | grep -q $AZUREWAVE_WIFI_BT_MODULE_STR
	if [ $? -eq 0 ]; then
		WIFI_BT_MODULE=$AZUREWAVE_MODULE
		echo "Azurewave Wifi/BT module is detected."
	else
		# Check if RealTek Module is present
		lspci | grep -q $REALTEK_WIFI_BT_MODULE_STR
		if [ $? -eq 0 ]; then
			WIFI_BT_MODULE=$REALTEK_MODULE
			echo "RealTek Wifi module is detected."
		else
			WIFI_BT_MODULE=$UNKNOWN_MODULE
			echo "Neither Azurewave not RealTek Wifi is detected."
		fi
	fi
}

is_interface_up()
{
    cnt=3
    while [ $cnt -gt 0 ]
    do
        iw dev | grep -q wlan1
        st=$?
        if [ $st -eq 0 ]; then
                rename_network_interface
                break
        fi
        cnt=$((cnt--))
        sleep 2
    done

   if [ $st -ne 0 ]
    then
        echo "WIFI AP - Failed : iw dev failed to list wlan1!"
    fi
}

rename_network_interface(){

        ip link set wlan1 down
        ip link set wlan1 name secondary
        ip link set secondary up
        sleep 1
        sudo systemctl restart NetworkManager
}

check_wifi_bt_module

sleep 2

#For EXPANDER and LED
/bin/vendor/sc7_reg_set

#FOR OUTCAM and Inward Camera
gpio_test -n 227 -o 1 #OUTWARD_CAM_PWR_ON
sleep 0.02
gpio_test -n 424 -o 1 #OUTWARD_PWRDN
sleep 0.02
gpio_test -n 243 -o 1 #OUTWARD_CAM_RESET
sleep 0.02
gpio_test -n 229 -o 1 #INWARD_CAM_PWR_ON
sleep 0.02
gpio_test -n 427 -o 0 #INWARD_CAM_PWRDN
sleep 0.02
gpio_test -n 246 -o 1 #INWARD_CAM_RST
sleep 0.02
gpio_test -n 237 -o 1 #LCAM_PWR_ON
sleep 0.02
gpio_test -n 224 -o 1 #RCAM_PWR_ON
sleep 0.02
gpio_test -n 225 -o 0 #LRCAM_PWR_DWN
sleep 0.02
gpio_test -n 376 -o 1 #RCAM_RESET
sleep 0.02
gpio_test -n 379 -o 1 #LCAM_RESET

#For ADC
insmod /lib/modules/$(uname -r)/kernel/drivers/misc/ads7924.ko

#For TEMPERATURE SENSOR
insmod /lib/modules/$(uname -r)/kernel/drivers/hwmon/tmp102.ko

gpio_test -n 228 -o 0 #LUM VBUS
gpio_test -n 238 -o 0 #LUM DEN

gpio_test -n 240 -o 0 #USBDEV VBUS
gpio_test -n 239 -o 0 #USBDEV DEN

#For CP2112
insmod /lib/modules/$(uname -r)/kernel/drivers/hid/hid-cp2112.ko

echo -n "3440000.sdhci" > /sys/bus/platform/drivers/sdhci-tegra/bind
sleep 2

if [ $WIFI_BT_MODULE -eq $AZUREWAVE_MODULE ]; then
    #For Wi-Fi and external eMMc
    insmod /lib/modules/misc/compat.ko
    insmod /lib/modules/misc/cfg80211.ko
    insmod /lib/modules/misc/brcmutil.ko
    insmod /lib/modules/misc/brcmfmac.ko debug=0x100000
elif [ $WIFI_BT_MODULE -eq $REALTEK_MODULE ]; then
	insmod /lib/modules/$(uname -r)/kernel/net/wireless/cfg80211.ko
	insmod /lib/modules/misc/rtl8822ce_wifi_module.ko rtw_drv_log_level=2
	# BT
	insmod /lib/modules/misc/hci_uart.ko
	gpio_test -n 235 -o 0
	sleep 1
	gpio_test -n 235 -o 1
	sleep 1
	rtk_hciattach -n -s 115200 /dev/ttyTHS2 rtk_h5 &
fi

sleep 1

if [ $WIFI_BT_MODULE -eq $REALTEK_MODULE ]; then

        is_interface_up
fi

mount -a

#AUDIO
/etc/init.d/amixer.sh &

#Disable IMU_WOM upon sc7 sleep exit
echo 0 > /sys/bus/iio/devices/iio:device1/wom_mode_config
echo 1 > /sys/bus/iio/devices/iio:device1/master_enable

#Turn off leds 
/etc/init.d/turn_off_leds.sh
# Turn off irleds
/etc/init.d/turn_off_irled.sh
