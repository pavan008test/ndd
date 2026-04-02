#!/usr/bin/env bash

set -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Script to fix the cron service

log "==============Start of Disable_Whoopsie================="

log "Adding the config to the file"
file_name=/etc/init.d/whoopsie
sync
sudo sed -i 's/.*report_crashes.*$//' $file_name
sudo sed -i -e '$areport_crashes=false' -e '/^$/d' $file_name
echo "echo '' >> /etc/init.d/whoopsie" | sudo bash
sync
log "Config is added in the file etc/init.d/whoopsie"
log "Stopping the whoopsie service"
sudo systemctl stop whoopsie.service
log "Validating the changes" 
[ `grep 'report_crashes' $file_name | awk -F '=' '{print $2}'` == "false" ]
log "Config was added successfully"
log "==============End of Disable_Whoopsie================="
			
exit 0

