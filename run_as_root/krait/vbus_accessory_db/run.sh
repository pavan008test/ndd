#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling execute_script function from lib
log "=============STARTING VBUS_ACCESSORY ADDITION=================="

INIPATH="/data/nd_files/config/vdm_can_adapter.ini"

if [ -f "$INIPATH" ]
then 
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/home/ubuntu/.nddevice/latest/:/nd_lib/
./vbus_accessory_db 
 status1=0
else 
log "file not present"
fi
check_status $(basename $(pwd)) $status1
log "=============End of VBUS_accessory_addition===================="

