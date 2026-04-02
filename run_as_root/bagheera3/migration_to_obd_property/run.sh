#!/usr/bin/env bash

set -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "============= Start of executing obd_db_mig.sh to change db for obd service  ==================="

    log "======= Executing obd_db_mig.sh for migrating db for obd service  ======="
    execute_command bash obd_db_mig.sh >> /home/ubuntu/.nddevice/log/bhcopy.log 2>&1
    status1=$(echo $?)


if [[ $status1 == 0 ]]; then
    log "======= obd_db_mig.sh script executed successfully======="
    status=0
    check_status $(basename $(pwd)) $status
else
    log "======= Something went wrong to execute obd_db_mig.sh script , please check======="
    status=1
    check_status $(basename $(pwd)) $status
fi

log "============= End of executing obd_db_mig.sh to change db for obd service  ==================="

