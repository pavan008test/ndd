#!/usr/bin/env bash

set -e

source  /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling copy funtion from lib

log "========= Start of copying fstab at /etc/ =========="
   
    log "Copying fstab  file to /etc/"
    file_md5sum=$(md5sum fstab |awk '{print $1}')
    system_md5sum=$(md5sum /etc/fstab |awk '{print $1}')
    if [[ $file_md5sum == $system_md5sum ]]; then
	    log "Already updated fstab file is present at /etc/ so not copying..."
	    status1=0
    else
	    log "copying latest fstab at /etc/...."
	    copy_file -f fstab -d /etc/ -m $file_md5sum -p 644  -o root:root
	    status1=$?
    fi
    if [[ $status1 == 0 ]];then
	    log "fstab successfully copied"
	    check_status $(basename $(pwd)) 0
    else
	    log "fstab not copied please check the logs....!!!"
	    check_status $(basename $(pwd)) 1
    fi
log "================ End of copying fstab================="
