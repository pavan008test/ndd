#!/usr/bin/env bash


source  /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling copy funtion from lib

CURRENT_OS_VERSION=$(grep "nd_os" /etc/nd_os_ver.ini | cut -d'=' -f2)

if [[ $CURRENT_OS_VERSION != 13.0.19 ]] && [[ $CURRENT_OS_VERSION != 13.0.1A ]] ; then
    echo "This script is intended for OS version 13.0.19 or 13.0.1A,So Exiting..."    
    exit 0
fi

log "========= Start of coping latest amixer.sh at /etc/init.d/ =========="
   
    log "Copying amixer.sh  file to /etc/init.d/"
    file_md5sum=$(md5sum amixer.sh |awk '{print $1}')
    system_md5sum=$(md5sum /etc/init.d/amixer.sh |awk '{print $1}')
    if [[ $file_md5sum == $system_md5sum ]]; then
	    log "Already latest amixer.sh file is present at /etc/init.d/ so not copying..."
	    status1=0
    else
	    log "copying latest amixer.sh at /etc/init.d/...."
	    copy_file -f amixer.sh -d /etc/init.d/ -m $file_md5sum -p 755  -o root:root
	    status1=$?
    fi
if [[ $status1 == 0 ]];then
	    log "latest amixer.sh successfully copied"
	    check_status $(basename $(pwd)) 0
    else
	    log "amixer.sh not copied please check the logs....!!!"
	    check_status $(basename $(pwd)) 1
    fi
log "================ End of copying latest amixer.sh================="
