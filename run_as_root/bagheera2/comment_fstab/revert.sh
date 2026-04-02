#!/usr/bin/env bash

set -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#unCommenting fstab last line

log "========= Start of revert modify fstab =========="
last_char=$(tail -1 /etc/fstab | cut -c '1')
if [[ $last_char == "#" ]]; then
log "Reverting comment in last line"
sed -i '/media/s/^#//' /etc/fstab
status=$?
else
log "comment already reverted in last line"
status=0
fi
check_status $(basename $(pwd)) $status

log "========= End of revert modify fstab ========="
