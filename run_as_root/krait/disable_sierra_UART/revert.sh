#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#calling copy function from lib
log "============= reverting disabling of UART on Sierra =================="

execute_script enable_sierra_uart.sh
status=$?
check_status $(basename $(pwd)) $status

log "============= End of revert disabling of UART on Sierra ===================="
 
