#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "============ Start of copying default-wifi-powersave-on.conf ==========="

md5sum_wifi=$(md5sum default-wifi-powersave-on.conf | awk '{print $1}')
md5sum_wifi_system=$(md5sum /etc/NetworkManager/conf.d/default-wifi-powersave-on.conf | awk '{print $1}')

if [[ -f /etc/NetworkManager/conf.d/default-wifi-powersave-on.conf && $md5sum_wifi == $md5sum_wifi_system ]]; then
	log "Already Updated default-wifi-powersave-on.conf is there,So not copying"
	status=0
else
	log "Copying the latest default-wifi-powersave-on.conf to /etc/NetworkManager/conf.d/"
	copy_file -f default-wifi-powersave-on.conf -d /etc/NetworkManager/conf.d/ -m $md5sum_wifi -p 644 -o root:root
	status=$?
fi
check_status $(basename $(pwd)) $status


log "============ End of copying default-wifi-powersave-on.conf to /etc/NetworkManager/conf.d/ ==========="
