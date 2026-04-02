#!/usr/bin/env bash

set -e

source  /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling copy funtion from lib

log "========= Start of copying blackvideo =========="
   
    log "Copying blackvideo_editing.mp4, blackvideo_editing_side.mp4, LD blackvideo file, blackvideo_editing_FLD.mp4 and blackvideo_editing_FHD.mp4 to /home/ubuntu/.nddevice/"
    file_md5sum=$(md5sum blackvideo_editing.mp4 |awk '{print $1}')
    ldfile_md5sum=$(md5sum blackvideo_editing_LD.mp4 |awk '{print $1}')
    FHDfile_md5sum=$(md5sum blackvideo_editing_FHD.mp4 |awk '{print $1}')
    FLDfile_md5sum=$(md5sum blackvideo_editing_FLD.mp4 |awk '{print $1}')
    Sidefile_md5sum=$(md5sum blackvideo_editing_side.mp4 |awk '{print $1}')

    copy_file -f blackvideo_editing.mp4 -d /home/ubuntu/.nddevice/ -m $file_md5sum -p 775  -o ubuntu:ubuntu
    status1=$?
    copy_file -f blackvideo_editing_LD.mp4 -d /home/ubuntu/.nddevice/ -m $ldfile_md5sum -p 775  -o ubuntu:ubuntu
    status2=$?
    copy_file -f blackvideo_editing_FHD.mp4 -d /home/ubuntu/.nddevice/ -m $FHDfile_md5sum -p 775  -o ubuntu:ubuntu
    status3=$?
    copy_file -f blackvideo_editing_FLD.mp4 -d /home/ubuntu/.nddevice/ -m $FLDfile_md5sum -p 775  -o ubuntu:ubuntu
    status4=$?
    copy_file -f blackvideo_editing_side.mp4 -d /home/ubuntu/.nddevice/ -m $Sidefile_md5sum -p 775  -o ubuntu:ubuntu
    status5=$?


    if [[ $status1 == 0 && $status2 == 0 && $status3 == 0 && $status4 == 0 && $status5 == 0 ]];then
    check_status $(basename $(pwd)) 0
    else
    check_status $(basename $(pwd)) 1
    fi

log "========= End of copying blackvideo ========="
