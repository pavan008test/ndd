#!/bin/bash
set -e

source  /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

BASE_PATH="/home/ubuntu/.nddevice/bootstrap/service/"
log "============== start of removing $BASE_PATH/nd_onetime_setup ==================="
if [[ -d $BASE_PATH/nd_onetime_setup ]]; then

	log " removing of $BASE_PATH/nd_onetime_setup ...!!"

	rm -rvf $BASE_PATH/nd_onetime_setup
	rm -rvf /home/root/.ash_history
	status=$?
	check_status $(basename $(pwd)) $status
else
	log " $BASE_PATH/nd_onetime_setup Folder is not present may be removed... ...!!"
	check_status $(basename $(pwd)) 0
fi
log "============== end of removing $BASE_PATH/nd_onetime_setup ==================="

