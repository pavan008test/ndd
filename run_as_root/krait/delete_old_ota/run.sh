#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Copying the required files to their respective paths
log "======= Start of delete_old_ota ======="
execute_command bash ${PWD}/delete_old_packages.sh
status=$(echo $?)
check_status $(basename $(pwd)) $status
log "============= End of delete_old_ota =============="

