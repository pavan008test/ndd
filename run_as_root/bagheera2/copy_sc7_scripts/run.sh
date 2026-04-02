#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

CHECKSUM_exit=$(md5sum sc7_exit.sh | awk -F " " '{print $1}')
status1=$(copy_file -f sc7_exit.sh -d  /etc/init.d/ -m ${CHECKSUM_exit} -p 755 -o root:root)$?

CHECKSUM_entry=$(md5sum sc7_entry.sh | awk -F " " '{print $1}')
status2=$(copy_file -f sc7_entry.sh -d  /etc/init.d/ -m ${CHECKSUM_entry} -p 755 -o root:root)$?

if [[ $status1 == 0 && $status2 == 0 ]]; then 
status=0
check_status $(basename $(pwd)) $status
fi

