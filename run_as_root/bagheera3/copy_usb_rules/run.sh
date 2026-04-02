#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

file_name="99-nd-usb_hub-devices.rules"
destination_path="/etc/udev/rules.d"
permission="644"
BACKUP_DIR="/home/ubuntu/backup/run_as_root/usb_rules/"


log "============ Start of copying $file_name ==========="
md5sum_local=$(md5sum $file_name | awk '{print $1}')
dest_file="${destination_path}/${file_name}"

if [[ -f $dest_file ]]; then
        md5sum_system=$(md5sum $dest_file | awk '{print $1}')
        if [[ $md5sum_local == $md5sum_system ]]; then
                log "$file_name already present and up-to-date."
                status=0
        else
                log "$file_name found but md5sum mismatch, copying."
                copy_file -f $file_name -d $destination_path -b $BACKUP_DIR -m $md5sum_local -p $permission -o ubuntu:ubuntu
                status=$?
        fi
else
        log "$file_name not found, copying."
        copy_file -f $file_name -d $destination_path -m $md5sum_local -p $permission -o ubuntu:ubuntu
        status=$?
fi

 check_status $(basename $(pwd)) $status

log "============ End of copying $file_name  ==========="
