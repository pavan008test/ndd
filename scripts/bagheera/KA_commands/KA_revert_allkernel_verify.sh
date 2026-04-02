#!/usr/bin/env bash

if [[ $(cat /sys/class/mmc_host/mmc0/mmc0:0001/manfid) == "0x000045" && $(cat /sys/class/mmc_host/mmc0/mmc0:0001/oemid) == "0x0100" && $(cat /sys/class/mmc_host/mmc0/mmc0:0001/name) == "DG4016" ]]
then
	echo "Checking the md5sum of backup bootfiles with the original md5sum"
	if [[ $(md5sum /boot/backup/Image | awk -F " " '{print $1}') == b36b799f80b4051b4656820045630518 && $(md5sum /boot/backup/tegra210-jetson-cv-camera-li-mipi-adpt-a01-devkit.dtb | awk -F " " '{print $1}') == e165f983b3e1f5d5203aaf474705ea78 && $(md5sum /boot/backup/tegra210-jetson-tx1-p2597-2180-a01-devkit.dtb | awk -F " " '{print $1}') == 6cb3e5706958725bb2ea87f2d7ca362e && $(md5sum /boot/backup/zImage | awk -F " " '{print $1}') == 0acfee250ece6aad2dc5ecef41d39e87 ]]; then
		echo "Moving files from backup to /boot"
		mv /boot/backup/Image /boot/
		sync /boot/Image
		mv /boot/backup/tegra210-jetson-cv-camera-li-mipi-adpt-a01-devkit.dtb /boot/
		sync /boot/tegra210-jetson-cv-camera-li-mipi-adpt-a01-devkit.dtb
		mv /boot/backup/tegra210-jetson-tx1-p2597-2180-a01-devkit.dtb /boot/
		sync /boot/tegra210-jetson-tx1-p2597-2180-a01-devkit.dtb
		mv /boot/backup/zImage /boot/
		sync /boot/zImage
		cp -f /boot/tegra210-jetson-cv-camera-li-mipi-adpt-a01-devkit.dtb /boot/dtb/
		sync /boot/dtb/tegra210-jetson-cv-camera-li-mipi-adpt-a01-devkit.dtb
		echo "Move completed"
		sync
		echo "Verifying copied bins"
		if [[ $(md5sum /boot/Image | awk -F " " '{print $1}') == b36b799f80b4051b4656820045630518 && $(md5sum /boot/tegra210-jetson-cv-camera-li-mipi-adpt-a01-devkit.dtb | awk -F " " '{print $1}') == e165f983b3e1f5d5203aaf474705ea78 && $(md5sum /boot/tegra210-jetson-tx1-p2597-2180-a01-devkit.dtb | awk -F " " '{print $1}') == 6cb3e5706958725bb2ea87f2d7ca362e && $(md5sum /boot/zImage | awk -F " " '{print $1}') == 0acfee250ece6aad2dc5ecef41d39e87 ]]
			then
			echo "------ /boot/Image is copied to /boot/ and md5sum - $(md5sum /boot/Image | awk -F " " '{print $1}')"
			echo "------ /boot/tegra210-jetson-tx1-p2597-2180-a01-devkit.dtb is copied to /boot/ and md5sum - $(md5sum /boot/tegra210-jetson-tx1-p2597-2180-a01-devkit.dtb | awk -F " " '{print $1}')"
			echo "------ /boot/tegra210-jetson-cv-camera-li-mipi-adpt-a01-devkit.dtb is copied to /boot/ and md5sum - $(md5sum /boot/tegra210-jetson-cv-camera-li-mipi-adpt-a01-devkit.dtb | awk -F " " '{print $1}')"
			echo "------ /boot/zImage is copied to /boot/ and md5sum - $(md5sum /boot/zImage | awk -F " " '{print $1}')"
		else
			echo "file not copied properly to /boot/"
			exit 1
		fi
	else
		echo "Md5sum is not matching with backup files or file may not be present in backup"
		exit 1
	fi
else
	echo "Not supported for non sandisk emmc devices"
fi
