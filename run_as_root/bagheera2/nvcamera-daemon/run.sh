#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling copy funtion from lib 
md5sum_nvargus=$(md5sum nvargus-daemon.service | awk -F " " '{print $1}')
md5sum_e33=$(md5sum e33_centerleft_P5V27C.isp | awk -F " " '{print $1}')

status1=$(copy_file -f nvargus-daemon.service -d /etc/systemd/system/ -b /home/ubuntu/.nddevice/backup -m ${md5sum_nvargus} -p 644 -o root:root)$?
status2=$(copy_file -f e33_centerleft_P5V27C.isp -d /var/nvidia/nvcam/settings/ -b /home/ubuntu/.nddevice/backup -m ${md5sum_e33} -p 644 -o root:root)$?

if [[ $status1 == 0 && $status2 == 0 ]]; then status=0 ; else status=1 ; fi;
check_status $(basename $(pwd)) $status

