#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling execute_script function from lib

status1=$(execute_command_sync systemctl stop rpcbind)$?


status2=$(execute_command_sync systemctl disable rpcbind)$?

status3=$(execute_command_sync systemctl mask rpcbind)$?

status4=$(execute_command_sync systemctl stop rpcbind.socket)$?

status5=$(execute_command_sync systemctl disable rpcbind.socket)$?


if [[ $status1 == 0 && $status2 == 0 && $status3 == 0 && $status4 == 0 && $status5 == 0 ]]; then
check_status $(basename $(pwd)) 0

else
check_status $(basename $(pwd)) 1
fi

