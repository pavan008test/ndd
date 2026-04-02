#!/usr/bin/env bash

set -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#revert the rename_fan_control task

log "==============Start of revert rename_fan_control ================="

log "Checking if fan_control_void file is present in /usr/bin/"
if [[ -f /usr/bin/fan_control_void ]] ; then
    log "fan_control_void file is present in /usr/bin/, reverting it..."
    if [[ -n $(pgrep fan_control) ]] ; then
        log "fan_control is in running process, stoping it..."
        sudo pkill fan_control
        if [[ $? == 0 ]]; then
            log "fan_control process stopped ok"
        else
            log "fan_control process would have killed already or not running"
        fi
    fi
    log "renaming fan_control_void file @ /usr/bin/"
    execute_command "mv /usr/bin/fan_control_void /usr/bin/fan_control"
    status=$(echo $?)
    check_status $(basename $(pwd)) $status
else
    log "/usr/bin/fan_control_void file is not present. Revert failed..."
    exit 1
fi

log "==============End of revert rename_fan_control================="

exit 0
