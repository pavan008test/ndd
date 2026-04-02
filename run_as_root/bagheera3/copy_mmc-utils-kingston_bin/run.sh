#!/bin/env bash


source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling copy funtion from lib

md5sum_kingston=$(md5sum mmc-utils-kingston | awk -F " " '{print $1}')

log "=========== Start of copy mmc-utils-kingston bin to /home/ubuntu/.nddevice/CLE_tool/SDcard/Kingston/ ==========="

if [[ ! -d /home/ubuntu/.nddevice/CLE_tool/SDcard/Kingston/ ]] ; then
mkdir -p /home/ubuntu/.nddevice/CLE_tool/SDcard/Kingston/
fi

if [[ ! -f /home/ubuntu/.nddevice/CLE_tool/SDcard/Kingston/mmc-utils-kingston ]] ; then
log "copying the mmc-utils-kingston bin to kingston folder"
status1=$(copy_file -f mmc-utils-kingston -d /home/ubuntu/.nddevice/CLE_tool/SDcard/Kingston/ -b /home/ubuntu/.nddevice/backup -m ${md5sum_kingston} -p 755 -o ubuntu:ubuntu)$?
else
log "mmc-utils-kingston bin is already exists in kingston folder"
status1=0
fi

if [[ $status1 == 0 ]]; then
        log "mmc-utils-kingston bin copied successfully"
else
        log "mmc-utils-kingston file not copied successfully, please check..!"
fi
log "======= End of coping mmc-utils-kingston bin to /home/ubuntu/.nddevice/CLE_tool/SDcard/Kingston/ ============"
