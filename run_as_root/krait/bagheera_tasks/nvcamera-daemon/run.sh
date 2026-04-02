#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling copy funtion from lib 

status=$(copy_file -f nvcamera-daemon.service -d /etc/systemd/system/ -b /home/ubuntu/.nddevice/backup -m eef13c9b5941a1081ee9f66875f360cc -p 664 -o root:root)$?
check_status $(basename $(pwd)) $status

