#!/usr/bin/env bash

set -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling copy funtion from lib

log "SanDiskTool folder is copying to .nddevice folder"
cp -rpf SanDiskTool /home/ubuntu/.nddevice/
status=$?
check_status $(basename $(pwd)) $status

