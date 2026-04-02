#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

status=$(execute_script emmc_bootfix_update.sh)$?
check_status $(basename $(pwd)) $status

