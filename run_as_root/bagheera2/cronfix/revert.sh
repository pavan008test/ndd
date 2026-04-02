#!/usr/bin/env bash


source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Script to fix the cron service

log "==============Start of Cronfix revert================="

log "reverting the cron.service to original file"
sync
status=$(rollback /home/ubuntu/.nddevice/backup/cron.service /lib/systemd/system/)$?
sync
if [ $status == 0 ]
then
log "replaced the old file in the cron.service"
else
log "reverting the cron.service file failed. Removing the cron.service file from /lib/systemd/system/"
sudo rm /lib/systemd/system/cron.service
fi
log "starting the cron service"
sudo systemctl start cron.service

log "==============END of Cronfix revert================="

exit 0
