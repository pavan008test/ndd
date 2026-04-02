#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh
export ND_DEVICE_REL_PATH=/home/ubuntu/.nddevice
#Cleaning 
log "======= Start of nd_config_version_path task execution ======="
execute_command python3 ${PWD}/nd_config_version_path.py
status=$?
check_status $(basename $(pwd)) $status
log "============= End of nd_config_version_path task execution ==================="

