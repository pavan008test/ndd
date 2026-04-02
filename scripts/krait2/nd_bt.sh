#!/bin/sh

export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/home/ubuntu/.nddevice/latest/service/nd_bt/

# killall -9 btproperty

# sleep 2

# btproperty &

# sleep 2

/home/ubuntu/.nddevice/latest/service/nd_bt/nd_bt_watchdog.sh &

if [ ! -d /home/ubuntu/.nddevice/log/btfv ]; then
    mkdir -p /home/ubuntu/.nddevice/log/btfv;
fi

# redirect output to file
readonly LOG_FILE_NAME="/home/ubuntu/.nddevice/log/btfv/log_$(date +%s)000.log"
touch $LOG_FILE_NAME
exec 1>>$LOG_FILE_NAME
exec 2>&1

INSTALLER_SCAN_FILE="/dev/shm/installer_scan_ongoing"

if [ -f "$INSTALLER_SCAN_FILE" ]; then
    STATE=$(cat "$INSTALLER_SCAN_FILE")

    if [ "$STATE" != "installer_app" ]; then
        rm -f "$INSTALLER_SCAN_FILE"
    fi
fi

ND_CONFIG_PATH=/home/ubuntu/.nddevice/latest/nd_config.ini ND_DEVICE_REL_PATH=/home/ubuntu/.nddevice/ /home/ubuntu/.nddevice/latest/service/nd_bt/nd_bt_man

echo END OF SCRIPT
