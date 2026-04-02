#!/usr/bin/env bash

set -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Commenting fstab last line

log "========= Start of modify fstab =========="
last_char=$(tail -1 /etc/fstab | cut -c '1')
if [[ $last_char != "#" ]]; then
log "Adding comment to the last line"
sed -i '21,21 s/^/#/' /etc/fstab
status=$?
else
log "Already comment exist"
status=0
fi
check_status $(basename $(pwd)) $status

log "========= End of modify fstab ========="
