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

check_wifi_bt_module

if [ $WIFI_BT_MODULE -eq $AZUREWAVE_MODULE ]; then
    #For wifi and emmc
    rmmod brcmfmac.ko brcmutil.ko cfg80211.ko compat.ko
elif [ $WIFI_BT_MODULE -eq $REALTEK_MODULE ]; then
	rmmod rtl8822ce_wifi_module cfg80211
	# BT
	killall rtk_hciattach
	sleep 1
	gpio_test -n 235 -o 0
	sleep 3
	rmmod hci_uart
fi

sync
#sleep 2
echo -n "3440000.sdhci" > /sys/bus/platform/drivers/sdhci-tegra/unbind

#For EXPANDER and LED
FILE=/etc/init.d/i2creg_dump.txt

rm ${FILE}
#24-bit expander values
i2cget -f -y 7 0x22 0x04 >> ${FILE}
i2cget -f -y 7 0x22 0x05 >> ${FILE}
i2cget -f -y 7 0x22 0x06 >> ${FILE}
i2cget -f -y 7 0x22 0x0c >> ${FILE}
i2cget -f -y 7 0x22 0x0d >> ${FILE}
i2cget -f -y 7 0x22 0x0e >> ${FILE}
sleep 0.5

#8-bit expander values
i2cget -f -y 7 0x70 0x01 >> ${FILE}
i2cget -f -y 7 0x70 0x03 >> ${FILE}

#OUTCAM and Inward Camera
gpio_test -n 243 -o 0 #OUTWARD_RESET
sleep 0.02
gpio_test -n 424 -o 0 #OUTWARD_PWRDN
sleep 0.02
gpio_test -n 227 -o 0 #OUTWARD_PWR_OFF
sleep 0.02
gpio_test -n 246 -o 0 #INWARD_RST
sleep 0.02
gpio_test -n 427 -o 1 #INWARD_PWR_DWN
sleep 0.02
gpio_test -n 229 -o 0 #INWARD_PWR_OFF
sleep 0.02
gpio_test -n 376 -o 0 #RCAM_RESET
sleep 0.02
gpio_test -n 379 -o 0 #LCAM_RESET
sleep 0.02
gpio_test -n 225 -o 1 #LRCAM_PWR_DWN
sleep 0.02
gpio_test -n 237 -o 0 #LCAM_PWR_OFF
sleep 0.02
gpio_test -n 224 -o 0 #RCAM_PWR_OFF


#For ADC
rmmod ads7924.ko

#For TEMPERATURE SENSOR
rmmod tmp102.ko

#For CP2112
rmmod hid_cp2112.ko

gpio_test -n 238 -o 1 #LUMIA DEN
gpio_test -n 228 -o 1 #LUMIA VBUS

gpio_test -n 239 -o 1 #USBDEV DEN
gpio_test -n 240 -o 1 #USBDEV VBUS

#sleep 2

#Turn off leds
/etc/init.d/turn_off_leds.sh
#Turn of Irleds
/etc/init.d/turn_off_irled.sh
