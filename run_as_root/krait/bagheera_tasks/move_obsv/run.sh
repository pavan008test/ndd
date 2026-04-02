#!/usr/bin/env bash


source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling execute_script function from lib

status=$(execute_command_sync sudo mv /media/SdCard/observations/observations.zip /home/ubuntu/.nddevice/)$?
if [[ $status == 0 ]]
then
	log "Observation has moved successfully"
	check_status $(basename $(pwd)) 0
else
	log "Observations has not found or failed to move"
	check_status $(basename $(pwd)) 0
fi
