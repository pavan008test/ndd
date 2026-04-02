#!/usr/bin/env bash

set -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

current_ota=$(grep -i -A1 "\[version\]" /home/ubuntu/.nddevice/nddevice.ini | grep -i nddevice | awk -F "=" '{print $2}' | tr -d " ")

log "--------------- Staring Kernl deb install ------------"
##################################################################### Kernel install
log "============= Install kernel files =================="

log "Taking backup of /boot/Image and zImage"
if [[ ! -f /boot/backup/Image &&  ! -f /boot/backup/zImage ]]; then 
sudo mkdir -p /boot/backup/
cp -f /boot/Image /boot/backup/
cp -f /boot/zImage /boot/backup/
else
log "Backup files are already present doing nothing"
fi

log "Install new kernel deb file"
execute_command_sync dpkg -i ./nvidia-l4t-kernel_4.9.140-tegra-32.4.3-20200625213407_arm64.deb
status_kernelinstall=$?
sleep 1
log "Verify kernel install"
if [[ -z $(sudo dpkg -V nvidia-l4t-kernel | grep -v modules.alias | grep -v modules.dep | grep -v modules.symbols ) && $(dpkg -l nvidia-l4t-kernel | tail -1 | awk '{print $1}') == "ii" ]]; then
log "verification successful"
status_kernelverify=0
else
log "verification failed. Some md5sum is mismatched.Please check"
status_kernelverify=1
fi
log "dpkg -l for this package - $(dpkg -l nvidia-l4t-kernel | tail -1)"

if [[ $status_kernelinstall != 0 || $status_kernelverify != 0 ]]; then
log "Kernel is not installed properly. Reverting it"
log "Reverting the Image & zImage from /boot/backup/"
sudo cp -f /boot/backup/Image /boot/
sudo cp -f /boot/backup/zImage /boot/

check_status $(basename $(pwd)) 1

else
log "Kernel installed successfully"
fi

log "============= End of kernel files ===================="


check_status $(basename $(pwd)) 0

log "--------------- End of kernal deb install ------------"

