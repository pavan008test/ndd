#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

set -e
#Script to fix the cron service
log "==============Start of delete_cron_nd_input================="

#Checking in user cron for ND_INPUT cleanup 

if [[ $(crontab -u ubuntu -l | grep -q "^\@reboot.*rm -rf .*ND_INPUT")$? == 0 ]]; then
	log "Commenting ND_INPUT line in user crontab"
	crontab -u ubuntu -l | sed 's/^@reboot rm .*ND_INPUT/#&/g' | crontab -u ubuntu
	status="${PIPESTATUS[0]} ${PIPESTATUS[1]} ${PIPESTATUS[2]}"
	sync
	[ "$status" == "0 0 0" ]
	log "cron commands are successful"
else
	log "No ND_INPUT clean up or it might have already commented"
fi

#checking in sudo cron for ND_INPUT cleanup

if [[ $(sudo crontab -l | grep -q "^\@reboot.*rm -rf .*ND_INPUT")$? == 0 ]]; then
  log "Commenting ND_INPUT line in root crontab"
  sudo crontab -l | sed 's/^@reboot rm .*ND_INPUT/#&/g' | sudo crontab
  status="${PIPESTATUS[0]} ${PIPESTATUS[1]} ${PIPESTATUS[2]}"
  sync
  [ "$status" == "0 0 0" ]
  log "cron commands are successful"
else
  log "No ND_INPUT clean up or it might have already commented"
fi

log "==============End of delete_cron_nd_input================="

