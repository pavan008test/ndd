#!/usr/bin/env bash

set -e

source  /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling copy funtion from lib

log "========= Start of copying blackvideo for all 2 camera =========="
    log "Copying blackvideo_editing.mp4 to /home/ubuntu/.nddevice/"
    inward_hd_md5sum=$(md5sum blackvideo_editing.mp4 |awk '{print $1}')
    copy_file -f blackvideo_editing.mp4 -d /home/ubuntu/.nddevice/ -m $inward_hd_md5sum -p 775
    status=$?
    check_status $(basename $(pwd)) $status

    log "Copying blackvideo_editing_LD.mp4 to /home/ubuntu/.nddevice/"
    inward_ld_md5sum=$(md5sum blackvideo_editing_LD.mp4 |awk '{print $1}')
    copy_file -f blackvideo_editing_LD.mp4 -d /home/ubuntu/.nddevice/ -m $inward_ld_md5sum -p 775
    status=$?
    check_status $(basename $(pwd)) $status

    log "Copying blackvideo_editing_FHD.mp4 to /home/ubuntu/.nddevice/"
    outward_ld_md5sum=$(md5sum blackvideo_editing_FHD.mp4 |awk '{print $1}')
    copy_file -f blackvideo_editing_FHD.mp4 -d /home/ubuntu/.nddevice/ -m $outward_ld_md5sum -p 775
    status=$?
    check_status $(basename $(pwd)) $status

    log "Copying blackvideo_editing_FLD.mp4 to /home/ubuntu/.nddevice/"
    outward_hd_md5sum=$(md5sum blackvideo_editing_FLD.mp4 |awk '{print $1}')
    copy_file -f blackvideo_editing_FLD.mp4 -d /home/ubuntu/.nddevice/ -m $outward_hd_md5sum -p 775
    status=$?
    check_status $(basename $(pwd)) $status
log "========= End of copying blackvideo done for all 2 camera ========="
