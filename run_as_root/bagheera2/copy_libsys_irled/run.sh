#!/usr/bin/env bash


source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling copy funtion from lib

log "================start of copying libsys.so=============="

CHECKSUM=$(md5sum libsys.so |awk '{print $1}')
status=$(copy_file -f libsys.so -d /lib/ -b /home/ubuntu/.nddevice/backup -m ${CHECKSUM} -p 755 -o root:root)$?
check_status $(basename $(pwd)) $status

log "================End of copying libsys.so================"
