#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh


log "=================Start of deleting cam_rec service ram file======================"

if [[ -f /dev/shm/nd_files_c/cam_rec_service_started ]]; then
log "File /dev/shm/nd_files_c/cam_rec_service_started present. Deleting it"
rm -f /dev/shm/nd_files_c/cam_rec_service_started
status=$?
else
log "File not present. Skipping it"
check_status $(basename $(pwd)) 0
log "=================End of removing cam_rec service ram file==================="
exit 0
fi

if [[ $status == 0 ]]; then
        log "cam_rec service ram file deleted successfully"
else
        log "cam_rec service ram file not deleted, please check..!"
fi
check_status $(basename $(pwd)) $status

log "=================End of removing cam_rec service ram file==================="
