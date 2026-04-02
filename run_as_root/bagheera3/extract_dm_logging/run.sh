#!/usr/bin/env bash

set -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log  "=========== Start of unzipping dm_logging.zip file ==========="
log  "dm_logging.zip unzipping the zip file to /home/ubuntu/"

unzip -o dm_logging.zip -d /home/ubuntu/
status=$?

if [[ $status == 0 ]]; then
	log "zip file successfully unzipped"
else
	log "zip file not unzipped please check...!"
fi

check_status $(basename $(pwd)) $status
log "=========== End of unzipped dm_logging.zip file ============"




