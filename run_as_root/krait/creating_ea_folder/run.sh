#!/usr/bin/env bash

set -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "========== Creating ea folder in /data/nd_files/nd_sdcard folder for Event access feature  ==========="

if [[ ! -d /data/nd_files/nd_sdcard/ea ]]; then 
    log "Creating ea folder in /data/nd_files/nd_sdcard/ path"
    mkdir -p /data/nd_files/nd_sdcard/ea
    status=$?
else
    log " ea directory already exists, not creating."
    status=0
fi

if [[ $status == 0 ]]; then
        log "Creating ea folder in /data/nd_files/nd_sdcard path"
else
        log "Something went wrong while creating ea folder in /data/nd_files/nd_sdcard path. Please Check...!"
fi
check_status $(basename $(pwd)) $status
log "========= End of creating ea folder in /data/nd_files/nd_sdcard folder for Event access feature ========="




