#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Copying the required files to their respective paths
log "======= Start of install_app_ULPM update  ======="
execute_command bash ${PWD}/install_app_ULPM.sh >> /home/ubuntu/.nddevice/log/bhcopy.log 2>&1
status=$(echo $?)
check_status $(basename $(pwd)) $status
log "============= End of install_app_ULPM update ==================="

