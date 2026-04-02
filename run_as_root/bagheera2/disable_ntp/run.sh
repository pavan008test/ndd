#!/usr/bin/env bash


source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling execute_script function from lib

status=$(execute_command_sync sudo timedatectl set-ntp false)$?
check_status $(basename $(pwd)) $status

