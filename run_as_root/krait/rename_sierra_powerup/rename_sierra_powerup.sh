#!/bin/bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "Checking the sierra_powerup.sh in /usr/bin"

if [[ -f /usr/bin/sierra_powerup.sh ]]; then 
    log "Script is present, Running couple of AT commands"
    log "Executing lte_gps_sample_app \'at!entercnd=\"A710\"\'"
    lte_gps_sample_app 'at!entercnd="A710"'
    log "Execute lte_gps_sample_app \'at!custom=\"SIMLPM\",2\'"
    lte_gps_sample_app 'at!custom="SIMLPM",2'
    log "Renaming the sierra_powerup.sh to old_sierra_powerup.sh"
    mv /usr/bin/sierra_powerup.sh /usr/bin/old_sierra_powerup.sh
else
log "File is not present. It would have already renamed."
fi

