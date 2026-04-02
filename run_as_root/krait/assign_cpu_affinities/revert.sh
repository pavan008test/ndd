#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "========= Reverting the CPU core affinities ========="
echo "Removing the *.service.d" >> /home/ubuntu/.nddevice/log/bhcopy.log
find /etc/systemd/system/ -type d -name "*.service.d" | xargs rm -rvf >> /home/ubuntu/.nddevice/log/bhcopy.log

status=$(echo $?)
check_status $(basename $(pwd)) $status
log "========= End of Reverting the CPU core affinities ========="
