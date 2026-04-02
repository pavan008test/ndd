#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling execute_script function from lib

#Script to update the root and user crontab
log "============== Start of crontab_update ================="

log "Updating the root crontab"
sudo crontab ${PWD}/run/root_conf.cron
status1=$?

log "Updating the ubuntu crontab"
crontab -u ubuntu ${PWD}/run/conf.cron
status2=$?

sync 

if [[ $status1 == 0 && $status2 == 0 ]]; then
check_status $(basename $(pwd)) 0
else
check_status $(basename $(pwd)) 1
fi

log "============== End of crontab_update ================="


