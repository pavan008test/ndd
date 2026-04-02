#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "========= Start of Copy Overlay Mounts Task =========="

status_value=0
status_array=()

# Step 1: Create required overlay filesystem directories in /media/data/
log "Step 1: Creating required overlay filesystem directories in /media/data/"

# Define the directories needed for overlay filesystems
overlay_dirs=(
    "/media/data/overlay_var_log/upper"
    "/media/data/overlay_var_log/work"
    "/media/data/overlay_nddevice_log/upper"
    "/media/data/overlay_nddevice_log/work"
    "/media/data/overlay_var_backups/upper"
    "/media/data/overlay_var_backups/work"
    "/media/data/overlay_var_tmp/upper"
    "/media/data/overlay_var_tmp/work"
)

# Create each directory
for dir in "${overlay_dirs[@]}"; do
    log "Creating directory: $dir"
    mkdir -p "$dir"
    create_status=$?
    status_array+=($create_status)

    if [[ $create_status -eq 0 ]]; then
        log "Directory $dir created successfully"
        # Set appropriate permissions
        chown root:root "$dir"
        chmod 775 "$dir"
        log "Status: Success : Directory $dir created successfully with status $create_status"
    else
        log "Status: Failure : Directory $dir creation failed with status $create_status"
    fi
done

# Step 2: Copy sync-early.sh script to /usr/local/bin/
log "Step 2: Copying sync-early.sh to /usr/local/bin/"

if [[ -f "sync-early.sh" ]]; then
    sync_early_md5sum=$(md5sum sync-early.sh | awk '{print $1}')

    # Check if file already exists with same checksum
    if [[ -f "/usr/local/bin/sync-early.sh" ]]; then
        system_md5sum=$(md5sum /usr/local/bin/sync-early.sh | awk '{print $1}')
        if [[ "$sync_early_md5sum" == "$system_md5sum" ]]; then
            log "sync-early.sh already exists with same checksum. Skipping copy."
            status_array+=(0)
        else
            log "sync-early.sh checksum differs. Copying new version."
            copy_file -f sync-early.sh -d /usr/local/bin/ -b /home/ubuntu/.nddevice/backup -m $sync_early_md5sum -p 775 -o root:root
            status_array+=($?)
        fi
    else
        log "sync-early.sh found. Copying to /usr/local/bin/"
        copy_file -f sync-early.sh -d /usr/local/bin/ -b /home/ubuntu/.nddevice/backup -m $sync_early_md5sum -p 775 -o root:root
        status_array+=($?)
    fi
    log "Copy status for sync-early.sh: ${status_array[-1]}"
else
    log "sync-early.sh not found in current directory"
    status_array+=(1)
fi

# Step 3: Copy sync-early@.service to /etc/systemd/system/
log "Step 3: Copying sync-early@.service to /etc/systemd/system/"

if [[ -f "sync-early@.service" ]]; then
    sync_early_service_md5sum=$(md5sum "sync-early@.service" | awk '{print $1}')

    # Check if file already exists with same checksum
    if [[ -f "/etc/systemd/system/sync-early@.service" ]]; then
        system_md5sum=$(md5sum "/etc/systemd/system/sync-early@.service" | awk '{print $1}')
        if [[ "$sync_early_service_md5sum" == "$system_md5sum" ]]; then
            log "sync-early@.service already exists with same checksum. Skipping copy."
            status_array+=(0)
        else
            log "sync-early@.service checksum differs. Copying new version."
            copy_file -f "sync-early@.service" -d /etc/systemd/system/ -b /home/ubuntu/.nddevice/backup -m $sync_early_service_md5sum -p 644 -o root:root
            status_array+=($?)
        fi
    else
        log "sync-early@.service found. Copying to /etc/systemd/system/"
        copy_file -f "sync-early@.service" -d /etc/systemd/system/ -b /home/ubuntu/.nddevice/backup -m $sync_early_service_md5sum -p 644 -o root:root
        status_array+=($?)
    fi
    log "Copy status for sync-early@.service: ${status_array[-1]}"
else
    log "sync-early@.service not found in current directory"
    status_array+=(1)
fi

# Step 4: Move mount service files to /lib/systemd/system/
log "Step 4: Moving mount service files to /lib/systemd/system/"

# Define mount service files (excluding sync-logs.service)
mount_services=(
    "var-log.mount"
    "home-ubuntu-.nddevice-log.mount"
    "var-backups.mount"
    "var-tmp.mount"
)

# Copy each mount service file
for service_file in "${mount_services[@]}"; do
    if [[ -f "$service_file" ]]; then
        service_md5sum=$(md5sum "$service_file" | awk '{print $1}')

        # Check if file already exists with same checksum
        if [[ -f "/lib/systemd/system/$service_file" ]]; then
            system_md5sum=$(md5sum "/lib/systemd/system/$service_file" | awk '{print $1}')
            if [[ "$service_md5sum" == "$system_md5sum" ]]; then
                log "$service_file already exists with same checksum. Skipping copy."
                status_array+=(0)
            else
                log "$service_file checksum differs. Copying new version."
                copy_file -f "$service_file" -d /lib/systemd/system/ -b /home/ubuntu/.nddevice/backup -m $service_md5sum -p 644 -o root:root
                status_array+=($?)
            fi
        else
            log "Copying $service_file to /lib/systemd/system/"
            copy_file -f "$service_file" -d /lib/systemd/system/ -b /home/ubuntu/.nddevice/backup -m $service_md5sum -p 644 -o root:root
            status_array+=($?)
        fi
        log "Copy status for $service_file: ${status_array[-1]}"
    else
        log "$service_file not found in current directory"
        status_array+=(1)
    fi
done

# Step 5: Mask all mount services to prevent auto-start by requires dependencies
log "Step 5: Masking mount services to prevent auto-start"

# Mask each mount service
for service_file in "${mount_services[@]}"; do
    log "Masking $service_file"
    systemctl mask "$service_file"
    mask_status=$?
    status_array+=($mask_status)

    if [[ $mask_status -eq 0 ]]; then
        log "Status: Success : $service_file masked successfully"
    else
        log "Status: Failure : $service_file masking failed with status $mask_status"
    fi
done

# Check overall status
log "Checking overall operation status"
for i in "${status_array[@]}"; do
    if [[ "$i" -ne 0 ]]; then
        log "One or more operations failed."
        status_value=1
        break
    fi
done

log "All overlay mount operations completed. Final Status: $status_value"

check_status "Copy Overlay Mounts Task" $status_value

log "========= End of Copy Overlay Mounts Task =========="
