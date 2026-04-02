#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Copying the required files to their respective paths
log "======= Start of copy of os_ota_migration files script ======="
execute_script copy_list.sh
status=$(echo $?)
check_status $(basename $(pwd)) $status
log "============= End of copy files script =============="

