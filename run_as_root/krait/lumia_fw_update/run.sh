#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Copying the required files to their respective paths
log "Preparing for the lumia firware update"
log "=============Installing the lumia firmware=================="
execute_command bash ${PWD}/lte_gps_sierra_upgrade_lite.sh lumia_firmware > /home/ubuntu/.nddevice/log/lumia_fw_update.log 2>&1
status=$(echo $?)
check_status $(basename $(pwd)) $status
log "=============End of lumia firmware==================="

