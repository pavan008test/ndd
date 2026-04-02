#!/usr/bin/env bash


source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling copy funtion from lib

md5sum_libcproducer=$(md5sum libcproducer.so  |awk '{print $1}')
md5sum_libcproducer_system=$(md5sum /nd_lib/libcproducer.so |awk '{print $1}')

log "============ Copying the kinesis lib libcproducer.so file ==========="
log "checking md5sum of existing /nd_lib/libcproducer.so file"
if [[ "$md5sum_libcproducer" == "$md5sum_libcproducer_system" ]]; then

log "libcproducer.so file already exist with same md5sum. Not copying...."
else
log "Checksum not matching copying the so file"
status=$(copy_file -f libcproducer.so -d /nd_lib -b /home/ubuntu/.nddevice/backup -m $md5sum_libcproducer  -p 755 -o root:root)$?
check_status $(basename $(pwd)) $status
fi

log "================End of copying the kinesis lib libcproducer.so file ==========="






