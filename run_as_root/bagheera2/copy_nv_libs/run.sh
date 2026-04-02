#!/bin/env bash

set -e 

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling copy funtion from lib

CHECKSUM_libgst=$(md5sum libgstnvarguscamerasrc.so | awk -F " " '{print $1}')
CHECKSUM_libnvar=$(md5sum libnvargus.so | awk -F " " '{print $1}')
CHECKSUM_libnvsc=$(md5sum libnvscf.so | awk -F " " '{print $1}')
CHECKSUM_libgst_system=$(md5sum /usr/lib/aarch64-linux-gnu/gstreamer-1.0/libgstnvarguscamerasrc.so | awk -F " " '{print $1}')
CHECKSUM_libnvar_system=$(md5sum /usr/lib/aarch64-linux-gnu/tegra/libnvargus.so | awk -F " " '{print $1}')
CHECKSUM_libnvsc_system=$(md5sum /usr/lib/aarch64-linux-gnu/tegra/libnvscf.so | awk -F " " '{print $1}')

log "=========== Start of copy_nv_libs files ==========="

log "started copy libgstnvarguscamerasrc.so to /usr/lib/aarch64-linux-gnu/gstreamer-1.0/"
log "checking md5sum of existing libgstnvarguscamerasrc.so file"
if [[ "$CHECKSUM_libgst" == "$CHECKSUM_libgst_system" ]];
then  
log "file libgstnvarguscamerasrc.so already exist with same md5sum. Not copying...."
else
log "Checksum not matching copying the .so file"
status=$(copy_file -f libgstnvarguscamerasrc.so -d  /usr/lib/aarch64-linux-gnu/gstreamer-1.0/ -b /home/ubuntu/.nddevice/backup -m $CHECKSUM_libgst -p 755 -o root:root)$?
check_status $(basename $(pwd)) $status
fi

log "started copy libnvargus.so to /usr/lib/aarch64-linux-gnu/tegra/"
log "checking md5sum of existing libnvargus.so file"
if [[ "$CHECKSUM_libnvar" == "$CHECKSUM_llibnvar_system" ]];
then  
log "file libnvargus.so already exist with same md5sum. Not copying...."
else
log "Checksum not matching copying the .so file"
status=$(copy_file -f libnvargus.so -d  /usr/lib/aarch64-linux-gnu/tegra/  -b /home/ubuntu/.nddevice/backup -m $CHECKSUM_libnvar -p 755 -o root:root)$?
check_status $(basename $(pwd)) $status
fi

log "started copy libnvscf.so to /usr/lib/aarch64-linux-gnu/tegra/"
log "checking md5sum of existing libnvscf.so file"
if [[ "$CHECKSUM_libnvsc" == "$CHECKSUM_llibnvsc_system" ]];
then  
log "file libnvscf.so already exist with same md5sum. Not copying...."
else
log "Checksum not matching copying the .so file"
status=$(copy_file -f libnvscf.so -d  /usr/lib/aarch64-linux-gnu/tegra/  -b /home/ubuntu/.nddevice/backup  -m $CHECKSUM_libnvsc -p 755 -o root:root)$?
check_status $(basename $(pwd)) $status
fi

log "=========== End of copy nv_libs files ============"

