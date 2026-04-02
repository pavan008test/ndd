#!/usr/bin/env bash

set -e

source  /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "=========start  copying treelite_runtime libraries  =========="

log "Copying treelite_runtime into /usr/lib64/python3.5/site-packages/ path "
tar -xzf treelite_runtime.tar.gz  -C /usr/lib64/python3.5/site-packages/
status=$(echo $?)
check_status $(basename $(pwd)) $status

log "========= END of copying treelite_runtime libraries =========="
