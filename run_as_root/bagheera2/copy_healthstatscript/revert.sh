#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

set -e

log "==================Start of merge_db.sh script to revert of Splitting of  healthstat BD================"

latest_version=$(readlink /home/ubuntu/.nddevice/latest | cut -d '.' -f1-3)
major_version=$(echo $latest_version | cut -d '.' -f2)
minor_version=$(echo $latest_version | cut -d '.' -f3)
log "Current latest_version is $latest_version"

if [ $major_version -eq 5 ] && [ $minor_version -lt 21 ] ; then

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
