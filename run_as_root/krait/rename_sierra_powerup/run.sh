#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "======= Start of rename sierra powerup script execution ======="
execute_command bash ${PWD}/rename_sierra_powerup.sh
status=$?
check_status $(basename $(pwd)) $status
log "============= End of rename sierra powerup script execution ==================="

