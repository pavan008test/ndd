#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "####### Removing all systemd override files  ######"
echo "Removing the *.service.d" >> /home/ubuntu/.nddevice/log/bhcopy.log
find /etc/systemd/system/ -type d -name "*.service.d" | xargs rm -rvf >> /home/ubuntu/.nddevice/log/bhcopy.log
status1=$?

if [[ $status1 == 0 ]];then
    log "Successfully executed"
    check_status $(basename $(pwd)) 0
    else
    log "Something goes wrong,please check...!"	    
    check_status $(basename $(pwd)) 1
fi
