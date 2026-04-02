#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "===============Assigning CPU affinities to ND Services=================="
# Calling execute_script function from lib
log "Executing the systemd_override_krait.sh"
execute_script systemd_override_krait.sh
status=$(echo $?)
check_status $(basename $(pwd)) $status
log "===========End of Assigning CPU affinities to ND Services==============="
