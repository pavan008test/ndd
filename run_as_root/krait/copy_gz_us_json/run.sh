#!/usr/bin/env bash

set -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling copy funtion from lib
log "============ Copying the gz_2010_us_040_00_20m.json file to config folder ==========="

CHECKSUM=$(md5sum gz_2010_us_040_00_20m.json | awk '{print $1}')

if [[ -s /data/nd_files/config/gz_2010_us_040_00_20m.json ]]
then
log "file already exist and not empty. So not copying...."
check_status $(basename $(pwd)) 0
else
status=$(copy_file -f gz_2010_us_040_00_20m.json -d /data/nd_files/config/ -b /home/ubuntu/.nddevice/backup -m $CHECKSUM)$?
check_status $(basename $(pwd))_Serial $status
fi
log "============ End of Copying the gz_2010_us_040_00_20m.json file to config folder ==========="    
