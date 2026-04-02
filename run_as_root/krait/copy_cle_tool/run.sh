#!/usr/bin/env bash

set -e

source  /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling copy funtion from lib

log "========= Start of Extract of CLE Tool in /home/ubuntu/.nddevice/ =========="


log "Checking for any existed CLE_tool"
if [[ -d /home/ubuntu/.nddevice/CLE_tool_krait ]]; then
	log "CLE_tool alraedy present, so deleting it..."
	rm -rf /home/ubuntu/.nddevice/CLE_tool_krait
else
	log "No existing Cle_tool so unzipping it..."
fi
tar -xzf  CLE_tool_krait.tar.gz -C /home/ubuntu/.nddevice/
status=$?
if [[ $status == 0 ]]; then
	log "CLE_tool is extracted Successfully..!"
else
	log "CLE_tool is failed to  extract Please Check...!"
fi
check_status $(basename $(pwd)) $status
log "========= End of extracting CLE tool ========="
