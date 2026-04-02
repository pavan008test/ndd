#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling execute_script function from lib

log "====== Starting of modify_mdvr_config file ========"
log "changing section ext_cam to ext_cam_config"
status=$(execute_command sed -i 's/\[ext_cam\]/\[ext_cam_config\]/g' /home/ubuntu/.nddevice/mdvr_config.ini)$?
check_status $(basename $(pwd)) $status
log "====== End of modify_mdvr_config file ======"
