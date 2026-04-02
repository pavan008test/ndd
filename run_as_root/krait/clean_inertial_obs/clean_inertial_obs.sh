#!/usr/bin/env bash 

set -e 

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "Checking the observations soft link is present under /data/nd_files/observations/..."
if [[ -L "/data/nd_files/observations/observations" ]] ; then
    log "soft link  /data/nd_files/observations/observations is present, unliking it"
    rm  /data/nd_files/observations/observations
else
    log "soft link file is not present under /data/nd_files/observations/observations, skipping the unlinking process"
fi

log "Checking for inertial_obs_temp files is present under /data/nd_files/observations/..."
if [[ -d "/data/nd_files/observations/inertial_obs_temp" ]] ; then
    log "inertial_obs_temp file is present under /data/nd_files/observations/, removing it"
    rm -r  /data/nd_files/observations/inertial_obs_temp
else
    log "inertial_obs_temp file is not present under /data/nd_files/observations/, skipping the deletion process"
fi
