#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "============ Start of reverting libsys.so ==========="

BACKUP_FILE="/home/ubuntu/backup/run_as_root/libsys/libsys.so"

if [[ -e "$BACKUP_FILE" ]] || [[ -L "$BACKUP_FILE" ]]; then
    log "Backup file found at $BACKUP_FILE. Restoring libsys.so"

    mv -f "$BACKUP_FILE" /lib/
    copy_libsys_status=$?

    if [[ -f "${BACKUP_DIR}libsys_version" ]]; then
        log "Restoring libsys_version file to /lib/"
        cp -f "${BACKUP_DIR}libsys_version" "/lib/libsys_version"
        copy_libsys_version_status=$?
    else
        log "No libsys_version found; libsys.so may have been a static file."
        copy_libsys_version_status=0
    fi
else
    log "No backup file found for libsys.so. Cannot restore."
    copy_libsys_status=0
    copy_libsys_version_status=0
fi

if [[ $copy_libsys_version_status -eq 0 ]] && [[ $copy_libsys_status -eq 0 ]]; then
    final_status=0
else
    final_status=1
fi

check_status $(basename $(pwd)) $final_status

log "============ End of reverting libsys.so ==========="
