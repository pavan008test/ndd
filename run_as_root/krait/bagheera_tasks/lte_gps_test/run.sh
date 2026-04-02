#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling copy funtion from lib 

status=$(copy_file -f lte_gps_test -d /bin/ -b /home/ubuntu/.nddevice/backup -m 7f7cbe4fecf5646edc843191e79a37ea -p 555 -o root:root)$?
check_status $(basename $(pwd)) $status
