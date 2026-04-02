#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "==============Start of Check And Update Ext Cam DB ================="

final_status=0

# Take a backup of ext_cam.db if it exists
if [[ -f /home/ubuntu/.nddevice/ext_cam.db ]]; then

    log "ext_cam.db exists in /home/ubuntu/.nddevice"

    if [[ -f /home/ubuntu/.nddevice/backup/ext_cam.db ]]; then
        log "Already ext_cam.db backup is done"
    else
        backup /home/ubuntu/.nddevice/ext_cam.db /home/ubuntu/.nddevice/backup/
        status1=$?
        log "Backed up ext_cam.db"
    fi

    # Check if EXTCAM table exists
    /bin/vendor/sqlite3 /home/ubuntu/.nddevice/ext_cam.db "SELECT name FROM sqlite_master WHERE type='table' AND name='EXTCAM';" | grep "EXTCAM"
    status2=$?
    if [[ $status2 == 0 ]]; then
        log "'EXTCAM' table exists in 'ext_cam.db' database"
        /bin/vendor/sqlite3 /home/ubuntu/.nddevice/ext_cam.db "PRAGMA table_info(EXTCAM);" | grep "PULL"
        status3=$?

        if [[ $status3 != 0 ]]; then
            log "Adding 'PULL' column to 'EXTCAM' table"
            # If "PULL" column does not exist, add it with default value of 0
            /bin/vendor/sqlite3 /home/ubuntu/.nddevice/ext_cam.db "ALTER TABLE EXTCAM ADD COLUMN PULL INTEGER DEFAULT 0;"
            status4=$?

            if [[ $status4 != 0 ]]; then
                    log "Failed to add 'PULL' column to 'EXTCAM' table, deleting ext_cam.db"
                    rm /home/ubuntu/.nddevice/ext_cam.db
                    final_status=$?

                    if [[ $final_status == 0 ]]; then
                        log "Removed the ext_cam.db"
                    else
                        log "Failed To Remove ext_cam.db"
                    fi
            else
                    log "'PULL' column Added Successfully" to 'EXTCAM' table
            fi

        else
            log "'PULL' column already exists in 'EXTCAM' table"
        fi
    else
        log "'EXTCAM' table does not exist in 'ext_cam.db' database"
    fi

else
    log "ext_cam.db file not found"
fi

# Check status and update logs accordingly
if [[ $final_status == 0 ]]; then
    log "Successfully Done Check And Update Of Ext Cam DB"
    check_status $(basename $(pwd)) 0
else
    log "Failed To Check And Update Ext Cam DB"
    check_status $(basename $(pwd)) 1
fi

log "==============END of Check And Update Ext Cam DB ================="
