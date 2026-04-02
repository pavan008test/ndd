#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

files=(
    "dms_i2c"
    "dms_flash"
    "dms_irled_status"
    "dms_set_label"
    "dms_sku_snpn"
    "dms_sonix"
    "dms_uvc"
)

destination_path="/bin/vendor"
permission="755"

status_array=()

for file_name in "${files[@]}"; do
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
	    log "Copy status of $file_name : $status"
        fi
    else
        log "$file_name not found, copying."
        copy_file -f $file_name -d $destination_path -b /home/ubuntu/.nddevice/backup -m $md5sum_local -p $permission -o root:root
        status=$?
	log "Copy status of $file_name : $status"
    fi

    status_array+=($status)
done

final_status=0
for s in "${status_array[@]}"; do
    if [[ "$s" -ne 0 ]]; then
        final_status=1
        break
    fi
done

log "Status array: ${status_array[*]}"
log "Final status: $final_status"

check_status $(basename $(pwd)) $final_status

log "============ End of copying all files ==========="
