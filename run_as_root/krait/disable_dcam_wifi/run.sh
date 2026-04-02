#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling execute_script function from lib
log "=============executing disable_dcam_wifi =================="
execute_command bash ${PWD}/disable_dcam_wifi.sh
status=$(echo $?)	
check_status $(basename $(pwd)) $status

log "=============End of disable_dcam_wifi execution===================="
