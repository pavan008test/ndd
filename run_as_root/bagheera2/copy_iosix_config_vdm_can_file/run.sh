#!/usr/bin/env bash

set -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling copy funtion from lib

CHECKSUM_vdm_can_adapter=$(md5sum vdm_can_adapter.ini | awk '{print $1}')
CHECKSUM_iosix_config=$(md5sum iosix_config.ini | awk '{print $1}')

log "=====Checks to copy vdm_can_adapter.ini file=========="
if [[ -s /home/ubuntu/config/vdm_can_adapter.ini ]]
then 
log "vdm_can_adapter.ini file already exist and not empty. So not copying...."
status1=0
else
status1=$(copy_file -f vdm_can_adapter.ini -d /home/ubuntu/config/ -m ${CHECKSUM_vdm_can_adapter} -p 755 -o root:root)$?
fi

log "=====Checks to copy iosix_config.ini file=========="
if [[ -s /home/ubuntu/config/iosix_config.ini ]]
then 
log "iosix_config.ini file already exist and not empty. So not copying...."
status2=0
else
status2=$(copy_file -f iosix_config.ini -d /home/ubuntu/config/ -m ${CHECKSUM_iosix_config} -p 755 -o root:root)$?
fi

if [[ $status1 == 0 && $status2 == 0 ]]; then	
check_status $(basename $(pwd)) 0
else
check_status $(basename $(pwd)) 1
fi


