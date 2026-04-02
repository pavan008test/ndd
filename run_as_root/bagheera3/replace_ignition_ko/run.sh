#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling execute_script function from lib
log "=============replace the ko file for gpio-ignition=================="
bash ${PWD}/replace_ignition_ko.sh >> /home/ubuntu/.nddevice/log/bhcopy.log 2>&1 
status=$(echo $?)
check_status $(basename $(pwd)) $status
log "=============End of replacing ko file for gpio-ignition===================="
