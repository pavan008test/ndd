#!/usr/bin/env bash

source  /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling copy funtion from lib 

md5sum_json=$(md5sum gz_2010_us_040_00_20m.json | awk -F " " '{print $1}')

log "====================Start of copying gz_2010_us_040_00_20m.json and installing shapely==2.0.1====="
log "copying gz_2010_us_040_00_20m.json"
status1=$(copy_file -f gz_2010_us_040_00_20m.json  -d /home/ubuntu/config/ -b /home/ubuntu/.nddevice/backup -m ${md5sum_json} -p 664 -o ubuntu:ubuntu)$?
log "installing shapely==2.0.1"
pip3 install shapely-2.0.1-cp39-cp39-manylinux_2_17_aarch64.manylinux2014_aarch64.whl
status2=$?

if [[ $status1 == 0 && $status2 == 0 ]]; then
check_status $(basename $(pwd)) 0
else
check_status $(basename $(pwd)) 1
fi
log "=========== End of the copying gz_2010_us_040_00_20m.json and installing shapely==2.0.1 ==========="
