#!/usr/bin/env bash
set -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh


log "==============Start of emmc_bootfix_update================="

sudo mkdir -p /boot/backup
manfid=$(cat /sys/class/mmc_host/mmc0/mmc0:0001/manfid)
oemid=$(cat /sys/class/mmc_host/mmc0/mmc0:0001/oemid)
name=$(cat /sys/class/mmc_host/mmc0/mmc0:0001/name)

log "Checking EMMC IDs for device"
if [[ $manfid == "0x000045" && $oemid == "0x0100" && $name == "DG4016" ]]
then
	#All these hardcoded md5sum are the target files md5sum.
	files_to_copy=()
	log "Checking the md5sum of existing bootfiles with the target files" 
	if [[ $(md5sum /boot/tegra210-jetson-cv-camera-li-mipi-adpt-a01-devkit.dtb | awk -F " " '{print $1}') != 5a911988c1fefd259ed37deea04019e4 ]]; then
		files_to_copy+=('tegra210-jetson-cv-camera-li-mipi-adpt-a01-devkit.dtb')
		if [[ $(md5sum /boot/tegra210-jetson-cv-camera-li-mipi-adpt-a01-devkit.dtb | awk -F " " '{print $1}') == e165f983b3e1f5d5203aaf474705ea78 ]]; then
			log "Backup old tegra210-jetson-cv-camera-li-mipi-adpt-a01-devkit.dtb to /boot/backup"
			sudo cp /boot/tegra210-jetson-cv-camera-li-mipi-adpt-a01-devkit.dtb /boot/backup/
			sync /boot/backup/tegra210-jetson-cv-camera-li-mipi-adpt-a01-devkit.dtb
		fi
	fi
	if [[ $(md5sum /boot/tegra210-jetson-tx1-p2597-2180-a01-devkit.dtb | awk -F " " '{print $1}') != 9f8a7f95e4705dbf792cd6dbea835e90 ]]; then
		files_to_copy+=('tegra210-jetson-tx1-p2597-2180-a01-devkit.dtb')
		if [[ $(md5sum /boot/tegra210-jetson-tx1-p2597-2180-a01-devkit.dtb | awk -F " " '{print $1}') == 6cb3e5706958725bb2ea87f2d7ca362e ]]; then
			log "Backup old tegra210-jetson-tx1-p2597-2180-a01-devkit.dtb to /boot/backup"
			sudo cp /boot/tegra210-jetson-tx1-p2597-2180-a01-devkit.dtb /boot/backup/
			sync /boot/backup/tegra210-jetson-tx1-p2597-2180-a01-devkit.dtb
		fi
	fi
	if [[ ${#files_to_copy[@]} == 0 ]]; then
		log "All kernel files are same as expected md5sum. So skipping copy. Exiting...."
		exit 0
	fi

	log "Changing permissions to 644 for all files"
	sudo chmod 644 bootfix/*
	log "Changing Onwership to root for all files"
	sudo chown root:root bootfix/*
	for file in ${files_to_copy[@]} ;do
		log "------ Copying and replacing the $file in /boot/ with md5sum - $(md5sum /boot/$file | awk -F " " '{print $1}')"
		sudo cp -f bootfix/$file /boot/
		if [[ $? != 0 ]]; then exit 1; fi
		sync /boot/$file
	done

	log "------ Verifying the copied binaries @ /boot/"
	if [ $(md5sum /boot/tegra210-jetson-tx1-p2597-2180-a01-devkit.dtb | awk -F " " '{print $1}') == 9f8a7f95e4705dbf792cd6dbea835e90 -a $(md5sum /boot/tegra210-jetson-cv-camera-li-mipi-adpt-a01-devkit.dtb | awk -F " " '{print $1}') == 5a911988c1fefd259ed37deea04019e4 ]
	then
		log "------ /boot/tegra210-jetson-tx1-p2597-2180-a01-devkit.dtb is copied to /boot/ and md5sum - $(md5sum /boot/tegra210-jetson-tx1-p2597-2180-a01-devkit.dtb | awk -F " " '{print $1}')"
		log "------ /boot/tegra210-jetson-cv-camera-li-mipi-adpt-a01-devkit.dtb is copied to /boot/ and md5sum - $(md5sum /boot/tegra210-jetson-cv-camera-li-mipi-adpt-a01-devkit.dtb | awk -F " " '{print $1}')"
	else
		log "$(md5sum /boot/tegra210-jetson-tx1-p2597-2180-a01-devkit.dtb) or $(md5sum /boot/tegra210-jetson-cv-camera-li-mipi-adpt-a01-devkit.dtb) , file not copied properly to /boot/"
		exit 1
	fi

	#explicite copy for dtb folder
	log "----- Copying tegra210-jetson-cv-camera-li-mipi-adpt-a01-devkit.dtb to /boot/dtb/ ----"
	sudo cp -f bootfix/tegra210-jetson-cv-camera-li-mipi-adpt-a01-devkit.dtb /boot/dtb/
	if [[ $? != 0 ]]; then exit 1; fi
	sync /boot/dtb/tegra210-jetson-cv-camera-li-mipi-adpt-a01-devkit.dtb
else
	log "EMMC IDs is not matching. Probably the device does not have Sandisk emmc. Skipping the KO copy changes"
fi

log "==============End of emmc_bootfix_update================="
