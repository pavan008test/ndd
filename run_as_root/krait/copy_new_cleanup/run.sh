#!/usr/bin/env bash


source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling copy_file function from lib

status=$(copy_file -f cleanupstate -d /home/ubuntu/.nddevice/latest/ -b /home/ubuntu/.nddevice/backup -m f92073e679f20297d7466cd36c83b385 -p 755 -o root:root)$?
check_status $(basename $(pwd)) $status

