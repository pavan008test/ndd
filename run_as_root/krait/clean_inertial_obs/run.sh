#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Cleaning 
log "======= Start of clean_inertial_obs task execution ======="
execute_command bash ${PWD}/clean_inertial_obs.sh
status=$?
check_status $(basename $(pwd)) $status
log "============= End of clean_inertial_obs.sh task execution ==================="

