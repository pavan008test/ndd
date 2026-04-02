#!/bin/bash

AZUREWAVE_WIFI_BT_MODULE_STR="Broadcom"
REALTEK_WIFI_BT_MODULE_STR="Realtek"

check_wifi_bt_module() {
    # Checks to See AzureWave or RealTek Wifi/BT module
	lspci | grep -q $AZUREWAVE_WIFI_BT_MODULE_STR
	if [ $? -eq 0 ]; then
		device_name="brcmfmac"
		echo "Azurewave Wifi/BT module is detected."
	else
		# Check if RealTek Module is present
		lspci | grep -q $REALTEK_WIFI_BT_MODULE_STR
		if [ $? -eq 0 ]; then
		    device_name="rtl88x2ce"
		    echo "RealTek Wifi module is detected."
		else
			WIFI_BT_MODULE=$UNKNOWN_MODULE
			echo "ERROR:Neither Azurewave not RealTek Wifi is detected."
			exit 1
		fi
	fi
}

usage()
{
	echo "Usage:"
	echo "$0 <arguments>"
	echo "arguments:"
	echo "         bind             --- To bind wifi driver with device"
	echo "         unbind           --- To unbind wifi driver with device"
	echo "         Ex:"
	echo "         $0 un/bind"
	echo ""
}

## Start of script execution
if [ $# -ne 1 ]; then
    usage
    exit 1
fi

#Wifi/BT Module check
check_wifi_bt_module

case $1 in 
        bind) 
                echo "Operation:bind"
                echo 0000:01:00.0 > /sys/bus/pci/drivers/$device_name/bind
                status=$?
                ;;
        unbind)
                echo "Operation:unbind"
                echo 0000:01:00.0 > /sys/bus/pci/drivers/$device_name/unbind
                status=$?
                ;;
        *)
                usage
                exit 1
                ;;
esac
exit $status
