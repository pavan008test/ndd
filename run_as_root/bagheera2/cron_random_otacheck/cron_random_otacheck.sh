#!/usr/bin/env bash

set -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Script to fix the cron service

log "==============Start of Cron_Random_Otacheck================="

log "Modifying the crontab for ubuntu"
sync
crontab -u ubuntu -l | sed "\~wrapper_otacheck$~{s~^\*\/10~\*\/1~;}" | crontab -u ubuntu -
status="${PIPESTATUS[0]} ${PIPESTATUS[1]} ${PIPESTATUS[2]}"
sync
[ "$status" == "0 0 0" ]
log "Commands are successful"
[ "`crontab -u ubuntu -l | grep wrapper_otacheck | cut -c 1-4`" == "*/1 " ]
log "Crontab changes are successful"

log "==============End of Cron_Random_Otacheck================="

exit 0


