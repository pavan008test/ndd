#!/usr/bin/env bash

set -e

source  /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "=============Start of Reverting ntdi_bag2_startup ================="


if [[ -f /dev/shm/ntdi_task_status_file ]]; then

	log "Reverting ntdi_bag2_startup as it has replaced"
	cp -f /home/ubuntu/.nddevice/ntdi_bag2_startup /etc/init.d/
	status=$?
	rm -f /dev/shm/ntdi_task_status_file
else
	log "Not reverting ntdi_bag2_startup"
	status=0

fi

check_status $(basename $(pwd)) $status





log "===============End of Reverting ntdi_bag2_startup==================="
