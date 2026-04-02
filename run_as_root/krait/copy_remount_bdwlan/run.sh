#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling copy funtion from lib

md5sum_bdwlan=$(md5sum bdwlan.bin  |awk '{print $1}')

slot=$(abctl --boot_slot | tr -d "\n")

log "================Remounting and copying wlan firmware bin================="
status1=$(/bin/mount -o remount,noexec,nodev,rw,context=system_u:object_r:firmware_t:s0 -t vfat /dev/disk/by-partlabel/modem$slot /firmware)$?
status2=$(copy_file -f bdwlan.bin -d /firmware/image/ -m $md5sum_bdwlan  -p 755 -o root:root)$?

if [[ $status1 == 0 && $status2 == 0 ]]
then
        log "Remounting /firmware and copying  bdwlan.bin file successfully"
	check_status $(basename $(pwd)) 0
else
        log "Something wrong to copy bdwlan.bin file .please check !!!!"
        check_status $(basename $(pwd)) 1
fi
