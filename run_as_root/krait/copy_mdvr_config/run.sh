#!/usr/bin/env bash

set -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling copy funtion from lib

CHECKSUM=$(md5sum mdvr_config.ini | awk '{print $1}')

if [[ -s /home/ubuntu/.nddevice/mdvr_config.ini ]]
then 
log "Mdvr_config file already exist and not empty. So not copying...."
execute_command "chown root:root /home/ubuntu/.nddevice/mdvr_config.ini"
else
copy_file -f mdvr_config.ini -d /home/ubuntu/.nddevice/ -b /home/ubuntu/.nddevice/backup -m $CHECKSUM
fi
