#!/usr/bin/env bash 

set -e 

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "Checking for the dcam_wifi service, is exists..."
if [[ $(systemctl list-units --all -t service --full --no-legend "dcam_wifi.service" | cut -f1 -d' ') == "dcam_wifi.service" ]] ; then
    log "dcam_wifi service is exist, stop and disbale in process"
    systemctl stop dcam_wifi.service
    sleep 2
    systemctl disable dcam_wifi.service
else
    log " dcam_wifi service does not exists, Skipping the disbale_dcam_wifi task..."
fi

