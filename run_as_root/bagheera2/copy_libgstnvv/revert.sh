#!/usr/bin/env bash


source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh


log "==============Start of libgstnvv4l2camerasrc.so revert================="


cp -f /home/ubuntu/.nddevice/backup/libgstnvv4l2camerasrc.so /usr/lib/aarch64-linux-gnu/gstreamer-1.0/ 

status=$?

check_status $(basename $(pwd)) $status


log "==============END of libgstnvv4l2camerasrc.so revert================="

