#!/bin/bash
set -e

source  /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

BASE_PATH="/nd_lib"
log "============== start of removing $BASE_PATH/libndsnpe.so ==================="
if [[ -f $BASE_PATH/libndsnpe.so ]]; then

	log " removing of $BASE_PATH/libndsnpe.so ...!!"

	rm -rf $BASE_PATH/libndsnpe.so
	status=$?
	check_status $(basename $(pwd)) $status
else
	log " File is not present may be removed... exiting $BASE_PATH/libndsnpe.so ...!!"
	check_status $(basename $(pwd)) 0
fi
log "============== end of removing $BASE_PATH/libndsnpe.so ==================="
