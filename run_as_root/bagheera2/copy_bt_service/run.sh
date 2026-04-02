#!/usr/bin/env bash

source  /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "========= Start of Updating BT  =========="

md5sum_nv_bluetooth_service_conf=$(md5sum nv-bluetooth-service.conf | awk '{print $1}')

copy_file -f nv-bluetooth-service.conf -d /lib/systemd/system/bluetooth.service.d -m $md5sum_nv_bluetooth_service_conf -p 644 -o root:root
status=$?

check_status $(basename $(pwd)) $status

log "========= End of Updating BT  =========="
