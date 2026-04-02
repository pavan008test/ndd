#!/usr/bin/env bash

set -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#script for invoking the tegra_update
log "================START OF tegra_update SCRIPT==============="

#####ADDing  AN UMOUNT OF SDCARD /dev/mmcblk1p1, experiment with banging SDCARD with data and unmounting at the same time

log "Unmounting the sdcard"
if [ -n "`lsblk |grep "/media/"`" ]
then
for i in {1..10}
do
sudo umount /dev/mmcblk1p1
if [ $? = 0 ]
then
log "Unmounting the sdcard successful" 
break
else 
log "Unmounting not successful"
sleep 5
continue
fi
done
fi


log "Creating the folder /boot/dtb/208mhz"
sudo mkdir -p /boot/dtb/208mhz

log "Taking backup for old file"
sudo cp -f --preserve /boot/tegra210-jetson-cv-camera-li-mipi-adpt-a01-devkit.dtb /boot/dtb/208mhz/tegra210-jetson-cv-camera-li-mipi-adpt-a01-devkit.dtb
sync /boot/dtb/208mhz/tegra210-jetson-cv-camera-li-mipi-adpt-a01-devkit.dtb

log "Changing tegrafile permissions to rw-r-r--"
chmod 644 /home/ubuntu/.nddevice/ota_temp/run_as_root/tegra_kernel_update/tegra210-jetson-cv-camera-li-mipi-adpt-a01-devkit.dtb

log "Changing tegrafile ownership to root "
sudo chown root:root /home/ubuntu/.nddevice/ota_temp/run_as_root/tegra_kernel_update/tegra210-jetson-cv-camera-li-mipi-adpt-a01-devkit.dtb

log "Replacing the old tegra file"
sync 
sudo mv -f /home/ubuntu/.nddevice/ota_temp/run_as_root/tegra_kernel_update/tegra210-jetson-cv-camera-li-mipi-adpt-a01-devkit.dtb /boot/tegra210-jetson-cv-camera-li-mipi-adpt-a01-devkit.dtb
sync /boot/tegra210-jetson-cv-camera-li-mipi-adpt-a01-devkit.dtb

log "Checking the md5sum of new tegrafile"
mdsum=`md5sum /boot/tegra210-jetson-cv-camera-li-mipi-adpt-a01-devkit.dtb |awk '{print $1}'`
log "md5sum : $mdsum"

if [ $mdsum != "e165f983b3e1f5d5203aaf474705ea78" ]
then
	log "mdsum not matched. Rolling back the changes"
	log "replacing back old file"
	sudo cp -f --preserve /boot/dtb/208mhz/tegra210-jetson-cv-camera-li-mipi-adpt-a01-devkit.dtb /boot/tegra210-jetson-cv-camera-li-mipi-adpt-a01-devkit.dtb 
	sync /boot/tegra210-jetson-cv-camera-li-mipi-adpt-a01-devkit.dtb
	log "Exiting the tegra_update with status 1"
	exit 1

else
	log "tegra_update is successful"

fi
log "=============END OF tegra_update SCRIPT============="

