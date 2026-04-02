#!/usr/bin/env bash


source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling execute_script function from lib

status=$(execute_command_sync sudo /usr/bin/nd_service.sh -c clean -n nd_shutdown -p /home/ubuntu/.nddevice/latest/service/)$?
check_status $(basename $(pwd)) $status

