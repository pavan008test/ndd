#!/usr/bin/env bash

set -e 

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "Checking for the .ble link file in .nddevice"
if [[ ! -L /home/ubuntu/.nddevice/.ble ]]; then
        ##Removing the login.db
        log "Checking for the .ble folder in .nddevice"
        if [[ -d /home/ubuntu/.nddevice/.ble ]] ; then
            log "Removing the files from .ble folder if present"
            if [[ -f "/home/ubuntu/.nddevice/.ble/login.db" ]] ; then
                log "files are present removing it"
                rm -f /home/ubuntu/.nddevice/.ble/*
            else
                log "login.db file is not present"
            fi
        else
            log "No folder name .ble , Skipping the login.db removal"
        fi

        ## Moving the .ble folder and creating the link
        log "Now moving the .ble folder to /data/nd_files/"
        if [[ ! -d /data/nd_files/.ble ]]; then
            log ".ble folder is not present. moving from .nddevice"
            mv /home/ubuntu/.nddevice/.ble /data/nd_files/
            sync
            log "creating a link for .ble in .nddevice"
            ln -s /data/nd_files/.ble /home/ubuntu/.nddevice/.ble
            sync
        else
            log "There is already a .ble folder in /data/nd_files. Skipping folder creation of .ble ..."
        fi
else      
        log "There is a .ble link created already. So skipping the actions...."
fi
