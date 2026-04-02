#!/usr/bin/env bash

set -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling copy funtion from lib

#CHECKSUM_gst=$(md5sum libgstvideo4linux2.so | awk -F " " '{print $1}')
CHECKSUM_gstlive=$(md5sum libgstnvvideo4linux2.so | awk -F " " '{print $1}')

#checksum_gst_device=$(md5sum /usr/lib/aarch64-linux-gnu/gstreamer-1.0/libgstvideo4linux2.so | awk -F " " '{print $1}')
checksum_gstlive_device=$(md5sum /usr/lib/aarch64-linux-gnu/gstreamer-1.0/libgstnvvideo4linux2.so | awk -F " " '{print $1}')

log "=============== Copy gst libs start ================"

#if [[ $checksum_gst_device != "4cb17388754532b090d14297aef5ef01" ]]; then
#log "Copying Hdmaps gst lib"
#status=$(copy_file -f libgstvideo4linux2.so -d /usr/lib/aarch64-linux-gnu/gstreamer-1.0/ -b /home/ubuntu/.nddevice/backup -m $CHECKSUM_gst -p 644 -o root:root)$?
#check_status $(basename $(pwd))_hdmaps $status
#else
#log "HDmaps gst lib is already present. Skipping the copy..."
#fi

if [[ $checksum_gstlive_device != "dec549691a6b5ffdd3e017add3574003" ]]; then
log "Copying live streaming gst lib"
status=$(copy_file -f libgstnvvideo4linux2.so -d /usr/lib/aarch64-linux-gnu/gstreamer-1.0/ -b /home/ubuntu/.nddevice/backup -m $CHECKSUM_gstlive -p 644 -o root:root)$?
check_status $(basename $(pwd))_livest $status
else
log "Live streaming gst lib is already present. Skipping the copy..."
fi

log "=============== End of copy gst libs ================"
