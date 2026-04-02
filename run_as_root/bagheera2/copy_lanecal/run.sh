#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling copy funtion from lib 
md5sum_lanecal=$(md5sum laneCal.json | awk -F " " '{print $1}')

status=$(copy_file -f laneCal.json -d /home/ubuntu/config/ -b /home/ubuntu/.nddevice/backup -m ${md5sum_lanecal} -p 664 -o ubuntu:ubuntu)$?
check_status $(basename $(pwd)) $status

