#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling execute_script function from lib

log "====== Start of revert for delete_cron_NDINPUT ======"

if [[ $(crontab -u ubuntu -l | grep -q "^#\@reboot.*rm -rf .*ND_INPUT")$? == 0 ]]; then
	log "Uncommenting ND_INPUT cleanup line in user crontab"
	crontab -u ubuntu -l | sed 's/^@reboot rm .*ND_INPUT/#&/g' | crontab -u ubuntu
	status="${PIPESTATUS[0]} ${PIPESTATUS[1]} ${PIPESTATUS[2]}"
	sync
	[ "$status" == "0 0 0" ]
	log "cron commands are successful"
else
	log "No ND_INPUT clean up or it might not have commented"
fi

if [[ $(sudo crontab -l | grep -q "^#\@reboot.*rm -rf .*ND_INPUT")$? == 0 ]]; then
  log "uncommenting ND_INPUT cleanup line in root crontab"
  sudo crontab -l | sed 's/^#\(@reboot rm .*ND_INPUT\)/\1/' | sudo crontab
  status="${PIPESTATUS[0]} ${PIPESTATUS[1]} ${PIPESTATUS[2]}"
  sync
  [ "$status" == "0 0 0" ]
  log "cron commands are successful"
else
  log "No ND_INPUT clean up or it might not have commented"
fi

log "====== End of revert for delete_cron_NDINPUT ======"

