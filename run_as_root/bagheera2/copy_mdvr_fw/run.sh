#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

file_name="FL3521A_4CH-V18062301-V18070301-V18112801-S25122501.35848.sw"
destination_path="/home/ubuntu/.nddevice/firmware/mdvr/"
delete_file_path="/home/ubuntu/.nddevice/firmware/mdvr/FL3521A_4CH-V18062301-V18070301-V18112801-S24041703.25202.sw"
permission="755"

log "============ Start of copying $file_name ==========="
md5sum_local=$(md5sum $file_name | awk '{print $1}')
dest_file="${destination_path}${file_name}"

if [[ -f $dest_file ]]; then
        md5sum_system=$(md5sum $dest_file | awk '{print $1}')
        if [[ $md5sum_local == $md5sum_system ]]; then
                log "$file_name already present and up-to-date."
                status=0
        else
                log "$file_name found but md5sum mismatch, copying."
                copy_file -f $file_name -d $destination_path -b /home/ubuntu/.nddevice/backup -m $md5sum_local -p $permission -o root:root
                status=$?
        fi
else
        log "$file_name not found, copying."
        copy_file -f $file_name -d $destination_path -b /home/ubuntu/.nddevice/backup -m $md5sum_local -p $permission -o root:root
        status=$?
fi

if [[ -f $delete_file_path ]]; then
        log "Deleting old firmware file: FL3521A_4CH-V18062301-V18070301-V18112801-S24041703.25202.sw"
        rm -f "$delete_file_path"
        status1=$?
else
        log "Old firmware file not found."
        status1=0
fi

if [[ $status -eq 0 && $status1 -eq 0 ]]; then
        check_status $(basename $(pwd)) 0
else
        check_status $(basename $(pwd)) 1
fi

log "============ End of copying $file_name  ==========="
