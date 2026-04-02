#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling execute_script function from lib
log "=============replace the ko file for inv-mpu-iio and inv-mpu-iio-i2c-icm20602 =================="
bash ${PWD}/replace_update_inv_ko.sh >> /data/nd_files/log/bhcopy.log 2>&1 
status=$?
check_status $(basename $(pwd)) $status
log "=============End of replacing ko file for inv-mpu-iio and inv-mpu-iio-i2c-icm20602===================="
