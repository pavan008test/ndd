#!/usr/bin/env bash 

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

BACKUP_DIR="/home/ubuntu/backup/IMU_KO/"
DEST_DIR="/lib/modules/4.9.299-tegra/kernel/drivers/iio/imu/inv_mpu/inv_mpu_43600/"

log "============== Starting revert operation for replace_update_inv_driver ===================="

final_status=0

# Check version and skip revert if current and upgrade versions match
current_version_full=$(sed -n '/^\[version\]/,/^\[/{s/^nddevice *= *//p}' /home/ubuntu/.nddevice/nddevice.ini)
upgrade_version_full=$(sed -n '/^\[upgrade\]/,/^\[/{s/^nddevice *= *//p}' /home/ubuntu/.nddevice/nddevice.ini)

current_version=$(echo "$current_version_full" | cut -d. -f1-3)
upgrade_version=$(echo "$upgrade_version_full" | cut -d. -f1-3)

log "Current version detected: $current_version"
log "Upgrade version detected: $upgrade_version"

    if [[ "$current_version" == "$upgrade_version" ]]; then
        log "Current version and upgrade version match ($current_version). Skipping revert operation."
        exit 0
    fi

log "Versions differ (current: $current_version, upgrade: $upgrade_version). Proceeding with revert operation."

remove_module_if_loaded() {
    module_name="$1"
    if lsmod | grep -q "$module_name"; then
        if ! rmmod "$module_name"; then
            log "ERROR: rmmod failed for $module_name module."
            return 1
        fi
        log "Found and removed $module_name module."
        sleep 1
        log "Sleeping for 1 sec after removing $module_name module"
    else
        log "$module_name module not found. Skipping removal."
    fi
    return 0
}

load_modules_if_not_loaded() {
    module_name="$1"
    if lsmod | grep -q "$module_name"; then
        log "$module_name module already loaded. Skipping modprobe."
        return 0
    else
        if modprobe "$module_name"; then
            log "$module_name module loaded."
            return 0
        else
            log "modprobe failed for $module_name, trying insmod."
            local ko_file="${DEST_DIR}${module_name//_/-}.ko"
            log "Attempting insmod with ko_file: $ko_file"
            if insmod "$ko_file"; then
                log "$module_name module loaded with insmod."
                return 0
            else
                log "insmod failed for $module_name."
                return 1
            fi
        fi
    fi
}

remove_module_if_loaded "inv_mpu_iio_i2c"
if [[ $? -ne 0 ]]; then
    final_status=1
fi

remove_module_if_loaded "inv_mpu_iio"
if [[ $? -ne 0 ]]; then
    final_status=1
fi

for file in "inv-mpu-iio.ko_org" "inv-mpu-iio-i2c.ko_org"; do
    src="${BACKUP_DIR}${file}"
    dest="${DEST_DIR}${file}"
    if [ -f "$src" ]; then
        log "Restoring $file from backup."
        if ! cp -f "$src" "$dest"; then
            log "ERROR: Failed to copy $file to $dest"
            final_status=1
            continue
        fi
        if cmp -s "$src" "$dest"; then
            mv "$dest" "${DEST_DIR}${file/_org/}"
            final_name="${DEST_DIR}${file/_org/}"
            log "Restored and renamed $file to $final_name"
            log "File is in destination: $final_name"
        else
            log "Checksum mismatch for $file. Aborting."
            final_status=1
        fi
    else
        log "Backup $file not found. Skipping."
    fi
done

if [ -f "${DEST_DIR}inv-mpu-iio.ko" ]; then
    load_modules_if_not_loaded "inv_mpu_iio"
    if [[ $? -ne 0 ]]; then
        final_status=1
    fi
else
    log "inv-mpu-iio.ko not found in ${DEST_DIR}. Skipping load."
fi


if [ -f "${DEST_DIR}inv-mpu-iio-i2c.ko" ]; then
    load_modules_if_not_loaded "inv_mpu_iio_i2c"
    if [[ $? -ne 0 ]]; then
        final_status=1
    fi
else
    log "inv-mpu-iio-i2c.ko not found in ${DEST_DIR}. Skipping load."
fi

log "============== End of revert operation for replace_update_inv_driver ===================="
