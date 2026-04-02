#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling copy funtion from lib
log "=========copying nd_dmcapture.sh file to /home/ubuntu/dm_logging/==========="
md5sum_dmcapture=$(md5sum nd_dmcapture.sh | awk -F " " '{print $1}')

status=$(copy_file -f nd_dmcapture.sh -d /home/ubuntu/dm_logging/ -b /home/ubuntu/.nddevice/backup -m ${md5sum_dmcapture} -p 755 -o root:root)$?
check_status $(basename $(pwd)) $status

log "===========End of coping nd_dmcapture.sh file==============="
