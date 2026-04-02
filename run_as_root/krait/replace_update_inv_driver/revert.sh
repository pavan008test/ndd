#!/usr/bin/env bash 

set -e 

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

BACKUP_DIR="/home/ubuntu/backup/IMU_KO/"
DEST_DIR="/usr/lib/modules/4.9.160-perf/kernel/drivers/iio/imu/inv_mpu_20602/"

log "==============Starting revert operation for IMU KO===================="

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

log "Removing existing modules..."
rmmod inv_mpu_iio_i2c_icm20602
if [[ $? != 0 ]]; then 
    log "ERROR: Failed to remove inv_mpu_iio_i2c_icm20602 module"
    exit 1
fi

rmmod inv_mpu_iio
if [[ $? != 0 ]]; then 
    log "ERROR: Failed to remove inv_mpu_iio module"
    exit 1
fi
sleep 1

log "revert inv-mpu-iio.ko"

log "Checking the inv-mpu-iio.ko file in backup"
if [[ -f ${BACKUP_DIR}inv-mpu-iio.ko ]]; then
    log "inv-mpu-iio.ko found in backup, reverting..."
    sudo cp -f ${BACKUP_DIR}inv-mpu-iio.ko ${DEST_DIR}
    if [[ $? != 0 ]]; then 
        log "ERROR: Failed to copy inv-mpu-iio.ko from backup to destination directory"
        exit 1
    fi
    sync ${DEST_DIR}inv-mpu-iio.ko
    log "Successfully reverted inv-mpu-iio.ko"
else
    log "backup file for inv-mpu-iio.ko is not found. So skipping the revert."
fi

log "revert inv-mpu-iio-i2c-icm20602.ko"

log "Checking the inv-mpu-iio-i2c-icm20602.ko file in backup"
if [[ -f ${BACKUP_DIR}inv-mpu-iio-i2c-icm20602.ko ]]; then
    log "inv-mpu-iio-i2c-icm20602.ko found in backup, reverting..."
    sudo cp -f ${BACKUP_DIR}inv-mpu-iio-i2c-icm20602.ko ${DEST_DIR}
    if [[ $? != 0 ]]; then 
        log "ERROR: Failed to copy inv-mpu-iio-i2c-icm20602.ko from backup to destination directory"
        exit 1
    fi
    sync ${DEST_DIR}inv-mpu-iio-i2c-icm20602.ko
    log "Successfully reverted inv-mpu-iio-i2c-icm20602.ko"
else
    log "backup file for inv-mpu-iio-i2c-icm20602.ko is not found. So skipping the revert."
fi

log "Loading reverted inv-mpu-iio base module..."
modprobe inv_mpu_iio
if [[ $? != 0 ]]; then 
    log "ERROR: Failed to load inv_mpu_iio module"
    exit 1
fi

log "Check if the inv-mpu-iio module is loaded or not"
if lsmod | grep -q 'inv_mpu_iio'; then
    log "Module inv_mpu_iio loaded successfully"
else
    log "Failed to load inv_mpu_iio module after revert"
    exit 1
fi

log "Loading reverted inv-mpu-iio-i2c-icm20602 dependent module..."
modprobe inv_mpu_iio_i2c_icm20602
if [[ $? != 0 ]]; then 
    log "ERROR: Failed to load inv_mpu_iio_i2c_icm20602 module"
    exit 1
fi

log "Check if the inv-mpu-iio-i2c-icm20602 module is loaded or not"
if lsmod | grep -q 'inv_mpu_iio_i2c_icm20602'; then
    log "Module inv_mpu_iio_i2c_icm20602 loaded successfully"
    log "Revert operation completed successfully - both modules are now running the original/base version"
else
    log "Failed to load inv_mpu_iio_i2c_icm20602 module after revert"
    exit 1
fi
exit 0
