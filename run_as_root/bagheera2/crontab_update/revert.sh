#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Script to revert the root and user crontab
log "============== Start revert of crontab_update ================="

latest_version=$(readlink /home/ubuntu/.nddevice/latest | cut -d '.' -f1-3)
major_version=$(echo $latest_version | cut -d '.' -f2)
minor_version=$(echo $latest_version | cut -d '.' -f3)
log "Current latest_version is $latest_version"

if [ $major_version -eq 5 ] && [ $minor_version -lt 21 ] ; then

log "Updating the root crontab"
sudo crontab ${PWD}/revert/root_conf.cron
status1=$?

log "Updating the ubuntu crontab"
crontab -u ubuntu ${PWD}/revert/conf.cron
status2=$?

log "Changing the ownership of otacheck logs"
sudo chown -fR ubuntu:ubuntu /home/ubuntu/.nddevice/log/otacheck/

sync

if [[ $status1 == 0 && $status2 == 0 ]]; then
check_status $(basename $(pwd)) 0
else
check_status $(basename $(pwd)) 1
fi

else
log "Revert task not applicable"
fi

log "============== End revert of crontab_update ================="

