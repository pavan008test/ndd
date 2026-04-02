#!/usr/bin/env bash


source  /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh


log "========= Start of moving iriscli folder into nd_sdcard folder =========="

if [[ ! -d /media/data/nd_sdcard/iriscli/ ]]; then
log "Iriscli folder does not exists in the nd_sdcard folder,so moving the iriscli folder to nd_sdcard"
status1=$(execute_command mv /home/iriscli/ /media/data/nd_sdcard/)$?
else
log "Folder already present in /media/data/nd_sdcard/iriscli/. So skipping move operation"
status1=0
fi

if [[ $(readlink /home/iriscli) == "/media/data/nd_sdcard/iriscli" ]]; then
log "Linking is already present to /media. So skipping"
status2=0
else
log "Linking the iriscli folder to the /media/data/nd_sdcard/"
status2=$(execute_command ln -sf /media/data/nd_sdcard/iriscli /home/iriscli)$?
fi


if [[ $status1 == 0 && $status2 == 0 ]] ; then
	log "Iriscli folder moved properly to /media/data/nd_sdcard/"
	check_status $(basename $(pwd)) 0
else
	log "!!!!!!! something wrong in moving or linking iriscli folder."
	if [[ -d /media/data/nd_sdcard/iriscli/ && ! -f /home/iriscli ]]; then
		log "Folder moved but linking failed. Hence moving back to /home/"
		mv /media/data/nd_sdcard/iriscli /home/
	else
		log "There can be move error or linking error. Restoring the folder structure"
		rm -rf media/data/nd_sdcard/iriscli ; rm -rf /home/iriscli 
		mkdir -p /home/iriscli/ND_INPUT
	fi

	check_status $(basename $(pwd)) 1
fi

log "============Moving of iriscli folder into nd_sdcard is happened successfully=============="
