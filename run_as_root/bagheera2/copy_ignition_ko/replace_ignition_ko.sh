#!/usr/bin/env bash 

set -e 

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#####replace gpio-ignition_ko

log "Checking the md5sum of gpio-ignition.ko files" 

if [[ ! -d /lib/modules/4.9.140-tegra/kernel/drivers/bag2_vendor/ignition/ ]]; then
	log "ignition folder not exists,creating ignition folder"
	mkdir -p /lib/modules/4.9.140-tegra/kernel/drivers/bag2_vendor/ignition/
fi

if [[ -f /lib/modules/4.9.140-tegra/kernel/drivers/bag2_vendor/ignition/gpio-ignition.ko ]]; then
    if [[ $(md5sum /lib/modules/4.9.140-tegra/kernel/drivers/bag2_vendor/ignition/gpio-ignition.ko 2>/dev/null | awk -F " " '{print $1}') != 2fb64901a07692836780214e5635cbee ]]; then
        log "md5sum not matched for gpio-ignition.ko, replacing..."
        DEST_DIR="/lib/modules/4.9.140-tegra/kernel/drivers/bag2_vendor/ignition/"
        copy_file -f "gpio-ignition.ko" -d ${DEST_DIR} -b /home/ubuntu/.nddevice/backup/ -m 2fb64901a07692836780214e5635cbee -p 644 -o root:root
        log "copied gpio-ignition.ko at /lib/modules/4.9.140-tegra/kernel/drivers/bag2_vendor/ignition/"
    else
        log "md5sum of kernel files gpio-ignition is same as expected. So skipping the replacement. Exiting..."
    fi
else
	log "No ignition ko file present in location /lib/modules/4.9.140-tegra/kernel/drivers/bag2_vendor/ignition/. copying...."
    	DEST_DIR="/lib/modules/4.9.140-tegra/kernel/drivers/bag2_vendor/ignition/"
    	copy_file -f "gpio-ignition.ko" -d ${DEST_DIR} -b /home/ubuntu/.nddevice/backup/ -m 2fb64901a07692836780214e5635cbee -p 644 -o root:root
    	log "copied gpio-ignition.ko at /lib/modules/4.9.140-tegra/kernel/drivers/bag2_vendor/ignition/"
fi

log "Checking if the gpio-ignition.ko module is loaded"
if lsmod | grep -q 'gpio_ignition'; then
    log "Module gpio_ignition is already loaded, removing it to load it again"
    rmmod gpio-ignition
    sleep 1
fi

log "Executing depmod to refresh the list of kernel modules"
depmod -a
sleep 1
log "Loading gpio_ignition module"
insmod /lib/modules/4.9.140-tegra/kernel/drivers/bag2_vendor/ignition/gpio-ignition.ko
log "Module gpio_ignition loaded"

log "Check if the module is loaded or not"
if lsmod | grep -q 'gpio_ignition'; then  
    log "Module gpio_ignition loaded successfully"  
else  
    log "Failed to load gpio_ignition module"  
    exit 1  
fi  

log "Checking if gpio_ignition is in /etc/modules-load.d/vendor.conf"
if ! grep -q 'gpio_ignition' /etc/modules-load.d/vendor.conf; then
    log "gpio_ignition is not present, appending it now"
    echo 'gpio_ignition' >> /etc/modules-load.d/vendor.conf
else
    log "gpio_ignition is already present"
fi
