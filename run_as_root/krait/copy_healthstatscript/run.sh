#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

set -e

log "==================Start of split_db.sh script to Split healthstat DB================"

if [[ ! -f /home/ubuntu/.nddevice/db/udid.db ]]; then
	log "excueting split_db.sh script to split healthstat DB"
	bash split_db.sh
	status=$?
else
	log "Healthstat DB already splitted"
	status=0
fi
if [[ $status == 0 ]]; then
	log "split_db.sh successfully excuted"
	check_status $(basename $(pwd)) $status
else
	log "split_db.sh Not excuted properly,Please check logs...!!"
	check_status $(basename $(pwd)) $status
fi

log "==================End of split_db.sh script to Split healthstat BD================"
