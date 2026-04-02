#!/usr/bin/env bash


source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling copy funtion from lib 

status=$(copy_file -f rsyslog -d /etc/logrotate.d/ -b /home/ubuntu/.nddevice/backup -m 355f96f7d0b3674ef27fd40c81751352 -p 644 -o root:root)$?
check_status $(basename $(pwd))_rsyslog $status
status=$(copy_file -f logrotate -d /etc/cron.d/ -b /home/ubuntu/.nddevice/backup -m bd0bdca869c370b0154a08934e78bfcd -p 644 -o root:root)$?
check_status $(basename $(pwd))_logrotate $status

