#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Cleaning 
log "======= Start of sync_cleanup_IB task execution ======="
execute_command bash ${PWD}/sync_cleanup_IB.sh
status=$?
check_status $(basename $(pwd)) $status
log "============= End of sync_cleanup_IB task execution ==================="

