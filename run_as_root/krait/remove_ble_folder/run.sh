#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "======= Start of remove_ble_folder task execution ======="
execute_command bash ${PWD}/remove_ble_folder.sh
status=$?
check_status $(basename $(pwd)) $status
log "============= End of remove_ble_folder task execution ==================="

