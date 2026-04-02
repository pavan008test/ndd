#!/usr/bin/env bash


source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling execute_script function from lib

status=$(sudo sed -i "/lanecal/c\lanecal = keep" /home/ubuntu/config/deviceconfig.ini)$?
check_status $(basename $(pwd)) $status
