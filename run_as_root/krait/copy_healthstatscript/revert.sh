#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

set -e

log "==================Start of merge_db.sh script to revert of Splitting of  healthstat BD================"

current_version=$(basename $(realpath /home/ubuntu/.nddevice/latest/) | tr -d ' ' | cut -d "." -f 1-3)
if [[ $current_version != "2.5.28" ]]; then 

if [[ -f /home/ubuntu/.nddevice/db/udid.db ]]; then
	log "excueting merge_db.sh script to merge healthstat DB"
        bash merge_db.sh
        status=$?
else
	log "Not excueting merge_db.sh script beacause healthstat DB is not splitted"
        status=0
fi

if [[ $status == 0 ]]; then
	log "merge_db.sh successfully excuted"
	check_status $(basename $(pwd)) $status
else
	log "merge_db.sh Not excuted properly,Please check logs...!!"
	check_status $(basename $(pwd)) $status
fi

else 
log "Revert task not applicable"
fi

log "==================End of merge_db.sh script to revert Splitting of  healthstat BD================"
