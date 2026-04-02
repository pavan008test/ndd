#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling function from lib to replace/upgrade the deleter_helper.sh @ /usr/bin/
CHECKSUM=$(md5sum deleter_helper.sh | awk -F " " '{print $1}')
status=$(copy_file -f deleter_helper.sh -d /usr/bin/ -m ${CHECKSUM} -p 775 -o root:root)$?
CHECKSUM=$(md5sum ubuntu_admin2 | awk -F " " '{print $1}')
status=$(copy_file -f ubuntu_admin2 -d /etc/sudoers.d -m ${CHECKSUM} -p 775 -o root:root)$?
execute_command chmod 440 /etc/sudoers.d/ubuntu_admin2
status=$(echo $?) 
check_status $(basename $(pwd)) $status
