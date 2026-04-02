#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "============ Start of revert copy_ko-nd_pmic_wdt_vfs ==========="
current_ota=$(cat /home/ubuntu/.nddevice/nddevice.ini | grep -a1 version | grep nddevice | awk -F "=" '{print $2}' | tr -d ' ' | cut -d "." -f 1-3)
ota_list=(0.5.3 0.5.4 0.5.5)
for version in ${ota_list[@]}
do
    if [[ $current_ota == $version ]]
    then
        log "This revert task is not required for this current OTA $current_ota"
      exit 0
    fi
done

log "Removing symlink for nd_pmic_wdt_vfs from /lib/modules/4.9.140-tegra/kernel/drivers/misc/"
sudo rm  /lib/modules/4.9.140-tegra/kernel/drivers/misc/nd_pmic_wdt_vfs.ko
sudo sed -i "/nd_pmic_wdt_vfs/d" $(readlink -f /etc/modules-load.d/nd.conf)
execute_command depmod -a
log "============ End of revert copy_ko-nd_pmic_wdt_vfs ==========="
