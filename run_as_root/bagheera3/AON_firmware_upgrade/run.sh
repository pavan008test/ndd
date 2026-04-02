#!/usr/bin/env bash


source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling copy funtion from lib

CURRENT_OS_VERSION=$(grep "nd_os" /etc/nd_os_ver.ini | cut -d'=' -f2)  
  
if [[ $CURRENT_OS_VERSION != 13.0.19 ]] && [[ $CURRENT_OS_VERSION != 13.0.1A ]] ; then     
    echo "This script is intended for OS version 13.0.19 or 13.0.1A,So Exiting..."    
    exit 0    
fi

md5sum_aon_check=$(md5sum aon_check.sh |awk '{print $1}')
md5sum_aon_check_system=$(md5sum /etc/init.d/aon_check.sh |awk '{print $1}')
md5sum_app_ver1b=$(md5sum app_ver1b.efm8 |awk '{print $1}')

log "============ Copying the aon_check.sh file ==========="
log "checking md5sum of existing aon_check.sh file"
if [[ "$md5sum_aon_check" == "$md5sum_aon_check_system" ]]; then
log "aon_check file already exist with same md5sum. Not copying...."
status1=0
else
log "Checksum not matching copying the aon_check.sh file"
status1=$(copy_file -f aon_check.sh  -d /etc/init.d/ -b /home/ubuntu/.nddevice/backup -m $md5sum_aon_check  -p 755 -o root:root)$?
fi
log "========== End of copying the aon_check.sh file============="


log "=========Upgrading the AON_FIRMWARE======"
if [[ -f /image_upgrade/aon/app_ver1b.efm8 ]] ; then
log "app_ver1b.efm8 file already exits,so skipping"
status2=0
status3=0
else
log "Coping the app_ver1b.efm8 file to /image_upgrade/aon/"
status2=$(copy_file -f app_ver1b.efm8 -d /image_upgrade/aon/ -b /home/ubuntu/.nddevice/backup -m $md5sum_app_ver1b  -p 664 -o ubuntu:ubuntu)$?
log "Linking the app_ver1b.efm8 to bag3_aon.efm8"
status3=$(execute_command ln -sf /image_upgrade/aon/app_ver1b.efm8 /image_upgrade/aon/bag3_aon.efm8)$?
log "chaning the ownership of the bag3_aon.efm8 file"
chown -h  ubuntu:ubuntu /image_upgrade/aon/bag3_aon.efm8
log "Moving the old version to aon_backup_images"
mv /image_upgrade/aon/app_ver15.efm8 /image_upgrade/aon/aon_backup_images/
log "Making aon_retry_count.txt as null"
echo 0 > /etc/init.d/aon_retry_count.txt
fi

if [[ $status1 == 0 && $status2 == 0 && $status3 == 0 ]]; then
check_status $(basename $(pwd)) 0
else
check_status $(basename $(pwd)) 1
fi

log "===========End of upgrading the AON_FIRMWARE============"

