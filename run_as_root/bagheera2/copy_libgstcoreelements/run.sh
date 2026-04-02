#!/usr/bin/env bash

set -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling copy funtion from lib

CHECKSUM_gstlibcore=$(md5sum libgstcoreelements.so | awk -F " " '{print $1}')

checksum_gstlibcore_device=$(md5sum /usr/lib/aarch64-linux-gnu/gstreamer-1.0/libgstcoreelements.so | awk -F " " '{print $1}')

log "=============== Copy libgstcoreelements.so start ================"


if [[ $checksum_gstlibcore_device != $CHECKSUM_gstlibcore ]]; then
log "Copying libgstcoreelements.so"
status=$(copy_file -f libgstcoreelements.so -d /usr/lib/aarch64-linux-gnu/gstreamer-1.0/ -b /home/ubuntu/.nddevice/backup -m $CHECKSUM_gstlibcore -p 644 -o root:root)$?
check_status $(basename $(pwd)) $status
else
log "libgstcoreelements.so lib is already present. Skipping the copy..."
fi

log "=============== End of copy gst libs ================"
