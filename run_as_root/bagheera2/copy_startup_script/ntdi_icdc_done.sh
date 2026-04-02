#!/usr/bin/env bash
set -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#adding command to create /dev/shm/ntdi_icdc.done

log "==============Start of Copy_startup_script================="

original_startup=c5d5ae068f012aab949c5902133df9ac
updated_startup=6d7cdde4446de7e75025405e25848e0d

log "Checking the existing /etc/startup.sh file in the device"
device_startup=$(md5sum /etc/startup.sh | awk -F " " '{print $1}')
if [[ $device_startup == $original_startup ]]; then
	log "copying new startup.sh with touch command added into /etc/"
	status=$(copy_file -f startup.sh -d /etc -b /home/ubuntu/.nddevice/backup -m $updated_startup -p 755 -o root:root)$?
	if [[ $status == 0 ]]; then 
		log "Copying updated startup.sh file to /etc is done successfully"
	else
		log "Failed to copy startup.sh file"
		exit 1
	fi
else
	if [[ $device_startup == $updated_startup ]] ; then 
		log "Updated startup.sh is already present. Skipping..."
	else
		log "Md5sum of startup.sh file present in the device is not matching with original or update ref file md5sum. Failing..."
		exit 1
	fi
fi

#This below touch is for the first time service start after update. 
if [ ! -f "/dev/shm/ntdi_icdc.done" ]; then 
	touch /dev/shm/ntdi_icdc.done
fi
log "==============End of Copy_startup_script================="
