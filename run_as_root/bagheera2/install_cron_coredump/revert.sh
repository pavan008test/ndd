#!/usr/bin/env bash

set -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh


log "==============Start of revert install_cron_coredump ================="

latest_version=$(readlink /home/ubuntu/.nddevice/latest | cut -d '.' -f1-3)
major_version=$(echo $latest_version | cut -d '.' -f2)
minor_version=$(echo $latest_version | cut -d '.' -f3)
log "Current latest_version is $latest_version"

if [ $major_version -eq 5 ] && [ $minor_version -lt 21 ] ; then
log "Reverting the crontab changes to original with OS"
status=$(execute_command_sync crontab /home/ubuntu/.nddevice/root_conf.cron)$?
check_status "install_cron_coredump" $status
else
log "Revert is not applicable"
fi

log "==============End of revert install_cron_coredump ================="

exit 0
