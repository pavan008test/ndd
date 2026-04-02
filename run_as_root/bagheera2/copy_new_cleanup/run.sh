#!/usr/bin/env bash


source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling copy_file function from lib

status=$(copy_file -f cleanupstate -d /home/ubuntu/.nddevice/latest/ -b /home/ubuntu/.nddevice/backup -m 4f7742bfaaa9f19607a4da853c0bb07f -p 775 -o ubuntu:ubuntu)$?
check_status $(basename $(pwd)) $status

