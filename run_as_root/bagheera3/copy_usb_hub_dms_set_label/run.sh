#!/usr/bin/env bash

set -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

CURRENT_OS_VERSION=$(grep "nd_os" /etc/nd_os_ver.ini | cut -d'=' -f2)

if [[ $CURRENT_OS_VERSION != 13.0.19 ]] && [[ $CURRENT_OS_VERSION != 13.0.1A ]] ; then
    echo "This script is intended for OS version 13.0.19 or 13.0.1A,So Exiting..."    
    exit 0
fi

#Calling copy funtion from lib

CHECKSUM_dms=$(md5sum dms_set_label | awk -F " " '{print $1}')
CHECKSUM_usb=$(md5sum usb_hub_set_label | awk -F " " '{print $1}')
CHECKSUM_NTLD9=$(md5sum 292B-OV2311_CBR---V230915_00_01-NTLD9.src | awk -F " " '{print $1}')
CHECKSUM_dms_system=$(md5sum /bin/vendor/dms_set_label | awk -F " " '{print $1}')
CHECKSUM_usb_system=$(md5sum /bin/vendor/usb_hub_set_label | awk -F " " '{print $1}')

log "===============Start of copying dms_set_label,usb_hub_set_label and 292B-OV2311_CBR---V230915_00_01-NTLD9.src============="

if [[ $CHECKSUM_dms == $CHECKSUM_dms_system ]]; then
        log "Already Updated dms_set_label is there,So not copying"
        status1=0
else
        log "Copying the updated dms_set_label"
	copy_file -f dms_set_label -d /bin/vendor -b /home/ubuntu/.nddevice/backup -m $CHECKSUM_dms -p 775 -o root:root
	status1=$?
fi


if [[ $CHECKSUM_usb == $CHECKSUM_usb_system ]]; then
	log  "Already Updated usb_hub_set_label is there,So not copying"
        status2=0
else
	log "Copying the updated usb_hub_set_label"
	copy_file -f usb_hub_set_label -d /bin/vendor -b /home/ubuntu/.nddevice/backup -m $CHECKSUM_usb -p 775 -o root:root
	status2=$?
fi

status3=$(copy_file -f 292B-OV2311_CBR---V230915_00_01-NTLD9.src -d /image_upgrade/dms -b /home/ubuntu/.nddevice/backup -m $CHECKSUM_NTLD9 -p 644 -o root:root)$?

if [[ $status1 == 0 && $status2 == 0 && $status3 == 0 ]]; then
check_status $(basename $(pwd)) 0
else
check_status $(basename $(pwd)) 1
fi
log "==============End of copying dms_set_label,usb_hub_set_label and 292B-OV2311_CBR---V230915_00_01-NTLD9.src==========="
