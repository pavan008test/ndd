#!/usr/bin/env bash

set -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling copy funtion from lib

if [[ -s /home/ubuntu/.nddevice/mdvr_config.ini ]]
then 
log "Mdvr_config file already exist and not empty. So not copying...."
else
copy_file -f mdvr_config.ini -d /home/ubuntu/.nddevice/ -b /home/ubuntu/.nddevice/backup -m dd830fa2cdc2338ee6c884d2f3ceaccb -p 775  -o ubuntu:ubuntu
fi


