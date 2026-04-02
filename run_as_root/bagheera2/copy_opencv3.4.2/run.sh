#!/usr/bin/env bash

set -e

source  /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling copy funtion from lib

log "========= Start of copying opencv3.4.2 =========="

if [[ -d /usr/local/opencv3.4.2/ ]] ; then
    log "opencv3.4.2 is present at /usr/local/. Replacing..."
    rm -rf /usr/local/opencv3.4.2/
fi
log "Untaring opencv tar directly into /usr/local/"
execute_command "sudo tar -xzf opencv3.4.2_cutdown_25082020.tar.gz -C /usr/local/"
if [[ $? == 0 ]] ; then
    num_of_files=$(ls -R /usr/local/opencv3.4.2/ | wc | awk -F " " '{print $1}' | tr -d '\t'); 
    if [[ ${num_of_files} == 847 ]] ; then
        status=0 ;
        log "untar is successful"
    else 
        status=1; 
    fi; 
else 
    log "tar command failed to untar" ; 
    status=1 ; 
fi 
check_status $(basename $(pwd)) $status
log "========= End of copying opencv3.4.2 ========="

