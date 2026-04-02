#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling copy funtion from lib 

log "=================Start of copying the lte_gps_sample_app to /bin folder=============="
md5sum_lte_gps_sample_app=$(md5sum lte_gps_sample_app | awk -F " " '{print $1}')
status=$(copy_file -f lte_gps_sample_app -d /bin/vendor/ -b /home/ubuntu/.nddevice/backup -m $md5sum_lte_gps_sample_app -p 755 -o root:root)$?
status1=$(sudo ln -sf vendor/lte_gps_sample_app /bin/lte_gps_sample_app)$?
check_status $(basename $(pwd)) $status
check_status $(basename $(pwd)) $status1
log "=================End of copying the lte_gps_sample_app to /bin folder=============="
