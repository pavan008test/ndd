#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling execute_script function from lib

status=$(execute_command_sync fw_setenv bootdelay 2147483650)$?
check_status $(basename $(pwd)) $status
