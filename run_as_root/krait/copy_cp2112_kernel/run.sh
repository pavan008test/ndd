#!/usr/bin/env bash


source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

md5sum_hid_cp2112=$(md5sum hid-cp2112.ko |awk '{print $1}')

#Calling execute_script function from lib
log "=============Copying the hid-cp2112 kernel=================="
copy_file -f hid-cp2112.ko -d /usr/lib/modules/4.9.160-perf/kernel/drivers/hid/ -b /home/ubuntu/.nddevice/backup -m ${md5sum_hid_cp2112} -p 644
status=$(echo $?)
check_status $(basename $(pwd)) $status
rmmod hid-cp2112
sleep 1
insmod /usr/lib/modules/4.9.160-perf/kernel/drivers/hid/hid-cp2112.ko

log "=============End of Copying the hid-cp2112 kernel===================="
