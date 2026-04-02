#!/usr/bin/env bash


source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling execute_script function from lib
log "=============Copying the qti file=================="
copy_file -f com.qti.chi.override.so -d /usr/lib/hw/ -b /home/ubuntu/.nddevice/backup -m cf880feae23f2feddc620f7c2d9c285e
status=$(echo $?)	
check_status $(basename $(pwd)) $status

log "=============End of Copying the qti file===================="
