#!/usr/bin/env bash

set -e

source  /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling copy funtion from lib

log "========= Start of copying carrier_reset_check bin to /bin/vendor/ =========="

log "Copying carrier_reset_check  file to /bin/vendor/"
file_md5sum=$(md5sum carrier_reset_check |awk '{print $1}')
system_md5sum=$(md5sum /bin/vendor/carrier_reset_check |awk '{print $1}')
if [[ $file_md5sum == $system_md5sum ]]; 
then
            log "Already updated carrier_reset_check file is present at /bin/vendor/ so not copying..."
            status=0
else
            log "copying latest carrier_reset_check to /bin/vendor/...."
            copy_file -f carrier_reset_check -d /bin/vendor/ -b /home/ubuntu/.nddevice/backup -m $file_md5sum -p 755 -o root:root
            status=$?
fi
    if [[ $status == 0 ]];
    then
            log "carrier_reset_check successfully copied"
            check_status $(basename $(pwd)) 0
    else
            log "carrier_reset_check not copied please check the logs....!!!"
            check_status $(basename $(pwd)) 1
    fi
log "================ End of copying carrier_reset_check================="
