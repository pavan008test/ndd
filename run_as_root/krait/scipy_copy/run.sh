#!/usr/bin/env bash


source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling execute_script function from lib
log "=============Copying the scipy=================="
rm -rf /usr/lib64/python3.5/site-packages/scipy_tools*
status=$(echo $?)	
check_status "remove_existing_scipy" $status
cp -rf scipy_* /usr/lib64/python3.5/site-packages/
status=$(echo $?)	
check_status "copy_new_scipy_folder" $status
log "=============End of Copying the scipy===================="
