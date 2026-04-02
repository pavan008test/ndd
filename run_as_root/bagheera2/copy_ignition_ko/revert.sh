#!/usr/bin/env bash 

set -e 

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#####revert gpio-ignition_ko

log "Checking the gpio-ignition.ko file in backup" 
if [[ -f /home/ubuntu/.nddevice/backup/gpio-ignition.ko  ]]; then
    log "gpio-ignition.ko found in backup, reverting..."
    sudo cp -f /home/ubuntu/.nddevice/backup/gpio-ignition.ko  /lib/modules/4.9.140-tegra/kernel/drivers/bag2_vendor/ignition/
    if [[ $? != 0 ]]; then exit 1; fi
    sync   /lib/modules/4.9.140-tegra/kernel/drivers/bag2_vendor/ignition/gpio-ignition.ko

else
    log "backup file for gpio-ignition.ko is not found. So skipping the revert. Exiting..."
    exit 0
fi

