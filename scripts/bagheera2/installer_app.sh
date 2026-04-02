#!/bin/sh

#script for invoking the installer_app service

#export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/home/ubuntu/.nddevice/latest/:/home/ubuntu/.nddevice/gst_1.8.1/
#export GST_PLUGIN_SYSTEM_PATH=/home/ubuntu/.nddevice/gst_1.8.1/gstreamer-1.0/

INSTALLER_SCAN_FILE="/dev/shm/nd_files_c/installer_scan_ongoing"

if [ -f "$INSTALLER_SCAN_FILE" ]; then
    STATE=$(cat "$INSTALLER_SCAN_FILE")

    if [ "$STATE" != "nd_bt" ]; then
        rm -f "$INSTALLER_SCAN_FILE"
    fi
fi

/home/ubuntu/.nddevice/latest/service/installer_app/installer_app

echo END OF SCRIPT
