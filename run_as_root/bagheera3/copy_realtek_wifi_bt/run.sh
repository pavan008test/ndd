#!/usr/bin/env bash


source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

# Copying files with checks and inserting modules
log "========= Start of copying Realtek wifi module =========="

CHECKSUM_wifi=$(md5sum rtl8822ce_wifi_module.ko |awk '{print $1}')
CHECKSUM_wifi_system=$(md5sum /lib/modules/misc/rtl8822ce_wifi_module.ko |awk '{print $1}')

if [[ $CHECKSUM_wifi == $CHECKSUM_wifi_system ]] ; then
	log "already updated rtl8822ce_wifi_module.ko is exists,so skipping."
	status1=0
else
	log "Copying the rtl8822ce_wifi_module.ko file"
	status1=$(copy_file -f rtl8822ce_wifi_module.ko -d /lib/modules/misc/ -m $CHECKSUM_wifi -p 644 -o root:root)$?
fi

if [[ $status1 == 0 ]];then
    log "Copied rtl8822ce_wifi_module.ko successfully"
    check_status $(basename $(pwd)) 0
    else
    log "something went wrong,please check" 
    check_status $(basename $(pwd)) 1
fi
log "========= End of copying Realtek wifi/BT modules =========="
