#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

DB_PATH="/home/ubuntu/.nddevice/camera_crash.db"
GENPROP_DB="/home/ubuntu/.nddevice/gen_property.db"

check_cam_crash_count() {
    if sqlite3 "$DB_PATH" "SELECT 1 FROM CAMERA_CRASH_DB WHERE PROPERTY LIKE 'CAM%_CRASH_COUNT' LIMIT 1;" | grep -q 1; then
        log "Integrity check successful: At least one CAM%_CRASH_COUNT found in camera crash DB."
        status2=0
    else
        log "Integrity check failed: No CAM%_CRASH_COUNT found in camera crash DB."
        status2=1
    fi
}


log "========= Checking camera crash DB =========="

if [ ! -f "$DB_PATH" ]; then
    log "DB not present. Setting up camera crash DB."
    bash -x split_genprop_to_camera_crash_db_bagheera.sh >> /home/ubuntu/.nddevice/log/bhcopy.log 2>&1
    status1=$?
    check_cam_crash_count
else
    log "DB present. Checking CAM%_CRASH_COUNT in GENPROP_DB..."
    if sqlite3 "$GENPROP_DB" "SELECT 1 FROM GENPROP WHERE PROPERTY LIKE 'CAM%_CRASH_COUNT' LIMIT 1;" | grep -q 1; then
        log "CAM%_CRASH_COUNT found in GENPROP_DB. Re-running setup."
        bash -x split_genprop_to_camera_crash_db_bagheera.sh >> /home/ubuntu/.nddevice/log/bhcopy.log 2>&1
        status1=$?
        check_cam_crash_count
    else
        check_cam_crash_count
        status1=0
    fi
fi

if [ $status1 -eq 0 ] && [ $status2 -eq 0 ]; then
    log "Camera crash DB setup completed successfully."
    check_status $(basename $(pwd)) 0

else
    log "Camera crash DB setup failed. DB Creation Status: status1=$status1 ; DB Creation Integrity: status2=$status2 "
    check_status $(basename $(pwd)) 1
fi


log "========= Setting up camera crash DB Completed =========="
