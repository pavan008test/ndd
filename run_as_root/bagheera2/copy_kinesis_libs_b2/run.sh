#!/usr/bin/env bash


source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh


#Calling execute_script function from lib
log "=============Copying the kinesis libs=================="

md5sum_cacert=$(md5sum cacert.pem |awk '{print $1}')

cp -Pf ./lib* /usr/local/lib/
check_status $(basename $(pwd))_lib $status

status=$(copy_file -f cacert.pem -d  /home/ubuntu/.nddevice/certificate/ -b /home/ubuntu/.nddevice/backup -m ${md5sum_cacert} -p 755 -o ubuntu:ubuntu)$?
check_status $(basename $(pwd))_cacert $status

log "=============End of Copying the kinesis libs===================="
