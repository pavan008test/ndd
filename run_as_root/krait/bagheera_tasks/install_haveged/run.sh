#!/usr/bin/env bash


source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling execute_script function from lib

status=$(execute_command_sync sudo dpkg -i libhavege1_1.9.1-3_arm64.deb)$?
if [[ -n $(sudo dpkg -l | grep libhavege1) ]]
then
	log "libhavege1 library was installed successfully"
else
	log "libhavege1 was not installed. Please check!!!!!!!!! "
fi

status=$(execute_command_sync sudo dpkg -i haveged_1.9.1-3_arm64.deb)$?
if [[ -n $(sudo dpkg -l | grep haveged) ]]
then
	log "haveged library was installed successfully"
else
	log "haveged was not installed. Please check!!!!!!!!! "
fi
