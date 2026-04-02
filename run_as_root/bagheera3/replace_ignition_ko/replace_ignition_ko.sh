#!/usr/bin/env bash 

set -e 

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#####replace gpio-ignition_ko
DEST_DIR="/lib/modules/4.9.299-tegra/kernel/drivers/bag2_vendor/ignition/"

if [[ ! -d /lib/modules/4.9.299-tegra/kernel/drivers/bag2_vendor/ignition/ ]]; then
	log "ignition folder not exists,creating ignition folder"
	mkdir -p /lib/modules/4.9.299-tegra/kernel/drivers/bag2_vendor/ignition/
fi

log "Checking the md5sum of gpio-ignition.ko files" 

if [[ -f /lib/modules/4.9.299-tegra/kernel/drivers/bag2_vendor/ignition/gpio-ignition.ko ]]; then
    if [[ $(md5sum /lib/modules/4.9.299-tegra/kernel/drivers/bag2_vendor/ignition/gpio-ignition.ko 2>/dev/null | awk -F " " '{print $1}') != 9efdfd6961c8c1f2aa6d3fae7cb51696 ]]; then
        log "md5sum not matched for gpio-ignition.ko, replacing..."
        copy_file -f "gpio-ignition.ko" -d ${DEST_DIR} -b /home/ubuntu/.nddevice/backup/ -m 9efdfd6961c8c1f2aa6d3fae7cb51696 -p 644 -o root:root
        log "copied gpio-ignition.ko at /lib/modules/4.9.299-tegra/kernel/drivers/bag2_vendor/ignition/"
    else
        log "md5sum of kernel files gpio-ignition is same as expected. So skipping the replacement. Exiting..."
    fi
else
    log "No ignition ko file present in location /lib/modules/4.9.299-tegra/kernel/drivers/bag2_vendor/ignition/. So copying it...."
    copy_file -f "gpio-ignition.ko" -d ${DEST_DIR} -b /home/ubuntu/.nddevice/backup/ -m 9efdfd6961c8c1f2aa6d3fae7cb51696 -p 644 -o root:root
    log "Copied gpio-ignition.ko file"
fi

rmmod gpio-ignition
sleep 1
insmod /lib/modules/4.9.299-tegra/kernel/drivers/bag2_vendor/ignition/gpio-ignition.ko

log "Check if the module is loaded or not"
if lsmod | grep -q 'gpio_ignition'; then
    log "Module gpio_ignition loaded successfully"
else
    log "Failed to load gpio_ignition module"
    exit 1
fi
