#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling execute_script function from lib
log "=============Copying the inward_sensor file=================="
copy_file -f com.qti.sensormodule.vvdn_b_ov2735.bin -d /usr/lib/camera/ -b /home/ubuntu/.nddevice/backup -m 564fbbbdc5c8235b9192dd539a2c589b -p 755
status=$(echo $?)
check_status $(basename $(pwd)) $status

log "=============End of Copying the inward_sensor file===================="
