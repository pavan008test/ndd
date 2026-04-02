#!/bin/bash

LUMIA_STATUS_FILE="/bin/vendor/lumia_status.txt" # store product name based on enumerated device
BLACK_LIST_FILE="/etc/modprobe.d/blacklist.conf"

lumia3_modules=("qcserial" "option" "usb_wwan" "qmi_wwan_q" "cdc_wdm")
lumia2_modules=("GobiSerial" "GobiNet")

# Lumia2 PID & VID ->  VID & PID is common for WP7611,WP7609 & WP7607
# Bus 001 Device 002: ID 1199:68c0 Sierra Wireless, Inc.

# Lumia3 (EC25-AFXD) PID's & VID's
# Bus 001 Device 004: ID 2c7c:0125
# Bus 001 Device 003: ID 10c4:ea60 Cygnal Integrated Products, Inc. CP210x UART Bridge / myAVR mySmartUSB light
# Bus 001 Device 002: ID 0424:2422 Standard Microsystems Corp

l2_vendor_id1="1199"
l2_product_id1="68c0"
l3_vendor_id1="2c7c"
l3_product_id1="0125"
l3_vendor_id2="10c4"
l3_product_id2="ea60"
l3_vendor_id3="0424"
l3_product_id3="2422"

function lumia3_drivers_enable {
	# Removing Lumia3 modules from the blacklist
	for name in "${lumia3_modules[@]}"; do
		if grep -qE "$name" "$BLACK_LIST_FILE"; then
			sed -i "/blacklist $name/d" "$BLACK_LIST_FILE"
		fi
	done

        # Adding Lumia2 modules into the blacklist
        for name in "${lumia2_modules[@]}"; do
            if ! grep -qE "$name" "$BLACK_LIST_FILE"; then
		    echo "blacklist $name" >> "$BLACK_LIST_FILE"
	    fi
        done

	echo "EC25-AFXD" > "$LUMIA_STATUS_FILE"
}

function lumia2_drivers_enable {
	# Removing Lumia2 Modules from the blacklist
	for name in "${lumia2_modules[@]}"; do
		if grep -qE "$name" "$BLACK_LIST_FILE"; then
			sed -i "/blacklist $name/d" "$BLACK_LIST_FILE"
		fi
	done

        # Adding Lumia3 modules into the blacklist
        for name in "${lumia3_modules[@]}"; do
            if ! grep -qE "$name" "$BLACK_LIST_FILE"; then
		    echo "blacklist $name" >> "$BLACK_LIST_FILE"
	    fi
        done

	echo "Sierra Wireless" > "$LUMIA_STATUS_FILE"
}

timeout=120  # Set timeout to 2 minutes (120 seconds)
time_cal=0

while [ $time_cal -lt $timeout ]; do
	if lsusb_output=$(lsusb | grep -E "$l3_vendor_id1:$l3_product_id1" && lsusb | grep -E "$l3_vendor_id2:$l3_product_id2" && lsusb | grep -E "$l3_vendor_id3:$l3_product_id3"); then
		if ! grep -q "EC25-AFXD" "$LUMIA_STATUS_FILE"; then
			sleep 1
			lumia3_drivers_enable
		fi
		ifconfig wwan0 up
		echo "WWAN0 Interface is up"
		sleep 0.1 # Sleep for 100ms
		break
	elif lsusb_output=$(lsusb | grep -E "$l2_vendor_id1:$l2_product_id1"); then
		if ! grep -q "Sierra Wireless" "$LUMIA_STATUS_FILE"; then
			sleep 1
			lumia2_drivers_enable
		fi
		break
	else
		sleep 1
		((time_cal++))
	fi
done

if [ $time_cal -ge $timeout ]; then
	echo "No USB device found" > "$LUMIA_STATUS_FILE"
fi
sync
