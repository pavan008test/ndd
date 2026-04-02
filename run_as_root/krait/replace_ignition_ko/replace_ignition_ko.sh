#!/usr/bin/env bash 

set -e 

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#####replace gpio-ignition_ko

log "Checking the md5sum of gpio-ignition.ko files" 

DEST_DIR="/usr/lib/modules/4.9.160-perf/kernel/drivers/gpio/"

if [[ ! -d /usr/lib/modules/4.9.160-perf/kernel/drivers/gpio/ ]]; then
	log "ignition folder not exists,creating ignition folder"
	mkdir -p /usr/lib/modules/4.9.160-perf/kernel/drivers/gpio/
fi

if [[ -f /usr/lib/modules/4.9.160-perf/kernel/drivers/gpio/gpio-ignition.ko ]]; then
    if [[ $(md5sum /usr/lib/modules/4.9.160-perf/kernel/drivers/gpio/gpio-ignition.ko 2>/dev/null | awk -F " " '{print $1}') != 53e87a71a3dd6f9d04e15b34360493fe ]]; then
        log "md5sum not matched for gpio-ignition.ko, replacing..."
        copy_file -f "gpio-ignition.ko" -d ${DEST_DIR} -b /home/ubuntu/.nddevice/backup/ -m 53e87a71a3dd6f9d04e15b34360493fe -p 644 -o root:root
        log "copied gpio-ignition.ko at /usr/lib/modules/4.9.160-perf/kernel/drivers/gpio/"
    else
        log "md5sum of kernel files gpio-ignition is same as expected. So skipping the replacement. Exiting..."
    fi
else
    log "No ignition ko file present in location /usr/lib/modules/4.9.160-perf/kernel/drivers/gpio/. so,copying..."
    copy_file -f "gpio-ignition.ko" -d ${DEST_DIR} -b /home/ubuntu/.nddevice/backup/ -m 53e87a71a3dd6f9d04e15b34360493fe -p 644 -o root:root
    log "Copied gpio-ignition.ko file"
fi

rmmod gpio-ignition
sleep 1
insmod /usr/lib/modules/4.9.160-perf/kernel/drivers/gpio/gpio-ignition.ko

log "Check if the module is loaded or not"
if lsmod | grep -q 'gpio_ignition'; then
    log "Module gpio_ignition loaded successfully"
else
    log "Failed to load gpio_ignition module"
    exit 1
fi
