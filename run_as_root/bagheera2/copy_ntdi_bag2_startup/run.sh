#!/usr/bin/env bash

set -e

source  /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

md5sum_ntdi_system=$(md5sum /etc/init.d/ntdi_bag2_startup |awk '{print $1}')
md5sum_ntdi=$(md5sum ntdi_bag2_startup |awk '{print $1}')

log "========= Start of copying ntdi_bag2_startup script into /etc/init.d/ =========="

if [[ $md5sum_ntdi_system == $md5sum_ntdi ]]; then
	
	log "Already Updated ntdi_bag2_startup is there so not copying"
	status=0
else
	log "Copying ntdi_bag2_startup to /etc/init.d"
	status=$(copy_file -f   ntdi_bag2_startup -d /etc/init.d -b /home/ubuntu/.nddevice/ -m $md5sum_ntdi -p 755 -o root:root)$?
	touch /dev/shm/ntdi_task_status_file

fi

check_status $(basename $(pwd)) $status

log "========= End of copying ntdi_bag2_startup script into /etc/init.d/ ========="
