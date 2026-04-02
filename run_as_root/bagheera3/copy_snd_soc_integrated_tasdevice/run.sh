#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

file_name="snd-soc-integrated-tasdevice.ko"
destination_path="/lib/modules/4.9.299-tegra/kernel/sound/soc/codecs/tas2563"
permission="755"

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
                copy_file -f $file_name -d $destination_path -b /home/ubuntu/.nddevice/backup -m $md5sum_local -p $permission -o root:root
                status=$?
        fi
else
        log "$file_name not found, copying."
        copy_file -f $file_name -d $destination_path -b /home/ubuntu/.nddevice/backup -m $md5sum_local -p $permission -o root:root
        status=$?
fi

 check_status $(basename $(pwd)) $status

log "============ End of copying $file_name  ==========="
