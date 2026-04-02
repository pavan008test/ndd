#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling function from lib to replace/upgrade the nd_service.sh @ /usr/bin/
CHECKSUM=$(md5sum nd_service.sh | awk -F " " '{print $1}')
status=$(copy_file -f nd_service.sh -d /usr/bin/ -m ${CHECKSUM} -p 775 -o root:root)$?
check_status $(basename $(pwd)) $status

