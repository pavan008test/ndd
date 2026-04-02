#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh
set -e

#Calling function from lib to replace/update
kernel_folder=/lib/modules/misc
kernel_backup=${kernel_folder}/backup/
skip_count=0

sudo mkdir -p ${kernel_backup}
tar -xzf wifi_no_sleep_kos.tar.gz

kernel_files=(brcmfmac.ko brcmutil.ko cfg80211.ko compat.ko)
#Respective md5sum of the above kernel file in same order
resp_md5sum=("cbcfd733637b805d52a4bf976061a40b" "d53ab18881f1fe2ed551a6098ca23175" "c8efc9a361b6ffd9828ceffd5e9c593a" "56bfea845682d8c4b75d76503073742f")

for (( i=0; i<${#kernel_files[@]}; i++ )); do 

	if [[ $(md5sum ${kernel_folder}/${kernel_files[$i]} | awk '{print $1}') != ${resp_md5sum[$i]} ]]; then
		log "Copying the ${kernel_files[$i]} as a backup file into folder ${kernel_backup}"
		execute_command_sync cp -f ${kernel_folder}/${kernel_files[$i]} ${kernel_backup}
		CHECKSUM=$(md5sum ${kernel_files[$i]} | awk -F " " '{print $1}')
		copy_file -f ${kernel_files[$i]} -d ${kernel_folder}/ -m ${CHECKSUM} -p 644 -o root:root
	else
		skip_count=$(($skip_count+1))
		log "New ${kernel_files[$i]} file md5sum is matching with the existing ko file. No need to copy. Skipping...."
	fi

done

if [[ $skip_count != 4 ]]; then 
	#Remove old modules
	rmmod brcmfmac brcmutil cfg80211 compat
	sleep 1

	#Install new modules
	insmod /lib/modules/misc/compat.ko
	insmod /lib/modules/misc/cfg80211.ko
	insmod /lib/modules/misc/brcmutil.ko
	insmod /lib/modules/misc/brcmfmac.ko debug=0x100000

	sleep 2

	#Toggle the device for interface to come up
	echo -n "3440000.sdhci" > /sys/bus/platform/drivers/sdhci-tegra/unbind
	sleep 2
	echo -n "3440000.sdhci" > /sys/bus/platform/drivers/sdhci-tegra/bind
fi
