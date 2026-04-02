#!/usr/bin/env bash

set -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh


log "=========Copying the latest ntdi_bag3_startup script with bt_fw update==========="
md5sum_ntdi_system=$(md5sum /etc/init.d/ntdi_bag3_startup |awk '{print $1}')
md5sum_ntdi=$(md5sum ntdi_bag3_startup |awk '{print $1}')

log "===========Copying the ntdi_bag3_start script to /etc/init.d/=========="
if [[ $md5sum_ntdi == $md5sum_ntdi_system ]] ; then
log "ntdi_bag3_start file already exist with same md5sum. Not copying...."
status1=0
else
log "Copying the latest ntdi_bag3_starup script to /etc/init.d/"
status1=$(copy_file -f  ntdi_bag3_startup  -d /etc/init.d -b /home/ubuntu/.nddevice/backup -m $md5sum_ntdi -p 755 -o root:root)$?
fi
log "========End of Copying the ntdi_bag3_startup=========="


if [[ $status1 == 0 ]]; then
log "ntdi_bag3_startup  script copied successfully"
check_status $(basename $(pwd)) 0
else
log "Something wrong to copy file.Please check !!!!"
check_status $(basename $(pwd)) 1
fi

log "====================End of copying latest ntdi_bag3_startup script===================="
