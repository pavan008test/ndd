#!/usr/bin/env bash


source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling execute_script function from lib

log "====== Starting of revert modify_mdvr_config file ========"
log "changing section ext_cam_config to ext_cam"
current_ota=$(cat /home/ubuntu/.nddevice/nddevice.ini | grep -a1 version | grep nddevice | awk -F "=" '{print $2}' | tr -d ' ' | cut -d "." -f 1-3)
ota_list=(0.4.9 0.5.3 0.5.4)
for version in ${ota_list[@]}
do
    if [[ $current_ota == $version ]]
    then
	log "This revert task is not required for this current OTA $current_ota"
      exit 0
    fi
done

status=$(execute_command sed -i 's/\[ext_cam_config\]/\[ext_cam\]/g' /home/ubuntu/.nddevice/mdvr_config.ini)$?
check_status $(basename $(pwd)) $status
log "====== End of revert modify_mdvr_config file ======"
