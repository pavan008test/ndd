#!/usr/bin/env bash

set -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh


log "==============Start of standy_setup_cron================="

log "Copying the standby_setup.sh to /home/ubuntu/bin"
CHECKSUM=$(md5sum standby_setup.sh | awk -F " " '{print $1}')
status=$(copy_file -f standby_setup.sh -d /home/ubuntu/bin -m ${CHECKSUM} -p 775 -o ubuntu:ubuntu)$?
check_status "copy_standby_setup" $status

log "Installing cron command to root cron to execute standby_setup"
if [[ ! $(sudo crontab -l | grep standby_setup.sh) ]]
then
    sudo crontab -l | sed '$ a @reboot /home/ubuntu/bin/standby_setup.sh' | sudo crontab -
    status="${PIPESTATUS[0]} ${PIPESTATUS[1]} ${PIPESTATUS[2]}"
    sync
    [ "$status" == "0 0 0" ]
    log "Commands are successful"
    [[ $(sudo crontab -l | grep standby_setup.sh) ]]
    log "Adding to cronjob is successful"
else
    log "standby_setup.sh is already present in the cronjob. Skipping cron install"
fi

log "executing the standby_setup.sh script for first time"
status=$(execute_command_sync bash /home/ubuntu/bin/standby_setup.sh)
check_status "install_standby_setup" $status

log "==============End of standby_setup_cron================="

exit 0
