#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "============ Start of copying libzbar.so ==========="

md5sum_libzbar=$(md5sum libzbar.so | awk '{print $1}')
md5sum_libzbar_system=$(md5sum /usr/lib/aarch64-linux-gnu/libzbar.so | awk '{print $1}')

log "start of Copying the libzbar.so"
if [[ -f /usr/lib/aarch64-linux-gnu/libzbar.so && $md5sum_libzbar == $md5sum_libzbar_system ]]; then
    log "Already Updated libzbar.so is there, So not copying"
    status=0
else
    log "libzbar.so is missing or md5 mismatch. Copying..."
    copy_file -f libzbar.so -d /usr/lib/aarch64-linux-gnu/ -b /home/ubuntu/.nddevice/backup -m $md5sum_libzbar -p 755 -o root:root
    status=$?
fi

check_status $(basename $(pwd)) $status
log "============End of copying the libzbar.so=========="

