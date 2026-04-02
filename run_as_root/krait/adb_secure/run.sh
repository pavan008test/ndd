#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#securing the adb with adb_keys
log "======= Start of adb_secure task execution ======="
execute_command bash ${PWD}/adb_secure.sh
status=$?
check_status $(basename $(pwd)) $status
log "============= End of adb_secure task execution ==================="

