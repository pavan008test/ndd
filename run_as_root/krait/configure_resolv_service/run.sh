#!/usr/bin/env bash


source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling execute_script function from lib
log "=============Starting configure_resolv_service =================="

status=$(execute_script configure_resolv_service.sh)$?
check_status $(basename $(pwd)) $status

log "=============End of configure_resolv_service==================="

