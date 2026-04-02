#!/usr/bin/env bash

set -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "========== start of nd_sdcard directory create in /data/nd_files/ path script ==========="

if [[ -d /data/nd_files/nd_sdcard ]]
then 
log "Directory already exist and So not creating...."
status=0
else
log "creating nd_sdcard folder in /data/nd_files/ path"
mkdir -p /data/nd_files/nd_sdcard
status=$(echo $?)
fi

check_status $(basename $(pwd)) $status
log "============= End of nd_sdcard directory create in /data/nd_files/ path script  ==================="

