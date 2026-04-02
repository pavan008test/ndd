#!/usr/bin/env bash 

set -e 

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "Checking for the chrony_service, is exists..."
if [[ $(systemctl list-units --all -t service --full --no-legend "chronyd.service" | cut -f1 -d' ') == "chronyd.service" ]] ; then
    log "disable_chrony_service is exist, stop and disbale in process"
    systemctl stop chronyd.service
    sleep 2
    systemctl disable chronyd.service
else
    log " disable_chrony_service does not exists, Skipping the disable_chrony_service task..."
fi

