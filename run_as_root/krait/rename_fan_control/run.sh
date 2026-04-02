#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "======= Start of rename_fan_control task execution ======="
execute_command bash ${PWD}/rename_fan_control.sh
status=$?
check_status $(basename $(pwd)) $status
log "============= End of rename_fan_control task execution ==================="

