#!/usr/bin/env bash

set -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling copy funtion from lib
checksum=$(md5sum mdvr_config.ini | awk '{print $1}')
if [[ -s /home/ubuntu/.nddevice/mdvr_config.ini ]]
then 
log "Mdvr_config file already exist and not empty. So not copying...."
else
status=$(copy_file -f mdvr_config.ini -d /home/ubuntu/.nddevice/ -b /home/ubuntu/.nddevice/backup -m $checksum -p 775  -o ubuntu:ubuntu)$?
check_status $(basename $(pwd)) $status
fi


