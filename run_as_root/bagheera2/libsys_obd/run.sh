#!/usr/bin/env bash


source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling copy funtion from lib

status=$(copy_file -f libsys_obd.so -d /lib/ -b /home/ubuntu/.nddevice/backup -m fd3ba0186a577ac192d292c9d51561a5 -p 755 -o root:root)$?
check_status $(basename $(pwd)) $status

