#!/usr/bin/env bash

set -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh


log "==============Start of core_dump script================="

log "Copying the core_dump.sh to /home/ubuntu/bin"
CHECKSUM=$(md5sum core_dump.sh | awk -F " " '{print $1}')
status1=$(copy_file -f core_dump.sh -d /home/ubuntu/bin -m ${CHECKSUM} -p 775 -o ubuntu:ubuntu)$?

log "Installing cron command to root cron to execute core_dump.sh"
if [[ ! $(sudo crontab -l | grep core_dump.sh) ]]
then
    sudo crontab -l | sed '$ a @reboot /home/ubuntu/bin/core_dump.sh' | sudo crontab -
    status="${PIPESTATUS[0]} ${PIPESTATUS[1]} ${PIPESTATUS[2]}"
    sync
    [ "$status" == "0 0 0" ]
    log "Commands are successful"
    [[ $(sudo crontab -l | grep core_dump.sh) ]]
    log "Adding to cronjob is successful"
else
    log "core_dump.sh is already present in the cronjob. Skipping cron install"
fi

check_status "core_dump_copy" $status1
log "==============End of core_dump.sh================="

exit 0
