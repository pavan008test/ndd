#!/usr/bin/env bash

set -e

source  /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling copy funtion from lib

log "========= Start of isp frimware upgrade =========="
   
    log "Copying ov491_isp_upgrade.sh to /image_upgrade/isp/"
    file_md5sum=$(md5sum ov491_isp_upgrade.sh |awk '{print $1}')
    copy_file -f ov491_isp_upgrade.sh -d /image_upgrade/isp/ -m $file_md5sum -p 775  -o root:root
    status1=$?
    check_status $(basename $(pwd)) $status1

log "========= End of isp frimware upgrade ========="
