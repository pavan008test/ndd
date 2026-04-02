#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling copy funtion from lib 

log "============= copy reboot commands script ============="
#md5sum_command=$(md5sum command.sh | awk -F " " '{print $1}')

#status=$(copy_file -f command.sh -d /home/ubuntu/.nddevice -b /home/ubuntu/.nddevice/backup -m ${md5sum_command} -p 775 -o ubuntu:ubuntu)$?
## Running this for the first to get the info before reboot
#sudo /home/ubuntu/.nddevice/command.sh --version_info

log "Copying reboot_trigger script and its service file"
## Copying a service file and script for rebooting the device after OS-OTA
md5sum_reboottrigger=$(md5sum reboot_trigger.sh | awk -F " " '{print $1}')
status=$(copy_file -f reboot_trigger.sh -d /home/ubuntu/.nddevice -m ${md5sum_reboottrigger} -p 775 -o ubuntu:ubuntu)$?
cp -f onetimereboot.service /etc/systemd/system/
## Starting the onetimereboot service with 300secs sleep
log "Starting the onetimereboot service"
sudo systemctl start onetimereboot.service
touch /home/ubuntu/.nddevice/com_root
check_status $(basename $(pwd)) $status
log "============== End of copy reboot commands script =============="
