#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#calling copy function from lib
log "============= Copy wifi kernel files =================="

status=$(execute_script kernel_copy.sh)$?
check_status $(basename $(pwd)) $status

log "============= End of Copy wifi kernel files ===================="
 
