#!/usr/bin/env bash


source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling execute_script function from lib
log "=============Installing the 7zip software=================="
status=$(execute_command_sync sudo dpkg -i p7zip-full_16.02+dfsg-6_arm64.deb)$?
if [[ -n $(sudo dpkg -l | grep p7zip-full) ]]
then
	log "p7zip-full package was installed successfully"
else
	log "p7zip-full was not installed. Please check!!!!!!!!! "
	check_status $(basename $(pwd)) $status
fi
log "=============End of Installing 7zip==================="
