#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "========= start copying early-slot-success.service and slotchangerofix.sh ==========="
log "copying early-slot-success.service"

#Calling function from lib to add  the early-slot-success.service @ /etc/systemd/system/
FILE_CHECKSUM=$(md5sum early-slot-success.service | awk -F " " '{print $1}')
DEST_FILE="/etc/systemd/system/early-slot-success.service"

# Check if destination file exists and compare checksums
if [[ -f "$DEST_FILE" ]]; then
    DEST_CHECKSUM=$(md5sum "$DEST_FILE" | awk -F " " '{print $1}')
    if [[ "$FILE_CHECKSUM" == "$DEST_CHECKSUM" ]]; then
        log "$DEST_FILE already exists with matching checksum. Skipping copy."
        status1=0
    else
        log "Checksum mismatch. Copying early-slot-success.service to /etc/systemd/system/"
        copy_file -f early-slot-success.service -d /etc/systemd/system/ -m ${FILE_CHECKSUM} -p 775 -o root:root
        status1=$?
    fi
else
    log "Copying early-slot-success.service to /etc/systemd/system/"
    copy_file -f early-slot-success.service -d /etc/systemd/system/ -m ${FILE_CHECKSUM} -p 775 -o root:root
    status1=$?
fi

file_name="slotchangerofix.sh"
destination_path="/data/"

log "copying slotchangerofix.sh"
md5sum_local=$(md5sum $file_name | awk '{print $1}')
dest_file="${destination_path}${file_name}"

if [[ -f $dest_file ]]; then
        md5sum_system=$(md5sum $dest_file | awk '{print $1}')
        if [[ $md5sum_local == $md5sum_system ]]; then
                log "$file_name already exists with matching checksum. Skipping copy."
		status2=0
        else
                log "Checksum mismatch. Copying slotchangerofix.sh to /data/"
                copy_file -f $file_name -d $destination_path -b /home/ubuntu/.nddevice/backup -m $md5sum_local -p 775 -o root:root
                status2=$?
        fi
else
        log "$file_name not found, copying."
        copy_file -f $file_name -d $destination_path -b /home/ubuntu/.nddevice/backup -m $md5sum_local -p 775 -o root:root
        status2=$?
fi

if [[ $status1 == 0 && $status2 == 0 ]] ; then
        log "early-slot-success.service and slotchangerofix.sh copy completed successfully. status1=$status1, status2=$status2"
        status=0
        check_status $(basename $(pwd)) $status
else
        log "early-slot-success.service and slotchangerofix.sh copy failed. status1=$status1, status2=$status2"
        status=1
        check_status $(basename $(pwd)) $status
fi
log "========= End of copying early-slot-success.service and slotchangerofix.sh ==========="
