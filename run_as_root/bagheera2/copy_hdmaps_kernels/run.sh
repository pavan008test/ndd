#!/usr/bin/env bash

set -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling copy funtion from lib

CHECKSUM_pps=$(md5sum pps_ts.ko | awk -F " " '{print $1}')
CHECKSUM_gst=$(md5sum libgstvideo4linux2.so | awk -F " " '{print $1}')


status=$(copy_file -f pps_ts.ko -d /lib/modules/4.9.140-tegra/kernel/drivers/misc/ -b /home/ubuntu/.nddevice/backup -m $CHECKSUM_pps -p 665 -o ubuntu:ubuntu)$?
check_status $(basename $(pwd))_pps $status
sudo rm -f /etc/modules-load.d/nd.conf
status=$(execute_command sudo ln -s /home/ubuntu/.nddevice/latest/nd_kernel_modules/nd.conf /etc/modules-load.d/nd.conf)$?
check_status $(basename $(pwd))_ndconf $status
status=$(copy_file -f libgstvideo4linux2.so -d /usr/lib/aarch64-linux-gnu/gstreamer-1.0/ -b /home/ubuntu/.nddevice/backup -m $CHECKSUM_gst -p 644 -o root:root)$?
check_status $(basename $(pwd))_gst $status
execute_command depmod -a
