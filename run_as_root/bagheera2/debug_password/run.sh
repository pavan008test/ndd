#!/usr/bin/env bash


source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling copy funtion from lib 

status=$(copy_file -f autologin.conf -d /etc/systemd/system/serial-getty@ttyS0.service.d -b /home/ubuntu/.nddevice/backup -m 65f91bce4b6fd230c1fed3fe530e7d56 -p 664 -o root:root)$?
check_status $(basename $(pwd)) $status
