#!/usr/bin/env bash

set -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "============ Start of copying dms_i2c ==========="

md5sum_dms_i2c=$(md5sum dms_i2c | awk '{print $1}')

if [[ -f /bin/vendor/dms_i2c ]]; then
    if [[ $(md5sum /bin/vendor/dms_i2c 2>/dev/null | awk -F " " '{print $1}') != 080b7efffd3c205ad731a9637a1c97a7 ]]; then
        log "md5sum not matched for dms_i2c, replacing..."
        DEST_DIR="/bin/vendor"
        copy_file -f "dms_i2c" -d ${DEST_DIR} -b /home/ubuntu/.nddevice/backup/ -m 080b7efffd3c205ad731a9637a1c97a7 -p 755 -o root:root
	status=$?
	log "copied dms_i2c to /bin/vendor/"
    else
        log "md5sum of dms_i2c is same as expected. So skipping the replacement. Exiting..."
	status=0
    fi
else
	log "No dms_i2c file present in location /bin/vendor. copying...."
    	DEST_DIR="/bin/vendor"									
        copy_file -f "dms_i2c" -d ${DEST_DIR} -b /home/ubuntu/.nddevice/backup/ -m 080b7efffd3c205ad731a9637a1c97a7 -p 755 -o root:root
	status=$?
	log "copied dms_i2c to /bin/vendor/"
fi

check_status $(basename $(pwd)) $status
log "============ End of copying dms_i2c ==========="
