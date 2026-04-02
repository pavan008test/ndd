#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling execute_script function from lib
log "########Starting of Revert of Inertial_obs_temp renaming to observations######"
log "Checking the current OTA version as revert should not apply on or after 0.4.7"

current_ota=$(cat /home/ubuntu/.nddevice/nddevice.ini | grep -a1 version | grep nddevice | awk -F "=" '{print $2}' | tr -d ' ' | cut -d "." -f 1-3)
ota_list=(0.4.7 0.5.3 0.5.4)
for version in ${ota_list[@]}
do
    if [[ $current_ota == $version ]]
    then
        log "This revert task is not required for this current OTA $current_ota"
        exit 0
    fi
done

if [[ ! -d /home/ubuntu/.nddevice/inertial_obs_temp ]]
then
    if [[ -d /home/ubuntu/.nddevice/observations ]]
    then
        log "Moving the observations to inertial_obs_temp"
        status=$(execute_command_sync sudo mv /home/ubuntu/.nddevice/observations /home/ubuntu/.nddevice/inertial_obs_temp)$?
        if [[ $status != 0 ]]; then log "observatoins to Inertial_obs_temp rename failed" ; check_status $(basename $(pwd)) 1 ; fi ;
        log "observations to inertial_obs_temp is renamed successfully"
    else
        log "Creating the inertial_obs_temp folder"
        status=$(execute_command_sync mkdir -p /home/ubuntu/.nddevice/inertial_obs_temp)$?
        if [[ $status != 0 ]]; then log "Creating the inertial_obs_temp folder failed" ; check_status $(basename $(pwd)) 1 ; fi ;
        status=$(execute_command_sync chown ubuntu:ubuntu /home/ubuntu/.nddevice/inertial_obs_temp)$?
        if [[ $status != 0 ]]; then log "Changing ownership failed" ; check_status $(basename $(pwd)) 1 ; fi ;
        status=$(execute_command_sync chmod 775 /home/ubuntu/.nddevice/inertial_obs_temp)$?
        if [[ $status != 0 ]]; then log "Changing permission failed" ; check_status $(basename $(pwd)) 1 ; fi ;
    fi
else
    log "inertial_obs_temp folder is present. Doing nothing"
fi
log "########End of Revert Inertial_obs_temp renaming to observations######"
