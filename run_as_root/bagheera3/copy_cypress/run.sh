#!/usr/bin/env bash

set -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh


CURRENT_OS_VERSION=$(grep "nd_os" /etc/nd_os_ver.ini | cut -d'=' -f2)  

if [[ $CURRENT_OS_VERSION != 13.0.19 ]] && [[ $CURRENT_OS_VERSION != 13.0.1A ]] ; then     
    echo "This script is intended for OS version 13.0.19 or 13.0.1A,So Exiting..."    
    exit 0    
fi

log "=========Copying the cypress files==========="
CHECKSUM_cyfmac54591_pcie=$(md5sum cyfmac54591-pcie.txt | awk -F " " '{print $1}')
CHECKSUM_cyfmac54591_pcie_system=$(md5sum /lib/firmware/cypress/cyfmac54591-pcie.txt | awk -F " " '{print $1}')
CHECKSUM_nvidia=$(md5sum cyfmac54591-pcie.nvidia,p3509-0000+p3636-0001.bin | awk -F " " '{print $1}')
CHECKSUM_cyfmac4356_pcie=$(md5sum cyfmac4356-pcie.clm_blob | awk -F " " '{print $1}')
CHECKSUM_cyfmac4356_pcie_system=$(md5sum /lib/firmware/cypress/cyfmac4356-pcie.clm_blob | awk -F " " '{print $1}')
CHECKSUM_cyfmac4356_pcie_txt=$(md5sum cyfmac4356-pcie.txt | awk -F " " '{print $1}')
CHECKSUM_cyfmac4356_pcie_txt_system=$(md5sum /lib/firmware/cypress/cyfmac4356-pcie.txt | awk -F " " '{print $1}')
CHECKSUM_cyfmac4356_pcie_bin=$(md5sum cyfmac4356-pcie.bin | awk -F " " '{print $1}')
CHECKSUM_cyfmac4356_pcie_bin_system=$(md5sum /lib/firmware/cypress/cyfmac4356-pcie.bin | awk -F " " '{print $1}')
CHECKSUM_cyfmac4356_sdio=$(md5sum cyfmac4356-sdio.clm_blob | awk -F " " '{print $1}')
CHECKSUM_cyfmac4356_sdio_system=$(md5sum /lib/firmware/cypress/cyfmac4356-sdio.clm_blob | awk -F " " '{print $1}')
CHECKSUM_cyfmac4356_sdio_bin=$(md5sum cyfmac4356-sdio.bin | awk -F " " '{print $1}')
CHECKSUM_cyfmac4356_sdio_bin_system=$(md5sum /lib/firmware/cypress/cyfmac4356-sdio.bin | awk -F " " '{print $1}')

log "========Copying the cyfmac54591-pcie.txt=========="

if [[ $CHECKSUM_cyfmac54591_pcie == $CHECKSUM_cyfmac54591_pcie_system ]] ; then
log "cyfmac54591-pcie.txt file already exist with same md5sum. Not copying...."
status1=0
else
log "copying the cyfmac54591-pcie.txt to /lib/firmware/cypress/"
status1=$(copy_file -f cyfmac54591-pcie.txt -d /lib/firmware/cypress/ -m $CHECKSUM_cyfmac54591_pcie -p 644 -o root:root)$?
fi
log "========End of copying cyfmac54591-pcie.txt=========="

log "===========Copying the cyfmac4356-pcie.clm_blob=========="
if [[ $CHECKSUM_cyfmac4356_pcie == $CHECKSUM_cyfmac4356_pcie_system ]] ; then
log "cyfmac4356-pcie.clm_blob file already exist with same md5sum. Not copying...."
status2=0
else
log "copying the cyfmac4356-pcie.clm_blob to /lib/firmware/cypress/"
status2=$(copy_file -f cyfmac4356-pcie.clm_blob -d /lib/firmware/cypress/ -m $CHECKSUM_cyfmac4356_pcie -p 644 -o root:root)$?
fi
log "========End of Copying the cyfmac4356-pcie.clm_blob=========="

log "===========Copying the cyfmac4356-pcie.txt =========="
if [[ $CHECKSUM_cyfmac4356_pcie_txt == $CHECKSUM_cyfmac4356_pcie_txt_system ]] ; then
log "cyfmac4356-pcie.txt file already exist with same md5sum. Not copying...."
status3=0
else
log "copying the cyfmac4356-pcie.txt to /lib/firmware/cypress/"
status3=$(copy_file -f cyfmac4356-pcie.txt -d /lib/firmware/cypress/ -m $CHECKSUM_cyfmac4356_pcie_txt -p 644 -o root:root)$?
fi
log "========End of Copying the cyfmac4356-pcie.txt=========="

log "===========Copying the cyfmac4356-pcie.bin =========="
if [[ $CHECKSUM_cyfmac4356_pcie_bin == $CHECKSUM_cyfmac4356_pcie_bin_system ]] ; then
log "cyfmac4356-pcie.bin file already exist with same md5sum. Not copying...."
status4=0
else
log "copying cyfmac4356-pcie.bin to /lib/firmware/cypress/"
status4=$(copy_file -f cyfmac4356-pcie.bin -d /lib/firmware/cypress/ -m $CHECKSUM_cyfmac4356_pcie_bin -p 644 -o root:root)$?
fi
log "========End of Copying the cyfmac4356-pcie.bin=========="

log "===========Copying the cyfmac4356-sdio.clm_blob =========="
if [[ $CHECKSUM_cyfmac4356_sdio == $CHECKSUM_cyfmac4356_sdio_system ]] ; then
log "cyfmac4356-sdio.clm_blob file already exist with same md5sum. Not copying...."
status5=0
else
log "copying cyfmac4356-sdio.clm_blob to /lib/firmware/cypress/"
status5=$(copy_file -f cyfmac4356-sdio.clm_blob -d /lib/firmware/cypress/ -m $CHECKSUM_cyfmac4356_sdio -p 644 -o root:root)$?
fi
log "========End of Copying the cyfmac4356-sdio.clm_blob=========="


log "===========Copying the cyfmac4356-sdio.bin =========="
if [[ $CHECKSUM_cyfmac4356_sdio_bin == $CHECKSUM_cyfmac4356_sdio_bin_system ]] ; then
log "cyfmac4356-sdio.bin file already exist with same md5sum. Not copying...."
status6=0
else
log "copying cyfmac4356-sdio.bin to /lib/firmware/cypress/"
status6=$(copy_file -f cyfmac4356-sdio.bin -d /lib/firmware/cypress/ -m $CHECKSUM_cyfmac4356_sdio_bin -p 644 -o root:root)$?
fi
log "========End of Copying the cyfmac4356-sdio.bin=========="

log "====Copying cyfmac54591-pcie.nvidia,p3509-0000+p3636-0001.bin==========="
status7=$(copy_file -f cyfmac54591-pcie.nvidia,p3509-0000+p3636-0001.bin -d /lib/firmware/cypress/ -m $CHECKSUM_nvidia -p 644 -o root:root)$?
log "End of copying the cyfmac54591-pcie.nvidia,p3509-0000+p3636-0001.bin==========="


if [[ $status1 == 0 && $status2 == 0 && $status3 == 0 && $status4 == 0 && $status5 == 0 && $status6 == 0 && $status7 == 0 ]]; then
log "All the cypress latest files copied successfully"
check_status $(basename $(pwd)) 0
else
log "Something wrong to copy cypress files.Please check !!!!"
check_status $(basename $(pwd)) 1
fi

log "====================End of copying the cypress files===================="
