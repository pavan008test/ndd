#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "========= Start of copying media_data.mount to /lib/systemd/system/ =========="

log "Copying media_data.mount to /lib/systemd/system/"
file_md5sum=$(md5sum media-data.mount |awk '{print $1}')
system_md5sum=$(md5sum /lib/systemd/system/media-data.mount |awk '{print $1}')
    
if [[ $file_md5sum == $system_md5sum ]]; then
	log "Already updated media_data.mount  file is present at /lib/systemd/system/ so not copying..."
	status1=0
else
	log "Moving the latest media-data.mount to /lib/systemd/system/"
	mv media-data.mount /lib/systemd/system/media-data.mount
    status1=$?
fi

if [[ $status1 == 0 ]]; then
    log "Succeefully copied the media-data.mount to /lib/systemd/system/"
    check_status $(basename $(pwd)) 0
else
    check_status $(basename $(pwd)) 1
    log "Something went wrong please check the logs"
fi
log "==========End of copying the media-data.mount to /lib/systemd/system/==========="
