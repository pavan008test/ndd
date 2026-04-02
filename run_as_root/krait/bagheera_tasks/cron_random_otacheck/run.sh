#!/usr/bin/env bash


source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling execute_script function from lib

status=$(execute_script cron_random_otacheck.sh)$?
check_status $(basename $(pwd)) $status
