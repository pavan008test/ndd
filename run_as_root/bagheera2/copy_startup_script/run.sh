#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#creating temp ntdi_icdc.done file @ /dev/shm/

status=$(execute_script ntdi_icdc_done.sh)$?
check_status $(basename $(pwd)) $status

