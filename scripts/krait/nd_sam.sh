#!/bin/sh

if [ ! -d /home/ubuntu/.nddevice/log/nd_sam ]; then
    mkdir -p /home/ubuntu/.nddevice/log/nd_sam;
fi

# redirect output to file
readonly LOG_FILE_NAME="/home/ubuntu/.nddevice/log/nd_sam/log_$(date +%s)000.log"
touch $LOG_FILE_NAME
exec 1>>$LOG_FILE_NAME
exec 2>&1

# script for invoking the nd_sam service

sudo /home/ubuntu/.nddevice/latest/service/nd_sam/nd_sam

echo END OF SCRIPT
