#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling execute_script function from lib
log "=============executing disable_chrony_service =================="
execute_command bash ${PWD}/disable_chrony_service.sh
status=$(echo $?)
check_status $(basename $(pwd)) $status

log "=============End of disable_chrony_service execution===================="
