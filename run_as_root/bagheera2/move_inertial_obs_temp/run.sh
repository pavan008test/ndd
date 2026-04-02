#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

set -e 
#Calling execute_script function from lib
log "########Starting of Inertial_obs_temp renaming to observations######"

if [[ -d /home/ubuntu/.nddevice/inertial_obs_temp ]]
then
    log "Deleting the observations directory"
    status=$(execute_command_sync rm -rf /home/ubuntu/.nddevice/observations)$?
    if [[ $status != 0 ]]; then log "observations folder delete failed" ; check_status $(basename $(pwd)) 1 ; fi ;
    log "Renaming the inertial_obs_temp to observations"
    status=$(execute_command_sync sudo mv /home/ubuntu/.nddevice/inertial_obs_temp /home/ubuntu/.nddevice/observations)$?
    if [[ $status != 0 ]]; then log "Inertial_obs_temp rename failed" ; check_status $(basename $(pwd)) 1 ; fi ;
    log "inertial_obs_temp is renamed successfully"
else
    if [[ ! -d /home/ubuntu/.nddevice/observations ]]
    then
        log "inertial_obs_temp folder is not present. Just creating the observations folder"
        status=$(execute_command_sync mkdir -p /home/ubuntu/.nddevice/observations)$?
        if [[ $status != 0 ]]; then log "Creating the observations folder failed" ; check_status $(basename $(pwd)) 1 ; fi ;
        status=$(execute_command_sync chown ubuntu:ubuntu /home/ubuntu/.nddevice/observations)$?
        if [[ $status != 0 ]]; then log "Changing ownership failed" ; check_status $(basename $(pwd)) 1 ; fi ;
        status=$(execute_command_sync chmod 775 /home/ubuntu/.nddevice/observations)$?
        if [[ $status != 0 ]]; then log "Changing permission failed" ; check_status $(basename $(pwd)) 1 ; fi ;
        log "observations folder created successfully"
    else
        log "observations folder is already present. Doing nothing"
    fi
fi
check_status $(basename $(pwd)) 0
log "########End of Inertial_obs_temp renaming to observations######"
