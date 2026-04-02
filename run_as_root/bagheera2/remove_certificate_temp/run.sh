#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "========== Start of Remove Certificate Temp =========="

FILE_PATH="/home/ubuntu/.nddevice/certificate/temp.txt"
if [ -f "$FILE_PATH" ]; then
    log "Removing file: $FILE_PATH"
    rm -rf "$FILE_PATH"
    status=$?
else
    log "File does not exist: $FILE_PATH"
    status=0
fi

check_status $(basename $(pwd)) $status

log "========== End of Remove Certificate Temp =========="
