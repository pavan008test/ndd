#!/usr/bin/env bash
set -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh


log "==============Start of revert emmc_bootfix_update================="

if [[ -f /boot/backup/Image ]]; then
    log "Copying older Image to /boot/ "
    sudo cp -f /boot/backup/Image /boot/
	if [[ $? != 0 ]]; then exit 1; fi
	sync /boot/Image
fi

if [[ -f /boot/backup/tegra210-jetson-cv-camera-li-mipi-adpt-a01-devkit.dtb  ]]; then
    log "Copying older tegra210-jetson-cv-camera-li-mipi-adpt-a01-devkit.dtb  to /boot/ "
    sudo cp -f /boot/backup/tegra210-jetson-cv-camera-li-mipi-adpt-a01-devkit.dtb  /boot/
	if [[ $? != 0 ]]; then exit 1; fi
	sync /boot/tegra210-jetson-cv-camera-li-mipi-adpt-a01-devkit.dtb 
fi

if [[ -f /boot/backup/tegra210-jetson-cv-camera-li-mipi-adpt-a01-devkit.dtb  ]]; then
    log "Copying older tegra210-jetson-cv-camera-li-mipi-adpt-a01-devkit.dtb  to /boot/dtb/ "
    sudo cp -f /boot/backup/tegra210-jetson-cv-camera-li-mipi-adpt-a01-devkit.dtb  /boot/dtb/
	if [[ $? != 0 ]]; then exit 1; fi
	sync /boot/dtb/tegra210-jetson-cv-camera-li-mipi-adpt-a01-devkit.dtb 
fi

if [[ -f /boot/backup/tegra210-jetson-tx1-p2597-2180-a01-devkit.dtb ]]; then
    log "Copying older tegra210-jetson-tx1-p2597-2180-a01-devkit.dtb to /boot/ "
    sudo cp -f /boot/backup/tegra210-jetson-tx1-p2597-2180-a01-devkit.dtb /boot/
	if [[ $? != 0 ]]; then exit 1; fi
	sync /boot/tegra210-jetson-tx1-p2597-2180-a01-devkit.dtb
fi

if [[ -f /boot/backup/zImage ]]; then
    log "Copying older zImage to /boot/ "
    sudo cp -f /boot/backup/zImage /boot/
	if [[ $? != 0 ]]; then exit 1; fi
	sync /boot/zImage
fi

log "==============End of revert emmc_bootfix_update================="
