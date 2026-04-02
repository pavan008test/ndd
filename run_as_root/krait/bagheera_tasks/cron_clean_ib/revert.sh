#!/usr/bin/env bash

set -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Script to fix the cron service

log "==============Start of Cron_Random_Otacheck revert================="

log "reverting the crontab for ubuntu"
sync
sudo crontab -l | sed '/clean_ib.sh/d' | sudo crontab - 
status="${PIPESTATUS[0]} ${PIPESTATUS[1]} ${PIPESTATUS[2]}"
sync
[ "$status" == "0 0 0" ]
log "Commands are successful"
[ ! $(sudo crontab -l | grep clean_ib.sh) ]
log "Crontab revert is successful"

log "==============End of Cron_Random_Otacheck================="

exit 0



