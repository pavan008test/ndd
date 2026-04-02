#!/usr/bin/env bash

set -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling copy funtion from lib

CHECKSUM_gstlibnvv=$(md5sum libgstnvv4l2camerasrc.so | awk -F " " '{print $1}')

checksum_gstlibnvv_device=$(md5sum /usr/lib/aarch64-linux-gnu/gstreamer-1.0/libgstnvv4l2camerasrc.so | awk -F " " '{print $1}')

log "=============== Start of Copying libgstnvv4l2camerasrc.so  ================"


if [[ $checksum_gstlibnvv_device != $CHECKSUM_gstlibnvv ]]; then
log "Copying libgstnvv4l2camerasrc.so"
status=$(copy_file -f libgstnvv4l2camerasrc.so -d /usr/lib/aarch64-linux-gnu/gstreamer-1.0/ -b /home/ubuntu/.nddevice/backup -m $CHECKSUM_gstlibnvv -p 755 -o root:root)$?
check_status $(basename $(pwd)) $status
else
log "libgstnvv4l2camerasrc.so lib is already present. Skipping the copy..."
fi

log "=============== End of copying libgstnvv4l2camerasrc.so ================"
