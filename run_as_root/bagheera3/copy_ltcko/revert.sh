#!/usr/bin/env bash

set -e

source  /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "=============Start of Reverting ltc3350.ko ================="


if [[ -f /home/ubuntu/.nddevice/backup/ltc3350.ko ]]; then

	log "Reverting ltc3350.ko to older one as 13.0.16"
	cp -f /home/ubuntu/.nddevice/backup/ltc3350.ko /lib/modules/4.9.299-tegra/extra/
	status=$?
else
	log "No ltc3350.ko file in backup"
	status=0

fi

check_status $(basename $(pwd)) $status

log "===============End of Reverting ltc3350.ko ==================="
