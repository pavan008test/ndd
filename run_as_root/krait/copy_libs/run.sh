#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Copying the required libs to /nd_lib from there orignal paths <>latest/libs
log "======= Start of copy_libs task execution ======="
execute_command bash ${PWD}/copy_libs.sh
status=$?
check_status $(basename $(pwd)) $status
log "============= End of copy_libs task execution ==================="

