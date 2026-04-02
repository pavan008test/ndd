#!/usr/bin/env bash


source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling copy funtion from lib

md5sum_e33_centerleft_NTDI=$(md5sum e33_centerleft_NTDI.isp  |awk '{print $1}')
md5sum_e33_centerleft_NTDI_system=$(md5sum /var/nvidia/nvcam/settings/e33_centerleft_NTDI.isp |awk '{print $1}')

log "============ Copying the e33_centerleft_NTDI.isp ==========="
log "checking md5sum of existing e33_centerleft_NTDI.isp file"
if [[ "$md5sum_e33_centerleft_NTDI" == "$md5sum_e33_centerleft_NTDI_system" ]]; then
  log "e33_centerleft_NTDI.isp file is already exist with same md5sum. Not copying...."
  status=0
else
  log "Checksum not matching copying the updated e33_centerleft_NTDI.isp file"
  copy_file -f e33_centerleft_NTDI.isp -d /var/nvidia/nvcam/settings/  -b /home/ubuntu/.nddevice/backup -m $md5sum_e33_centerleft_NTDI  -p 644 -o root:root
  status=$?
fi

check_status $(basename $(pwd)) $status
log "==========End of copying the e33_centerleft_NTDI.isp============"
