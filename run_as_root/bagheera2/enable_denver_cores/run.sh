#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh


#Calling function from lib to replace/upgrade the nd_service.sh @ /usr/bin/
CHECKSUM_ext=$(md5sum extlinux_pri.conf | awk -F " " '{print $1}')
CHECKSUM_nvp=$(md5sum nvpmodel_t186.conf | awk -F " " '{print $1}')

status=$(copy_file -f extlinux_pri.conf -b /boot/extlinux/backup/ -d /boot/extlinux/ -m ${CHECKSUM_ext} -p 644 -o root:root)$?
check_status $(basename $(pwd))_ext $status
status=$(copy_file -f nvpmodel_t186.conf -b /etc/nvpmodel/backup/ -d /etc/nvpmodel/ -m ${CHECKSUM_nvp} -p 644 -o root:root)$?
check_status $(basename $(pwd))_nvp $status
