#!/usr/bin/env bash

set -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Script to fix the cron service

log "==============Start of cron_clean_ib================="

log "Copying the clean_ib.sh to /home/ubuntu/bin"
status=$(execute_command_sync cp -p clean_ib.sh /home/ubuntu/bin)$?
check_status "copy clean_ib.sh status $status"
sync
if [[ ! $(sudo crontab -l | grep clean_ib.sh) ]]
then
    sudo crontab -l | sed '$ a */5 *  * * * /home/ubuntu/bin/clean_ib.sh' | sudo crontab -
    status="${PIPESTATUS[0]} ${PIPESTATUS[1]} ${PIPESTATUS[2]}"
    sync
    [ "$status" == "0 0 0" ]
    log "Commands are successful"
    [ "`sudo crontab -l | grep clean_ib.sh | cut -c 1-4`" == "*/5 " ]
    log "Adding to cronjob is successful"
else
    [ "`sudo crontab -l | grep clean_ib.sh | cut -c 1-4`" == "*/5 " ]
    log "Clean_ib script is present in the cronjob"
fi

log "==============End of cron_clean_ib================="

exit 0
