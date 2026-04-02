#!/usr/bin/env bash

set -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh


log "============start of Removing MSGQ folder======================"

rm -rf /home/ubuntu/.nddevice/MSGQ

status=$?

if [[ $status == 0 ]]; then
	log "MSGQ Folder removed successfully"
else
	log "MSGQ folder not removed please check..!"
fi
check_status $(basename $(pwd)) $status

log "=================End of Removing MSGQ Folder==================="
