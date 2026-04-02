#!/usr/bin/env bash 

set -e 

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "Checking for the clik_wifi service, is exists..."
if [[ $(systemctl list-units --all -t service --full --no-legend "clik_wifi.service" | cut -f1 -d' ') == "clik_wifi.service" ]] ; then
    log "clik_wifi service is exist, stop and disbale in process"
    systemctl stop clik_wifi.service
    sleep 2
    systemctl disable clik_wifi.service
else
    log " clik_wifi service does not exists, Skipping the disbale_clik_wifi task..."
fi

