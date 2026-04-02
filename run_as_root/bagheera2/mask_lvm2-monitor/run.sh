#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling execute_script function from lib
log "=============executing mask_lvm2-monitor =================="
execute_command bash ${PWD}/mask_lvm2-monitor.sh
status=$(echo $?)   
check_status $(basename $(pwd)) $status

log "=============End of mask_lvm2-monitor execution===================="
