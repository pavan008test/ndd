#!/usr/bin/env bash 

set -e 

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "==============replace inv-mpu-iio drivers===================="

log "Checking the md5sum of driver files" 

DEST_DIR="/lib/modules/4.9.160-perf/kernel/drivers/iio/imu/inv_mpu_20602/"
BACKUP_DIR="/home/ubuntu/backup/IMU_KO/"
STATUS_FILE="/sys/bus/iio/devices/iio:device1/"

# Check if backup directory exists, create if not
if [[ ! -d ${BACKUP_DIR} ]]; then
    log "Backup directory ${BACKUP_DIR} does not exist, creating..."
    mkdir -p ${BACKUP_DIR}
else
    log "Backup directory ${BACKUP_DIR} already exists"
fi

md5sum_inv_mpu=$(md5sum inv-mpu-iio.ko | awk '{print $1}')
md5sum_inv_mpu_iio_i2c=$(md5sum inv-mpu-iio-i2c-icm20602.ko | awk '{print $1}')

if [[ ! -d ${DEST_DIR} ]]; then
    log "inv_mpu_20602 folder not exists,creating inv_mpu_20602 folder"
    mkdir -p ${DEST_DIR}
fi

log "Removing existing modules..."
rmmod inv_mpu_iio_i2c_icm20602
rmmod inv_mpu_iio
sleep 1

log "==============replace inv-mpu-iio.ko===================="

if [[ -f ${DEST_DIR}inv-mpu-iio.ko ]]; then
    if [[ $(md5sum ${DEST_DIR}inv-mpu-iio.ko 2>/dev/null | awk -F " " '{print $1}') != $md5sum_inv_mpu ]]; then
        log "md5sum not matched for inv-mpu-iio.ko, replacing..."
        copy_file -f "inv-mpu-iio.ko" -d ${DEST_DIR} -b ${BACKUP_DIR} -m $md5sum_inv_mpu -p 644 -o root:root
        log "copied inv-mpu-iio.ko at ${DEST_DIR}"
    else
        log "md5sum of kernel files inv-mpu-iio is same as expected. So skipping the replacement."
    fi
else
    log "No inv-mpu-iio.ko file present in location ${DEST_DIR}. so,copying..."
    copy_file -f "inv-mpu-iio.ko" -d ${DEST_DIR} -b ${BACKUP_DIR} -m $md5sum_inv_mpu -p 644 -o root:root
    log "Copied inv-mpu-iio.ko file"
fi

log "==============replace inv-mpu-iio-i2c-icm20602.ko===================="

if [[ -f ${DEST_DIR}inv-mpu-iio-i2c-icm20602.ko ]]; then
    if [[ $(md5sum ${DEST_DIR}inv-mpu-iio-i2c-icm20602.ko 2>/dev/null | awk -F " " '{print $1}') != $md5sum_inv_mpu_iio_i2c ]]; then
        log "md5sum not matched for inv-mpu-iio-i2c-icm20602.ko, replacing..."
        copy_file -f "inv-mpu-iio-i2c-icm20602.ko" -d ${DEST_DIR} -b ${BACKUP_DIR} -m $md5sum_inv_mpu_iio_i2c -p 644 -o root:root
        log "copied inv-mpu-iio-i2c-icm20602.ko at ${DEST_DIR}"
    else
        log "md5sum of kernel files inv-mpu-iio-i2c-icm20602 is same as expected. So skipping the replacement."
    fi
else
    log "No inv-mpu-iio-i2c-icm20602.ko file present in location ${DEST_DIR}. so,copying..."
    copy_file -f "inv-mpu-iio-i2c-icm20602.ko" -d ${DEST_DIR} -b ${BACKUP_DIR} -m $md5sum_inv_mpu_iio_i2c -p 644 -o root:root
    log "Copied inv-mpu-iio-i2c-icm20602.ko file"
fi

log "Loading inv-mpu-iio base module..."
modprobe inv_mpu_iio

log "Check if the inv-mpu-iio module is loaded or not"
if lsmod | grep -q 'inv_mpu_iio'; then
    log "Module inv_mpu_iio loaded successfully"
else
    log "Failed to load inv_mpu_iio module"
    exit 1
fi

log "Loading inv-mpu-iio-i2c-icm20602 dependent module..."
modprobe inv_mpu_iio_i2c_icm20602

log "Check if the inv-mpu-iio-i2c-icm20602 module is loaded or not"
if lsmod | grep -q 'inv_mpu_iio_i2c_icm20602'; then
    log "Module inv_mpu_iio_i2c_icm20602 loaded successfully"
    log "Both modules loaded successfully"
else
    log "Failed to load inv_mpu_iio_i2c_icm20602 module"
    exit 1
fi

log "============== Checking the wom_mode_x_status file to confirm drivers are working ===================="
if [[ -f ${STATUS_FILE}wom_mode_x_status ]]; then
    log "wom_mode_x_status file found - drivers updated and working successfully"
else
    log "wom_mode_x_status file not found - driver initialization failed"
    exit 1
fi

