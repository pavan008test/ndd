#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "========== Start of copying fw_details.ini =========="

if [[ ! -d /home/ubuntu/fw_download ]]; then 
    log "Creating fw_download folder in /home/ubuntu/ path"
    mkdir -p /home/ubuntu/fw_download
else
    log "/home/ubuntu/fw_download Directory already exists, not creating."
fi

vbus_fw_details_file_copy_status_file=/home/ubuntu/.nddevice/vbus_fw_details_file_copy_status

if [[ -f $vbus_fw_details_file_copy_status_file ]]; then
    log "File $vbus_fw_details_file_copy_status_file exists."
    status=0
    touch_file_status=0
else
    md5sum_fw_details=$(md5sum fw_details.ini | awk '{print $1}')
    copy_file -f fw_details.ini -d /home/ubuntu/fw_download/ -m $md5sum_fw_details -p 644 -o ubuntu:ubuntu
    status=$?
    touch $vbus_fw_details_file_copy_status_file
    touch_file_status=$?
fi

final_status=0
if [[ $status -ne 0 && $touch_file_status -ne 0 ]]; then
    log "Failed to copy fw_details.ini to /home/ubuntu/fw_download/ or failed to touch status file."
    final_status=1
else
    log "fw_details.ini copied successfully to /home/ubuntu/fw_download/"
fi

check_status $(basename $(pwd)) $final_status

log "========== End of copying fw_details.ini =========="
