#!/usr/bin/env bash

set -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#calling copy function from lib
log "============= revert dtb files =================="

checksum_dtb=$(md5sum /boot/tegra186-quill-p3489-0888-a00-00-base.dtb | awk -F " " '{print $1}')

if [[ $checksum_dtb == "75214678846c5252e8bfc9dc0a8b8293" ]];
then

log "Install old dtb deb file"
execute_command_sync apt-get install --reinstall ./nvidia-l4t-kernel-dtbs_4.9.140-tegra-32.4.3-recovery_arm64.deb
status1=$?

log "Reverting the backup extlinux_pri file to original location"
if [[ -f /boot/backup/extlinux/extlinux_pri.conf ]]; then
	log "Copying extlinux conf file to /boot/extlinux/"
	cp -fp /boot/backup/extlinux/extlinux_pri.conf /boot/extlinux/extlinux_pri.conf
	status2=$?
else
	log "No backup file for extlinux_pri.conf"
	checksum_ext=$(md5sum /boot/extlinux/extlinux_pri.conf | awk -F " " '{print $1}')
	if [[ $checksum_ext == "ed48b419850e9131782edbf68e6c15a2" ]]; then log "original file is present in its location" ; fi;
fi


if [[ $status1 == 0 && $status2 == 0 ]]; then 
  status=0
  check_status $(basename $(pwd)) $status
else
  status=1
  check_status $(basename $(pwd)) $status
fi

else

log "Correct dtb are in place. No revert is required"

fi

log "============= End of install dtb files ===================="

