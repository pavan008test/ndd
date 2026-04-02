#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "========== Starting execution of delete_udid_table task =========="
#Calling execute_command function from lib
if [[ -f /home/ubuntu/.nddevice/db/healthstats.db ]]; then
    sqlite3 /home/ubuntu/.nddevice/db/healthstats.db "DROP TABLE IF EXISTS UDID_TABLE;" 
    status=$?
    check_status $(basename $(pwd)) $status
    log "UDID_TABLE is deleted as there was an update done in table schema."
else
    log "udid table is not deleted as the healthstats.db file is not avaiable @ /home/ubuntu/.nddevice/db/"
fi
log "========== END of delete_udid_table task execution =========="

