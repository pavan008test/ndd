#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "========= Reverting of Updating /tmp on tmpfs  =========="

rm -f /lib/systemd/system/tmp.mount
check_status $(basename $(pwd)) $?

log "========= End of Reverting /tmp on tmpfs  =========="

