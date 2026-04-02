#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "========= Start of Updating /tmp on tmpfs  =========="

status_value=0
status_array=()

tmp_mount_tmpfs_system=$(md5sum /lib/systemd/system/tmp.mount | awk '{print $1}')

tmp_mount_tmpfs=$(md5sum tmp.mount | awk '{print $1}')
if [[ ! -f /lib/systemd/system/tmp.mount || "$tmp_mount_tmpfs_system" != "$tmp_mount_tmpfs" ]]; then
    log "/lib/systemd/system/tmp.mount not found or different. Copying file."
    copy_file -f tmp.mount -d /lib/systemd/system -m $tmp_mount_tmpfs -p 644 -o root:root
    status_array+=($?)
    log " The copy_file command exited with status ${status_array[-1]}"

    log " Reloading systemd manager configuration "
    systemctl daemon-reload
    status_array+=($?)
    log " The systemctl daemon-reload command exited with status ${status_array[-1]}"


    log " Disabling the tmp.mount service "
    systemctl disable tmp.mount
        status_array+=($?)

    log " The systemctl disable tmp.mount command exited with status ${status_array[-1]}"
    
    for i in "${status_array[@]}"; do
        if [[ "$i" -ne 0 ]]; then
            log "One of the operations failed."
            status_value=1
            break
        fi
    done

    log "All operations Complete Final Status : $status_value."

else
    log "Same file is found. No need to copy."
fi

check_status "Update /tmp on tmpfs" $status_value

log "========= End of Updating /tmp on tmpfs  =========="
