#!/usr/bin/env bash

set -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#calling copy function from lib
log "============= Install dtb files =================="

##Below are getting checksum and comparing with original md5sum from OS12.3.1 hardcoded in ifcondition
checksum_boot=$(md5sum /boot/tegra186-quill-p3489-0888-a00-00-base.dtb | awk -F " " '{print $1}')
checksum_boot_rec=$(md5sum /boot/tegra186-quill-p3489-0888-a00-00-base.dtb.rec | awk -F " " '{print $1}')
checksum_dtb=$(md5sum /boot/dtb/tegra186-quill-p3489-0888-a00-00-base.dtb | awk -F " " '{print $1}')

## 405e3d0e1a55b9aac68c9314eba4e4b4 == 12.3.2 OS ; 75214678846c5252e8bfc9dc0a8b8293 == dtb ota update
if [[ $checksum_boot == "405e3d0e1a55b9aac68c9314eba4e4b4" || $checksum_boot == "75214678846c5252e8bfc9dc0a8b8293" ]]; 
then

log "dtb is already latest through OTA or OS. So skipping update"
check_status $(basename $(pwd)) 0

else

log "Taking backup of existing dtb files by checking the md5sum of original md5sum"
mkdir -p /boot/backup/boot/dtb 
if [[ $checksum_boot == "89e76588c61bec145869a36618c1f0f7" ]]; then cp -p /boot/tegra186-quill-p3489-0888-a00-00-base.dtb /boot/backup/boot/; fi
if [[ $checksum_boot_rec == "54fe6ddea838a900d785feff8eef52a8" ]]; then cp -p /boot/tegra186-quill-p3489-0888-a00-00-base.dtb.rec /boot/backup/boot/; fi
if [[ $checksum_dtb == "ebe7289506caf0815d7ac2ef79fd0c92" ]]; then cp -p /boot/dtb/tegra186-quill-p3489-0888-a00-00-base.dtb /boot/backup/boot/dtb/; fi

log "Install new dtb deb file"
execute_command_sync apt-get install --reinstall ./nvidia-l4t-kernel-dtbs_4.9.140-tegra-32.4.3-20200625213407_arm64.deb
status1=$?

log "Taking the backup of extlinux_pri.conf file"
#Hardcoded check is the original with OS 12.3.1 and checking it with existing file.
mkdir -p /boot/backup/extlinux
checksum_ext=$(md5sum /boot/extlinux/extlinux_pri.conf | awk -F " " '{print $1}')
if [[ $checksum_ext == "ed48b419850e9131782edbf68e6c15a2" ]] ; then cp -p /boot/extlinux/extlinux_pri.conf /boot/backup/extlinux ; fi

log "Copying new extlinux conf file to /boot/extlinux/"
cp extlinux_pri.conf.upgrade_fdt extlinux_pri.conf
CHECKSUM=$(md5sum extlinux_pri.conf | awk -F " " '{print $1}')
status2=$(copy_file -f extlinux_pri.conf -d /boot/extlinux/ -m ${CHECKSUM} -p 644 -o root:root)$?

if [[ $status1 == 0 && $status2 == 0 ]]; then 
  status=0
  check_status $(basename $(pwd)) $status
else
  status=1
  check_status $(basename $(pwd)) $status
fi


fi

log "============= End of install dtb files ===================="

