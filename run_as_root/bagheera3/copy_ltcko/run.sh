#!/usr/bin/env bash

set -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "============ Start of copying ltc3350.ko ==========="

md5sum_local=$(md5sum ltc3350.ko | awk '{print $1}')
md5sum_system=$(md5sum /lib/modules/4.9.299-tegra/extra/ltc3350.ko | awk '{print $1}')

if [[ $md5sum_local == $md5sum_system ]]; then

	log "Already Updated ltc3350.ko is there,So not copying"
	status=0
else
	copy_file -f ltc3350.ko -d /lib/modules/4.9.299-tegra/extra/ -b /home/ubuntu/.nddevice/backup -m $md5sum_local -p 664 -o root:root
	status=$?
fi
check_status $(basename $(pwd)) $status


log "============ End of copy_ltc3350.ko ==========="
