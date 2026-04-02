#!/usr/bin/env bash

set -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "============ Start of copy_ko-nd_pmic_wdt_vfs ==========="

log "Removing existing file or soft link for nd_pmic_wdt_vfs.ko from /lib/modules/4.9.140-tegra/kernel/drivers/misc/ "

log "Creating a soft link to activate nd_pmic_wdt_vfs from kernal"
status=$(execute_command_sync ln -sf /home/ubuntu/.nddevice/latest/nd_kernel_modules/nd_pmic_wdt_vfs.ko /lib/modules/4.9.140-tegra/kernel/drivers/misc/nd_pmic_wdt_vfs.ko)$?
check_status $(basename $(pwd)) $status
status=$(execute_command_sync ln -sf /home/ubuntu/.nddevice/latest/nd_kernel_modules/nd.conf /etc/modules-load.d/nd.conf)$?
check_status $(basename $(pwd)) $status

if [[ ! -f /home/ubuntu/.nddevice/latest/nd_kernel_modules/nd_pmic_wdt_vfs.ko ]];
then
	mkdir -p /home/ubuntu/.nddevice/latest/nd_kernel_modules/
	updating_version=$(grep -A1 upgrade "/home/ubuntu/.nddevice/nddevice.ini" | awk -F "=" '{print $2}' | tail -1 | tr -d " ")
	cp /home/ubuntu/.nddevice/$updating_version/nd_kernel_modules/nd_pmic_wdt_vfs.ko /home/ubuntu/.nddevice/latest/nd_kernel_modules/
	execute_command_sync depmod -a
	rm -f /home/ubuntu/.nddevice/latest/nd_kernel_modules/nd_pmic_wdt_vfs.ko
	if [[ -z $(ls /home/ubuntu/.nddevice/latest/nd_kernel_modules/) ]]; then
		rm -rf /home/ubuntu/.nddevice/latest/nd_kernel_modules/
	fi
else
	execute_command_sync depmod -a
fi

log "============ End of copy_ko-nd_pmic_wdt_vfs ==========="
