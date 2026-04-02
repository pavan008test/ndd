#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

md5sum_acc=$(md5sum accessory_details.ini | awk '{print $1}')

SRC_FILE="accessory_details.ini"
DST_FILE="/home/ubuntu/.nddevice/accessory_details.ini"
BAK_FILE="/home/ubuntu/.nddevice/accessory_details_bk.ini"

log "============ Copying accessory_details.ini file ==========="

if [[ -f "$DST_FILE" ]] && [[ -f "$BAK_FILE" ]]; then
    log "accessory_details.ini and backup already exist. Not copying."
    status1=0
    status2=0
else
    log "accessory_details.ini does not exist. Copying file."
    copy_file -f "$SRC_FILE" -d /home/ubuntu/.nddevice/ -m $md5sum_acc -p 775 -o root:root
    status1=$?
    cp "$DST_FILE" "$BAK_FILE"
    status2=$?
    sync
fi

if [[ $status1 -eq 0 && $status2 -eq 0 ]]; then
    log "Copying accessory_details.ini and backup created successfully"
    status=0
    check_status $(basename $(pwd)) $status
else
    log "Something wrong with copying accessory_details.ini or backup creation. please check !!!!!!"
    status=1
    check_status $(basename $(pwd)) $status
fi

log "================End of copying accessory_details.ini file ==========="
