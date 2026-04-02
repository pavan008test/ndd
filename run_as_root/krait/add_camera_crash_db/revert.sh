#!/usr/bin/env bash

source  /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh


log "========= Setting up camera crash DB =========="

bash -x merge_cameracrash_genproperty_db_krait.sh >> /data/nd_files/log/bhcopy.log 2>&1
status=$?

check_status $(basename $(pwd)) $status

log "========= Setting up camera crash DB Completed =========="
