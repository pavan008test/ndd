#!/usr/bin/env bash


source  /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling copy funtion from lib

log "========= Start of copying latest nvargus-daemon.service at /etc/systemd/system/=========="
   
    log "Copying latest nvargus-daemon.service file to /etc/systemd/system/"
    file_md5sum=$(md5sum nvargus-daemon.service |awk '{print $1}')
    system_md5sum=$(md5sum /etc/systemd/system/nvargus-daemon.service |awk '{print $1}')
    if [[ $file_md5sum == $system_md5sum ]]; then
	    log "Already latest nvargus-daemon.service file is present at /etc/systemd/system/ so not copying..."
	    status1=0
    else
	    log "copying latest nvargus-daemon.service at /etc/systemd/system/...."
	    copy_file -f nvargus-daemon.service -d /etc/systemd/system/ -m $file_md5sum -p 644  -o root:root
	    log "Reloading the daemon-reload"
	    systemctl daemon-reload
	    status1=$?
    fi
    if [[ $status1 == 0 ]];then
	    log "nvargus-daemon.service successfully copied"
	    check_status $(basename $(pwd)) 0
    else
	    log "nvargus-daemon.service not copied please check the logs....!!!"
	    check_status $(basename $(pwd)) 1
    fi
log "================ End of copying latest nvargus-daemon.service================="
