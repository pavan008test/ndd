#!/usr/bin/env bash

set -e 

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "--------------- Staring Kernl deb install ------------"
##################################################################### Kernel install
log "============= Install kernel files =================="

checksum_bootimage_new=f983dc6837d027e00c208d5e54f95d3b
checksum_bootzimage_new=74125eca559f231549d64fe1827d1698
checksum_bootimage=$(md5sum /boot/Image | awk -F " " '{print $1}')
checksum_bootzimage=$(md5sum /boot/zImage | awk -F " " '{print $1}')

log "checking the checksum of /boot/Image and zImage"
if [[ $checksum_bootimage_new != $checksum_bootimage ]] || [[ $checksum_bootzimage_new != $checksum_bootzimage ]] ; then  
      log "Taking backup of /boot/Image and zImage"  
        
      if [[ $checksum_bootimage_new != $checksum_bootimage ]] ; then  
            log "Copying the /boot/Image to /boot/backup"  
            cp -f /boot/Image /boot/backup/  
      else  
            log "Image backup file is already present doing nothing"  
      fi  
  
      if [[ $checksum_bootzimage_new != $checksum_bootzimage ]] ; then  
            log "Copying the /boot/zImage to /boot/backup"  
            cp -f /boot/zImage /boot/backup/  
      else  
            log "zImage backup files are already present doing nothing"  
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
else
      log "Checksum for both Image and zImage match with new files. Skipping installation and verification."
fi 

check_status $(basename $(pwd)) 0
log "-------------- End of kernal deb install ------------"
