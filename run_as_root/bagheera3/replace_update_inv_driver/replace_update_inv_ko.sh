#!/usr/bin/env bash 

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

DEST_DIR="/lib/modules/4.9.299-tegra/kernel/drivers/iio/imu/inv_mpu/inv_mpu_43600/"
BACKUP_DIR="/home/ubuntu/backup/run_as_root/IMU_KO/"
STATUS_FILE="/sys/bus/iio/devices/iio:device1/"

remove_module_if_loaded() {
    module_name="$1"
    if lsmod | grep -q "$module_name"; then
        if ! rmmod "$module_name"; then
            log "ERROR: rmmod failed for $module_name module."
        fi
        log "Found and removed $module_name module."
        sleep 1
        log "Sleeping for 1 sec after removing $module_name module"
    else
        log "$module_name module not found. Skipping removal."
    fi
}

load_modules_if_not_loaded() {
    module_name="$1"
    if lsmod | grep -q "$module_name"; then
        log "$module_name module already loaded. Skipping modprobe."
    else
        if ! modprobe "$module_name"; then
            log "modprobe failed for $module_name, trying insmod."
            local ko_file="${DEST_DIR}${module_name//_/-}.ko"
            log "Attempting insmod with ko_file: $ko_file"
            if insmod "$ko_file"; then
                log "$module_name module loaded with insmod."
            else
                log "insmod failed for $module_name."
            fi
        else
            log "$module_name module loaded."
        fi
    fi
}

copy_and_load() {
    local filepath="$1"
    local filename=$(basename "$filepath")
    local md5sum_val=$(md5sum "$filepath" | awk '{print $1}')

    log "MD5SUM of source ($filename): $md5sum_val"

    # Check if file exists and MD5 matches
    if [[ -f "${DEST_DIR}${filename%.ko_rar}.ko" ]]; then
        local dest_md5sum=$(md5sum "${DEST_DIR}${filename%.ko_rar}.ko" | awk '{print $1}')
        log "MD5SUM of destination (${filename%.ko_rar}.ko): $dest_md5sum"
        
        if [[ "$md5sum_val" == "$dest_md5sum" ]]; then
            log "File $filename already up to date. Skipping copy and load operations."
            return 0
        fi
        log "MD5 mismatch detected for $filename. Destination needs to be updated."
        # Rename existing destination file by appending .ko_rar
        log "Renaming existing destination file to backup with .ko_rar extension"
        mv "${DEST_DIR}${filename%.ko_rar}.ko" "${DEST_DIR}${filename%.ko_rar}.ko_rar"
        log "Renamed ${filename%.ko_rar}.ko to ${filename%.ko_rar}.ko_rar"


    else
        log "Destination file $filename does not exist. Will proceed with copy."
    fi

    log "Proceeding with module replacement for $filename"
    remove_module_if_loaded "inv_mpu_iio_i2c"
    remove_module_if_loaded "inv_mpu_iio"

    copy_file -f "$filepath" -d "${DEST_DIR}" -b "${BACKUP_DIR}" -m "$md5sum_val" -p 644 -o root:root
    
    if [[ -f "${DEST_DIR}${filename}" ]]; then
        local ko_filename="${filename%.ko_rar}.ko"
        log "Renaming the copied file ${filename} to ${ko_filename}"
        mv "${DEST_DIR}${filename}" "${DEST_DIR}${ko_filename}"
        log "Copied KO file info : $(ls -l "${DEST_DIR}${ko_filename}")"
        if [[ -f "${DEST_DIR}${ko_filename}" ]]; then
            log "File copy and rename successful for ${filename} to ${ko_filename}"
        else
            log "ERROR: Renaming failed for ${filename} to ${ko_filename}"
            exit 1
        fi
        log "Renamed destination ${filename} to ${ko_filename}"
    else
        log "ERROR: Destination file ${filename} not found after copy operation" 
        exit 1
    fi

    # Rename backup file if it exists
    local backup_ko_rar="${BACKUP_DIR}${filename}"
    local backup_ko="${BACKUP_DIR}${filename%.ko_rar}.ko"    
    
    log "Renamed backup ${filename%.ko_rar}.ko to ${filename%.ko_rar}.ko_org"

    
    if [[ -f "${backup_ko_rar}" ]]; then
        mv "${backup_ko_rar}" "${BACKUP_DIR}${filename%.ko_rar}.ko_org"
        log "Renamed backup ${filename} to ${filename%.ko_rar}.ko_org"
    else
        log "No backup file found to rename for ${filename}" 
    fi
}

CURRENT_OS_VERSION=$(grep "nd_os" /etc/nd_os_ver.ini | cut -d'=' -f2 | tr -d '[:space:]')
log "Current OS Version: $CURRENT_OS_VERSION"


if [[ "$CURRENT_OS_VERSION" == "13.0.19" || "$CURRENT_OS_VERSION" == "13.0.1A" ]]; then
    log "OS version is 13.0.19 or 13.0.1A. so copying from old_os ko files."
    copy_and_load "old_os/inv-mpu-iio-i2c.ko_rar"
    copy_and_load "old_os/inv-mpu-iio.ko_rar"
else
    log "OS version is not 13.0.19 or 13.0.1A, Probably latest version. Proceeding with coping ko from new_os folder."
    copy_and_load "new_os/inv-mpu-iio-i2c.ko_rar"
    copy_and_load "new_os/inv-mpu-iio.ko_rar"
fi

load_modules_if_not_loaded "inv_mpu_iio_i2c"
load_modules_if_not_loaded "inv_mpu_iio"

if [[ -f ${STATUS_FILE}wom_x_status ]]; then    
    log "wom_x_status file found - drivers updated and working successfully"
else
    log "wom_x_status file not found - driver initialization failed"
fi
