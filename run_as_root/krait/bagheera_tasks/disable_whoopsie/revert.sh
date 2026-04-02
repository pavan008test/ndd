#!/usr/bin/env bash

set -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Script to fix the whoopsie

log "==============Start of Disable_Whoopsie================="

log "removing the config to the file"
file_name=/etc/init.d/whoopsie
sudo sed -i 's/.*report_crashes.*$//' $file_name 
sync
log "Config is removed in the file etc/init.d/whoopsie"
log "Stopping the whoopsie service"
sudo systemctl stop whoopsie.service
log "Validating the changes" 
[ ! `grep 'report_crashes' $file_name` ]
log "Config was removed successfully"
log "==============End of revert Disable_Whoopsie================="
			
exit 0
