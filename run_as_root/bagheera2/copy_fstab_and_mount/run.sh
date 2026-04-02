#!/usr/bin/env bash

set -e

source  /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

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

emmc_name=/dev/mmcblk
num=2   # it is for /dev/mmcblk2
emmc_name="/dev/mmcblk$num"


# Taking backup of fstab in /home/ubuntu/.nddevice/backup

file=/etc/fstab

	backup=/home/ubuntu/.nddevice/backup
	log "copying fstab file from /etc/ to $backup"

	status1=$(backup $file $backup)$?
	status_handle "$file copied to $backup" "$file not copied to $backup" $status1

# to umount /dev/mmcblk2

	sudo umount /media/data
	status1=$?
	status_handle "$emmc_name unmount successfully" "$emmc_name not unmount" $status1

# commenting /dev/mmcblk2

new_fstab=/home/ubuntu/.nddevice/ota_temp/run_as_root/copy_fstab_and_mount/fstab_new/fstab
file_md5sum=$(md5sum $new_fstab |awk '{print $1}')

	if [ -f $new_fstab ]; then
		log "copying new fstab to /etc/"
		copy_file -f fstab_new/fstab -d /etc/ -m $file_md5sum -p 644  -o root:root
		status1=$?
        else
                log "Failed to copy new fstab to /etc/"
                status1=1
	fi
	status_handle "$emmc_name commented in /etc/fstab" "Failed to comment $emmc_name in /etc/fstab" $status1

#to copy media-data.mount to /lib/systemd/system

media_file=/home/ubuntu/.nddevice/ota_temp/run_as_root/copy_fstab_and_mount/media-data.mount
file_md5sum=$(md5sum $media_file |awk '{print $1}')

	if [ -f $media_file ]; then
		log "copying media-data.mount to /lib/systemd/system/"
		copy_file -f media-data.mount -d /lib/systemd/system/ -m $file_md5sum -p 644  -o root:root
		status1=$?
        else
                log "Failed to copy media-data.mount to /lib/systemd/system/"
                status1=1
        fi
		status_handle "media-data.mount file copied in /lib/systemd/system" "Failed to copy media-data.mount file in /lib/systemd/system" $status1

# Restarting daemon

	systemctl daemon-reload

# to enable media-data.mount
	
	log "enabling media-data.mount"
	sudo systemctl enable media-data.mount
	status1=$?
	status_handle "media-data.mount successfully enabled" "Failed to enable media-data.mount" $status1

# to start media-data.mount
	
	log "starting media-data.mount"
	sudo systemctl start media-data.mount
	status1=$?
	status_handle "media-data.mount started successfully" "Failed to start media-data.mount" $status1

# To check status of media-data.mount

	log "checking status of media-data.mount"
        sudo systemctl status media-data.mount | grep -E 'active | $emmc_name /media/data'
	status1=$?
	status_handle "status checking of media-data.mount successfull" "status checking of media-data.mount failed" $status1
