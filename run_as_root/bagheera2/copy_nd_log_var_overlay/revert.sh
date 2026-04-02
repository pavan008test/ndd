#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "========= Reverting Copy Overlay Mounts Task =========="

status_value=0
status_array=()

# Step 1: Unmount overlay filesystems
log "Step 1: Unmounting overlay filesystems"

mount_points=(
    "/var/log"
    "/home/ubuntu/.nddevice/log"
    "/var/backups"
    "/var/tmp"
)

for mount_point in "${mount_points[@]}"; do
    if [[ -d "$mount_point" ]] && mountpoint -q "$mount_point"; then
        log "Unmounting $mount_point"
        umount -l "$mount_point"
        umount_status=$?
        status_array+=($umount_status)

        if [[ $umount_status -eq 0 ]]; then
            log "Successfully unmounted $mount_point"
        else
            log "Failed to unmount $mount_point"
        fi
    elif [[ ! -d "$mount_point" ]]; then
        log "$mount_point directory does not exist, skipping unmount"
        status_array+=(0)
    else
        log "$mount_point is not mounted, skipping"
        status_array+=(0)
    fi
done

# Step 2: Remove mount service files from /lib/systemd/system/
log "Step 2: Removing mount service files from /lib/systemd/system/"

mount_services=(
    "var-log.mount"
    "home-ubuntu-.nddevice-log.mount"
    "var-backups.mount"
    "var-tmp.mount"
)

for service_file in "${mount_services[@]}"; do
    log "Removing /lib/systemd/system/$service_file"
    rm -f "/lib/systemd/system/$service_file"
    remove_status=$?
    status_array+=($remove_status)

    if [[ $remove_status -eq 0 ]]; then
        log "Successfully removed /lib/systemd/system/$service_file"
    else
        log "Failed to remove /lib/systemd/system/$service_file"
    fi
done

# Step 3: Remove sync-early@.service from /etc/systemd/system/
log "Step 3: Removing sync-early@.service from /etc/systemd/system/"

log "Removing /etc/systemd/system/sync-early@.service"
rm -f "/etc/systemd/system/sync-early@.service"
remove_status=$?
status_array+=($remove_status)

if [[ $remove_status -eq 0 ]]; then
    log "Successfully removed /etc/systemd/system/sync-early@.service"
else
    log "Failed to remove /etc/systemd/system/sync-early@.service"
fi

# Step 4: Remove sync-early.sh from /usr/local/bin/
log "Step 4: Removing sync-early.sh from /usr/local/bin/"

log "Removing /usr/local/bin/sync-early.sh"
rm -f "/usr/local/bin/sync-early.sh"
remove_status=$?
status_array+=($remove_status)

if [[ $remove_status -eq 0 ]]; then
    log "Successfully removed /usr/local/bin/sync-early.sh"
else
    log "Failed to remove /usr/local/bin/sync-early.sh"
fi


# Step 5: Perform reverse sync to restore original files before removing overlay directories
log "Step 5: Performing reverse sync to restore original files"

# Define sync mappings (reverse of what sync-early.sh does)
sync_mappings=(
    "/media/data/overlay_var_log/upper:/var/log"
    "/media/data/overlay_nddevice_log/upper:/home/ubuntu/.nddevice/log"
    "/media/data/overlay_var_backups/upper:/var/backups"
    "/media/data/overlay_var_tmp/upper:/var/tmp"
)

for mapping in "${sync_mappings[@]}"; do
    src="${mapping%:*}"
    dest="${mapping#*:}"

    if [[ -d "$src" ]]; then
        if [[ -d "$dest" ]]; then
            log "Reverse syncing from $src to $dest"

            # Perform reverse sync (from overlay back to original location)
            rsync -a --inplace --no-compress "$src"/ "$dest"/
            rsync_status=$?
            status_array+=($rsync_status)

            if [[ $rsync_status -eq 0 ]]; then
                log "Successfully reverse synced $src to $dest"
            else
                log "Failed to reverse sync $src to $dest"
            fi
        else
            log "Destination directory $dest does not exist, skipping reverse sync"
            status_array+=(0)
        fi
    else
        log "Source directory $src does not exist, skipping reverse sync"
        status_array+=(0)
    fi
done

# Step 6: Remove overlay filesystem directories from /media/data/
log "Step 6: Removing overlay filesystem directories from /media/data/"

overlay_dirs=(
    "/media/data/overlay_var_log"
    "/media/data/overlay_nddevice_log"
    "/media/data/overlay_var_backups"
    "/media/data/overlay_var_tmp"
)

for dir in "${overlay_dirs[@]}"; do
    if [[ -d "$dir" ]]; then
        log "Removing directory: $dir"
        rm -rf "$dir"
        remove_status=$?
        status_array+=($remove_status)
        if [[ $remove_status -eq 0 ]]; then
            log "Successfully removed directory $dir"
        else
            log "Failed to remove directory $dir"
        fi
    else
        log "Directory $dir does not exist, skipping"
        status_array+=(0)
    fi
done

# Step 7: Reload systemd daemon to reflect changes
log "Step 7: Reloading systemd daemon"
systemctl daemon-reload
daemon_reload_status=$?
status_array+=($daemon_reload_status)

if [[ $daemon_reload_status -eq 0 ]]; then
    log "Successfully reloaded systemd daemon"
else
    log "Failed to reload systemd daemon"
fi

# Check overall status
log "Checking overall revert operation status"
for i in "${status_array[@]}"; do
    if [[ "$i" -ne 0 ]]; then
        log "One or more revert operations failed."
        status_value=1
        break
    fi
done

log "All overlay mount revert operations completed. Final Status: $status_value"

check_status $(basename $(pwd)) $status_value

log "========= End of Reverting Copy Overlay Mounts Task =========="
