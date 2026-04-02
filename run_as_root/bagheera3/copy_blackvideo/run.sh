#!/usr/bin/env bash

set -e

source  /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling copy funtion from lib

log "========= Start of copying blackvideo_editing_dms_LD.mp4 =========="
   
    log "Copying blackvideo_editing_dms_LD.mp4 to /home/ubuntu/.nddevice/"
    dmsldfile_md5sum=$(md5sum blackvideo_editing_dms_LD.mp4 |awk '{print $1}')

    copy_file -f blackvideo_editing_dms_LD.mp4 -d /home/ubuntu/.nddevice/ -m $dmsldfile_md5sum -p 775  -o ubuntu:ubuntu
    status1=$?

    if [[ $status1 == 0 ]];then
    check_status $(basename $(pwd)) 0
    else
    check_status $(basename $(pwd)) 1
    fi

log "========= End of copying blackvideo_editing_dms_LD.mp4 ========="
