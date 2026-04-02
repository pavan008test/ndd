#!/usr/bin/env bash


source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling execute_script function from lib

status=$(execute_command_sync pip install -I psutil-5.3.1-cp27-cp27mu-linux_aarch64.whl)$?
check_status $(basename $(pwd)) $status
