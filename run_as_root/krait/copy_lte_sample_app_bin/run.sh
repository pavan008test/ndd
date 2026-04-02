#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

md5sum_sample_app=$(md5sum lte_gps_sample_app |awk '{print $1}')

#Calling execute_script function from lib
log "=============Copying the lte_gps_sample_app Binary into /usr/bin/ folder=================="
status=$(copy_file -f lte_gps_sample_app -d /usr/bin/ -b /home/ubuntu/.nddevice/backup -m ${md5sum_sample_app} -p 775 -o root:root)$?
check_status $(basename $(pwd)) $status
log "=============End of lte_gps_sample_app Binary===================="
