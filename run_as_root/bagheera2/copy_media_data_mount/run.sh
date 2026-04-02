#!/usr/bin/env bash

source  /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling copy funtion from lib

log "========= Start of copying media_data.mount to /lib/systemd/system/ =========="
   
    log "Copying media_data.mount to /lib/systemd/system/"
    file_md5sum=$(md5sum media-data.mount |awk '{print $1}')
    system_md5sum=$(md5sum /lib/systemd/system/media-data.mount |awk '{print $1}')
    if [[ $file_md5sum == $system_md5sum ]]; then
	    log "Already updated media_data.mount  file is present at /lib/systemd/system/ so not copying..."
	    status1=0
    else
	   log "copying latest media-data.mount at /lib/systemd/system/...."
	   copy_file -f media-data.mount  -d /lib/systemd/system/ -m $file_md5sum -p 644  -o root:root
	   status1=$?
    fi
 
if [[ $status1 == 0 ]];then
	    log "media-data.mount copied and started successfully"
	    check_status $(basename $(pwd)) 0
    else
	    log "media-data.mount not copied please check the logs....!!!"
	    check_status $(basename $(pwd)) 1
    fi
log "================ End of copying media-data.mount================="
