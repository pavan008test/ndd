#!/usr/bin/env bash


source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "Checking if fan_contol file is present in /usr/bin/"
if [[ -f /usr/bin/fan_control ]] ; then
    log "fan_contol file is present in /usr/bin/, renaming it..."
    CHECKSUM=$(md5sum /usr/bin/fan_control | awk -F " " '{print $1}')
    if [[ -n $(pgrep fan_control) ]] ; then
        log "fan_control is in running process, stoping it..."
        sudo pkill fan_control
        if [[ $? == 0 ]]; then
            log "fan_control process stopped ok"
        else
            log "fan_control process would have killed already or not running"
        fi
    fi
    execute_command "mv /usr/bin/fan_control /usr/bin/fan_control_void"
    if [[ $(md5sum /usr/bin/fan_control_void | awk -F " " '{print $1}') == ${CHECKSUM} ]]; then
        log "file /usr/bin/fan_control renamed ok as /usr/bin/fan_control_void"
    else
        log "failed for file rename of /usr/bin/fan_control as /usr/bin/fan_control_void. Aborting the update..."
        exit 1
    fi
else
    log "/usr/bin/fan_control file is not present. Skipping the rename..."
fi

