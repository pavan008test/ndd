#!/usr/bin/env bash

set -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh


# pass log message as parameter
# parameter $1 is for success and parameter $2 is for failure
# parameter $3 is for status1     0 or 1

function status_handle {
		if [[ $3 == 0 ]]; then
			log $1
			check_status $(basename $(pwd)) 0
		else
			log $2
			check_status $(basename $(pwd)) 1
		fi
}

# putting fstab at /etc/ from backup

backup_fstab=/home/ubuntu/.nddevice/ota_temp/run_as_root/copy_fstab_and_mount/fstab_old/fstab
file_md5sum=$(md5sum $backup_fstab |awk '{print $1}')
system_md5sum=$(md5sum /etc/fstab |awk '{print $1}')
    
	if [[ $file_md5sum != $system_md5sum ]]; then
		log "copying fstab from $backup_fstab to /etc"
		copy_file -f fstab_old/fstab -d /etc/ -m $file_md5sum -p 644  -o root:root
		status1=$?
	else
		log "Already uncomment fstab is present in /etc"
		status1=0
	fi
		status_handle "fstab copied to /etc/" "failed to copy fstab" $status1

# removing media-data.mount from /lib/systemd/system

media_file=/lib/systemd/system/media-data.mount

	if [ -f $media_file ]; then
		log "stoping and disabling media-data.mount"
		sudo systemctl stop media-data.mount
                status1=$?
		status_handle "media-data.mount stop" "failed to stop media-data.mount" $status1
		sudo systemctl disable media-data.mount
		status1=$?
                status_handle "media-data.mount disable" "failed to disable media-data.mount" $status1
                sudo rm $media_file
		status1=$?
		status_handle "media-data.mount removed from /lib/systemd/system/" "failed to remove media-data.mount from /lib/systemd/system/" $status1
	else
		log "media-data.mount file not present in /lib/systemd/system/"
		status1=0
	fi

# to start fstab again 

        mount -a
	systemctl daemon-reload

