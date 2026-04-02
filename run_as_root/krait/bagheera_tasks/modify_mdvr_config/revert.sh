#!/usr/bin/env bash


source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling execute_script function from lib

log "====== Starting of revert modify_mdvr_config file ========"
log "changing section ext_cam_config to ext_cam"
status=$(execute_command sed -i 's/\[ext_cam_config\]/\[ext_cam\]/g' /home/ubuntu/.nddevice/mdvr_config.ini)$?
check_status $(basename $(pwd)) $status
log "====== End of revert modify_mdvr_config file ======"
