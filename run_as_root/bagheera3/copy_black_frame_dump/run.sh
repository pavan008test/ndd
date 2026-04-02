#!/usr/bin/env bash

source  /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling copy funtion from lib

log "========= Start of copying black frame dump  =========="

    log "Copying black_frame_inward_LS.avc and black_frame_outward_LS.avc to /home/ubuntu/.nddevice/"
    black_frame_inward_LS_md5sum=$(md5sum black_frame_inward_LS.avc |awk '{print $1}')
    black_frame_outward_LS_md5sum=$(md5sum black_frame_outward_LS.avc |awk '{print $1}')

    copy_file -f black_frame_inward_LS.avc -d /home/ubuntu/.nddevice/ -m $black_frame_inward_LS_md5sum -p 775  -o ubuntu:ubuntu
    status1=$?
    copy_file -f black_frame_outward_LS.avc -d /home/ubuntu/.nddevice/ -m $black_frame_outward_LS_md5sum -p 775  -o ubuntu:ubuntu
    status2=$?
  

    if [[ $status1 == 0 && $status2 == 0 ]];then
    check_status $(basename $(pwd)) 0
    else
    check_status $(basename $(pwd)) 1
    fi

log "========= End of copying copying black frame dump ========="
