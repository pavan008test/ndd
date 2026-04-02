#!/usr/bin/env bash


source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling execute_script function from lib

log "=============== Start of move audiolog ============="
set -e
log "moving the /home/ubuntu/.nddevice/log/audioPlayback_service.log to /home/ubuntu/.nddevice/log/audio"
mkdir -p /home/ubuntu/.nddevice/log/audio && chown ubuntu:ubuntu /home/ubuntu/.nddevice/log/audio
set +e
if [[ -s /home/ubuntu/.nddevice/log/audioPlayback_service.log ]]; then 
	status=$(execute_command mv /home/ubuntu/.nddevice/log/audioPlayback_service.log /home/ubuntu/.nddevice/log/audio)$?
	check_status $(basename $(pwd)) $status
else
	log "There is no audioPlayback_service.log file"
fi
log "=============== Done with move audiolog ============"
