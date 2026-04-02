#!/usr/bin/env bash


source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling execute_script function from lib
log "=============Command for Sdcard Mount=================="
log "Removing th files from /home/iriscli/internal_buff/"
execute_command rm -rf /home/iriscli/internal_buff/*
status=$(echo $?)
check_status $(basename $(pwd)) $status
execute_command touch /data/nd_files/nd_sdcard.img
status=$(echo $?)
check_status $(basename $(pwd)) $status
execute_command truncate -s 40G /data/nd_files/nd_sdcard.img
status=$(echo $?)
check_status $(basename $(pwd)) $status

log "Unmounting the /media/SdCard"
if [ -n "`lsblk |grep "/media/SdCard"`" ]
then
umount /media/SdCard/
    if [ $? = 0 ]
    then
    log "Unmounting the sdcard successful"
    else
    log "Unmounting not successful"
    fi
else
log "/media/SdCard/ is not mounted"
fi

echo y | ./mkfs.ext4 /data/nd_files/nd_sdcard.img 2>&1 >>/home/ubuntu/.nddevice/log/bhcopy.log
status=$(echo $?)
check_status $(basename $(pwd)) $status
log "=============End of Command for Sdcard Mount==================="

