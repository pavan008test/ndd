#!/usr/bin/env bash

set -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling copy funtion from lib

CHECKSUM_cyfmac4354_sdio_bin=$(md5sum cyfmac4354-sdio.bin | awk '{print $1}')
CHECKSUM_cyfmac4354_sdio_clm_blob=$(md5sum cyfmac4354-sdio.clm_blob | awk '{print $1}')
CHECKSUM_cyfmac4354_sdio_nvidia_lightning=$(md5sum cyfmac4354-sdio.nvidia,lightning.txt | awk '{print $1}')

log "============= start checking and updating of cyfmac4354-sdio.bin and cyfmac4354-sdio.clm_blob in /lib/firmware/cypress/ path ==========="

if [[ -d /home/ubuntu/old_firmware/ ]]
then 
log "Directory already exist and So not creating...."
else
log "creating old_firmware folder in /home/ubuntu/ path"
mkdir -p /home/ubuntu/old_firmware/
fi

log "=====Checks to copy cyfmac4354-sdio.bin file=========="
if [[ -f /home/ubuntu/old_firmware/cyfmac4354-sdio.bin ]]
then 
log "cyfmac4354-sdio.bin file already copied and not empty. So not copying...."
else
cp /lib/firmware/cypress/cyfmac4354-sdio.bin /home/ubuntu/old_firmware/ 
copy_file -f cyfmac4354-sdio.bin -d /lib/firmware/cypress/ -m $CHECKSUM_cyfmac4354_sdio_bin -p 644
fi

log "=====Checks to copy cyfmac4354-sdio.clm_blob file=========="
if [[ -f /home/ubuntu/old_firmware/cyfmac4354-sdio.clm_blob ]]
then 
log "cyfmac4354-sdio.clm_blob file already copied and not empty. So not copying...."
else
cp /lib/firmware/cypress/cyfmac4354-sdio.clm_blob /home/ubuntu/old_firmware/
copy_file -f cyfmac4354-sdio.clm_blob -d /lib/firmware/cypress/ -m $CHECKSUM_cyfmac4354_sdio_clm_blob -p 644
fi

log "=====Checks to copy cyfmac4354-sdio.nvidia,lightning.txt file=========="
if [[ -f /home/ubuntu/old_firmware/cyfmac4354-sdio.nvidia,lightning.txt ]]
then 
log "cyfmac4354-sdio.nvidia,lightning.txt file already copied and not empty. So not copying...."
else
cp /lib/firmware/cypress/cyfmac4354-sdio.nvidia,lightning.txt /home/ubuntu/old_firmware/
copy_file -f cyfmac4354-sdio.nvidia,lightning.txt -d /lib/firmware/cypress/ -m $CHECKSUM_cyfmac4354_sdio_nvidia_lightning -p 644
fi


log "============= End of checking and updating of cyfmac4354-sdio.bin and cyfmac4354-sdio.clm_blob and CHECKSUM_cyfmac4354_sdio_nvidia_lightning in /lib/firmware/cypress/ path ==========="

