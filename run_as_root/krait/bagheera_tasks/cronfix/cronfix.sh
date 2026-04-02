#!/usr/bin/env bash


source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Script to fix the cron service

log "==============Start of Cronfix================="

log "Taking back up of original file to default backup path"
backup /lib/systemd/system/cron.service /home/ubuntu/.nddevice/backup/

log "Changing cron.service permissions to rw-r-r--"
chmod 644 /home/ubuntu/.nddevice/ota_temp/run_as_root/cronfix/cron.service

log "Changing cron.service ownership to root "
sudo chown root:root /home/ubuntu/.nddevice/ota_temp/run_as_root/cronfix/cron.service

log "Stopping the cron service"
sudo systemctl stop cron.service

log "Copying the new cron.service file to /lib/systemd/system/"
sync 
sudo mv -f /home/ubuntu/.nddevice/ota_temp/run_as_root/cronfix/cron.service /lib/systemd/system/
sync /lib/systemd/system/cron.service

checksum_status=$(check_md5sum "/lib/systemd/system/cron.service" "3f22d6d7461b8b335de156b434c981a6")$?

if [[ $checksum_status == 0 ]]; then
	log "File copied Successfully"
else
	log "File is not copied properly. Please try again later. Exiting from this script"
	exit 1
fi
			
log "Starting the cron service"
sudo systemctl start cron.service

log "==============End of Cronfix================="
			
exit 0

