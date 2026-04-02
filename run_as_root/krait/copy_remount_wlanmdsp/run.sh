#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

md5sum_wlanmdsp=$(md5sum wlanmdsp.mbn  |awk '{print $1}')

slot=$(abctl --boot_slot | tr -d "\n")

log "================Remounting and copying wlanmdsp.mbn firmware bin================="
status1=$(/bin/mount -o remount,noexec,nodev,rw,context=system_u:object_r:firmware_t:s0 -t vfat /dev/disk/by-partlabel/modem$slot /firmware)$?
status2=$(copy_file -f wlanmdsp.mbn -d /firmware/image/ -m $md5sum_wlanmdsp  -p 755 -o root:root)$?
log "Remount status: $status1, Copy status: $status2"
sleep 2

if [[ $status1 == 0 && $status2 == 0 ]]
then
        log "firmware remounted and wlanmdsp.mbn copied successfully"
        log "Wi-Fi firmware upgrade completed, syncing..."
        sync
        log "sync completed..."
	check_status $(basename $(pwd)) 0
else
        log "Failed to remount firmware or copy wlanmdsp.mbn"
        check_status $(basename $(pwd)) 1
fi
log "================ wlanmdsp.mbn firmware update script completed ================="
