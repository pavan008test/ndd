#!/usr/bin/env bash


source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling copy funtion from lib

status=$(copy_file -f boot_logs.conf -d /var/log -b /home/ubuntu/.nddevice/backup -m 40eb93993aab20867dc0f4365ffc7a11 -p 664 -o root:root)$?
check_status $(basename $(pwd)) $status
