#!/usr/bin/env bash


source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling execute_script function from lib

status=$(execute_command_sync pip install /home/ubuntu/.nddevice/ota_temp/run_as_root/pyzmq_install/pyzmq-16.0.3-cp27-cp27mu-linux_aarch64.whl)$?
check_status $(basename $(pwd)) $status
