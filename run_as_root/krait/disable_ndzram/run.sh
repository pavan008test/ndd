#!/usr/bin/env bash 

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "Disabling the ndzram service to disable swap memory"
systemctl disable ndzram.service
if [[ "disabled" == $(systemctl is-enabled ndzram) ]]; then 
	log "ndzram is successfully disabled"
else
	log "Something went wrong ndzram not disabled. Here is the service status"
	systemctl status ndzram.service >> /home/ubuntu/.nddevice/log/bhcopy.log
fi

