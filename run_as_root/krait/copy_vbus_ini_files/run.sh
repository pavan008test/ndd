#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "========== Start of copying vbus_common_config.ini files =========="

if [[ ! -d /data/nd_files/fw_download ]]; then 
    log "Creating fw_download folder in /data/nd_files/ path"
    mkdir -p /data/nd_files/fw_download
else
    log "Directory /data/nd_files/fw_download already exists, not creating."
fi

md5sum_vbus_common_config=$(md5sum vbus_common_config.ini | awk '{print $1}')

md5sum_fw_details=$(md5sum fw_details.ini | awk '{print $1}')

vbus_fw_details_file_copy_status_file=/home/ubuntu/.nddevice/vbus_fw_details_file_copy_status

if [[ -f $vbus_fw_details_file_copy_status_file ]]; then
    log "File $vbus_fw_details_file_copy_status_file exists."
    status1=0
    touch_file_status=0
else
    log "File $vbus_fw_details_file_copy_status_file does not exist. Copying fw_details.ini to /data/nd_files/fw_download/ and creating status file."
    copy_file -f fw_details.ini -d /data/nd_files/fw_download/ -m $md5sum_fw_details -p 644 -o root:root
    status1=$?
    touch $vbus_fw_details_file_copy_status_file
    touch_file_status=$?
fi

vbus_common_config_sys=$(md5sum /data/nd_files/config/vbus_common_config.ini | awk '{print $1}')
vbus_common_config_local=$(md5sum vbus_common_config.ini | awk '{print $1}')

log "copying the vbus_common_config.ini file"
if [[ -f /data/nd_files/config/vbus_common_config.ini && "$md5sum_vbus_common_config" == "$vbus_common_config_sys" ]]; then
    log "vbus_common_config.ini already exists with matching md5sum, skipping copy."
    status3=0
else
    copy_file -f vbus_common_config.ini -d /data/nd_files/config/ -m $md5sum_vbus_common_config -p 775
    status3=$?
    log "vbus_common_config.ini copied status $status3"
fi

if [[ $status1 -eq 0 ]] && [[ $status3 -eq 0 ]] && [[ $touch_file_status -eq 0 ]]; then
    check_status $(basename $(pwd)) 0
else
    check_status $(basename $(pwd)) 1
fi

log "============= End of Copying vbus_common_config.ini file  ==================="

