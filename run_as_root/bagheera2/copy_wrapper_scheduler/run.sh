#!/usr/bin/env bash


source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling copy_file function from lib

status=$(copy_file -f wrapper_scheduler -d /home/ubuntu/bin -m 7cd58d67abbb384f55952e4ef3246a4b -p 775 -o ubuntu:ubuntu)$?
check_status $(basename $(pwd)) $status

