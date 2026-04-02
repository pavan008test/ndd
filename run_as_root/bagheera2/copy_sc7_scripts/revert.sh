#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "==================Start of revert sc7_entry and sc7_exit scripts================"

latest_version=$(readlink /home/ubuntu/.nddevice/latest | cut -d '.' -f1-3)
major_version=$(echo $latest_version | cut -d '.' -f2)
minor_version=$(echo $latest_version | cut -d '.' -f3)
log "Current latest_version is $latest_version"

if [ $major_version -eq 5 ] && [ $minor_version -lt 21 ] ; then

CHECKSUM_exit=$(md5sum original_12.3.1/sc7_exit.sh | awk -F " " '{print $1}')
status1=$(copy_file -f original_12.3.1/sc7_exit.sh -d  /etc/init.d/ -m ${CHECKSUM_exit} -p 755 -o root:root)$?

CHECKSUM_entry=$(md5sum original_12.3.1/sc7_entry.sh | awk -F " " '{print $1}')
status2=$(copy_file -f original_12.3.1/sc7_entry.sh -d  /etc/init.d/ -m ${CHECKSUM_entry} -p 755 -o root:root)$?

if [[ $status1 == 0 && $status2 == 0 ]]; then 
status=0
check_status $(basename $(pwd)) $status
fi

else 
log "Revert not applicable"
fi

log "==================End of revert sc7_entry and sc7_exit scripts================"
