#!/usr/bin/env bash


source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling execute_script function from lib
log "=============Copying the scipy=================="
cp -rf scip* /usr/local/lib/python3.9/site-packages/
status=$(echo $?)	
check_status "copy_new_scipy_folder" $status
log "=============End of Copying the scipy===================="
