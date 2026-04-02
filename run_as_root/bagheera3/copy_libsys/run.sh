#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "============ Start of copying libsys.so ==========="

BACKUP_DIR="/home/ubuntu/backup/run_as_root/libsys/"
mkdir -p "$BACKUP_DIR"

if [[ -L /lib/libsys.so ]]; then
    log "/lib/libsys.so is a symlink, moving it to backup"
    mv -f /lib/libsys.so "$BACKUP_DIR"
    move_linked_file=$?
else
    move_linked_file=0
fi


new_libsys_md5sum=$(md5sum libsys.so | awk '{print $1}')
if [[ -f /lib/libsys.so ]]; then
    md5sum_libsys_system=$(md5sum /lib/libsys.so | awk '{print $1}')
else
    md5sum_libsys_system=""
fi
libsys_version_md5sum=$(md5sum libsys_version | awk '{print $1}')
if [[ -f /lib/libsys_version ]]; then
    libsys_version_md5sum_system=$(md5sum /lib/libsys_version | awk '{print $1}')
else
    libsys_version_md5sum_system=""
fi

log "start of Copying the libsys.so"
if [[ $new_libsys_md5sum == $md5sum_libsys_system ]]; then
    log "Already Updated libsys.so is there, So not copying"
    status=0
else
    log "Copying the latest libsys.so"
    copy_file -f libsys.so -d /lib/ -b "$BACKUP_DIR" -m $new_libsys_md5sum -p 755 -o root:root
    status=$?
fi

log "start of Copying the libsys_version"
if [[ -f /lib/libsys_version ]] && [[ $libsys_version_md5sum == $libsys_version_md5sum_system ]]; then
    log "Already Updated libsys_version is there, So not copying"
    status_version=0
else
    log "Copying the latest libsys_version"
    copy_file -f libsys_version -d /lib/ -b "$BACKUP_DIR" -m $libsys_version_md5sum -p 644 -o root:root
    status_version=$?
fi

if [[ $status -eq 0 ]] && [[ $status_version -eq 0 ]] && [[ $move_linked_file -eq 0 ]]; then
    final_status=0
else
    final_status=1
fi

check_status $(basename $(pwd)) $final_status

log "============ End of copying libsys.so ==========="
