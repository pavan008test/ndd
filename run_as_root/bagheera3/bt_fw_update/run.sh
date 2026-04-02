#!/usr/bin/env bash

set -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh


log "=========Copying the latest bluetooth fw BCM4356A2_25_10_2024_001.003.015.0124.0418.hcd ==========="
CHECKSUM_BCM=$(md5sum BCM4356A2_25_10_2024_001.003.015.0124.0418.hcd | awk -F " " '{print $1}')
CHECKSUM_BCM_SYS=$(md5sum /lib/firmware/cypress/BCM4356A2_25_10_2024_001.003.015.0124.0418.hcd 2>/dev/null | awk -F " " '{print $1}')

if [[ -f /lib/firmware/cypress/BCM4356A2_25_10_2024_001.003.015.0124.0418.hcd && $CHECKSUM_BCM == $CHECKSUM_BCM_SYS ]]; then
        log "md5sum of BCM4356A2_25_10_2024_001.003.015.0124.0418.hcd is same as expected. So skipping the copy Exiting..."
	status=0
else
	log "No BCM4356A2_25_10_2024_001.003.015.0124.0418.hcd file present in location /lib/firmware/cypress/ or md5um not matching. Hence copying...."
    	DEST_DIR="/lib/firmware/cypress"									
        copy_file -f "BCM4356A2_25_10_2024_001.003.015.0124.0418.hcd" -d ${DEST_DIR} -b /home/ubuntu/.nddevice/backup/ -m $CHECKSUM_BCM -p 644 -o root:root
	status=$?
	log "copied BCM4356A2_25_10_2024_001.003.015.0124.0418.hcd to /lib/firmware/cypress/"
fi

check_status $(basename $(pwd)) $status

log "====================End of copying the bluetooth fw BCM4356A2_25_10_2024_001.003.015.0124.0418.hcd script===================="
