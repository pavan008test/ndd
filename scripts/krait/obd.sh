#!/bin/sh

readonly LOG_FILE_NAME="/home/ubuntu/.nddevice/log/obd/log_$(date +%s)000.log"
touch $LOG_FILE_NAME
exec 1>$LOG_FILE_NAME
exec 2>&1

#script for executing obd firmware update
/home/ubuntu/.nddevice/latest/service/run_as_root/obd_fw_update/obd_FW_update.sh latest/service

#script for invoking the obd service

sudo /home/ubuntu/.nddevice/latest/service/obd/obd_app1

echo END OF SCRIPT

