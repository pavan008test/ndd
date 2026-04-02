#!/usr/bin/env bash

set -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

CURRENT_OS_VERSION=$(grep "nd_os" /etc/nd_os_ver.ini | cut -d'=' -f2)

if [[ $CURRENT_OS_VERSION != 13.0.19 ]] && [[ $CURRENT_OS_VERSION != 13.0.1A ]] ; then
    echo "This script is intended for OS version 13.0.19 or 13.0.1A,So Exiting..."    
    exit 0
fi


#Calling copy funtion from lib

CHECKSUM_cp210x=$(md5sum cp210x.ko | awk -F " " '{print $1}')
CHECKSUM_cp210x_system=$(md5sum /lib/modules/4.9.299-tegra/kernel/drivers/usb/serial/cp210x.ko | awk -F " " '{print $1}')
CHECKSUM_lumia3_gpio=$(md5sum lumia3_gpio_configure.sh | awk -F " " '{print $1}')
CHECKSUM_lumia3_gpio_system=$(md5sum /bin/vendor/lumia3_gpio_configure.sh | awk -F " " '{print $1}')
CHECKSUM_lumia_configure=$(md5sum lumia_configure | awk -F " " '{print $1}')
CHECKSUM_lumia_configure_system=$(md5sum /bin/vendor/lumia_configure | awk -F " " '{print $1}')
CHECKSUM_Lumia_drivers=$(md5sum Lumia_drivers_enable.sh | awk -F " " '{print $1}')
CHECKSUM_Lumia_drivers_system=$(md5sum /bin/vendor/Lumia_drivers_enable.sh | awk -F " " '{print $1}')
CHECKSUM_sys_tx2read=$(md5sum sys_tx2read | awk -F " " '{print $1}')
CHECKSUM_sys_tx2read_system=$(md5sum /bin/vendor/sys_tx2read | awk -F " " '{print $1}')
CHECKSUM_blacklist=$(md5sum blacklist.conf | awk -F " " '{print $1}')
CHECKSUM_blacklist_system=$(md5sum /etc/modprobe.d/blacklist.conf | awk -F " " '{print $1}')


log "===========Start of copying the cp210x.ko ,lumia3_gpio_configure.sh,lumia_configure and Lumia_drivers_enable.sh files ==========="
log "=======Start of Copying the cp210x.ko file============"
if [[ "$CHECKSUM_cp210x" == "$CHECKSUM_cp210x_system" ]] ; then
log "cp210x.ko file already exist with same md5sum. Not copying...."
status1=0
else
log "copying the cp210x.ko to /lib/modules/4.9.299-tegra/kernel/drivers/usb/serial/"
status1=$(copy_file -f cp210x.ko -d /lib/modules/4.9.299-tegra/kernel/drivers/usb/serial/ -m $CHECKSUM_cp210x -p 664 -o root:root)$?
fi
log "==========End of Copying the cp210x.ko file=============="

log "============Start of copying the lumia3_gpio_configure.sh script=========="
if [[ "$CHECKSUM_lumia3_gpio" == "$CHECKSUM_lumia3_gpio_system" ]] ; then
log "lumia3_gpio_configure.sh script is already exist with same md5sum. Not copying...."
status2=0
else
log "copying the lumia3_gpio_configure.sh script to /bin/vendor/"
status2=$(copy_file -f lumia3_gpio_configure.sh -d /bin/vendor/ -m $CHECKSUM_lumia3_gpio -p 755 -o root:root)$?
fi
log "==========End of Copying the lumia3_gpio_configure.sh script=============="


log "============Start of copying the lumia_configure to /bin/vendor/=========="
if [[ "$CHECKSUM_lumia_configure" == "$CHECKSUM_lumia_configure_system" ]] ; then
log "lumia_configure bin is already exist with same md5sum. Not copying...."
status3=0
else
log "copying the lumia_configure bin to /bin/vendor/"
status3=$(copy_file -f lumia_configure -d /bin/vendor/ -m $CHECKSUM_lumia_configure -p 775 -o root:root)$?
fi
log "==========End of Copying the lumia_configure bin =============="


log "============Start of copying the Lumia_drivers_enable.sh to /bin/vendor/=========="
if [[ "$CHECKSUM_Lumia_drivers" == "$CHECKSUM_Lumia_drivers_system" ]] ; then
log "Lumia_drivers_enable.sh is already exist with same md5sum. Not copying...."
status4=0
else
log "copying the Lumia_drivers_enable.sh script to /bin/vendor/"
status4=$(copy_file -f Lumia_drivers_enable.sh -d /bin/vendor/ -m $CHECKSUM_Lumia_drivers -p 775 -o root:root)$?
fi
log "==========End of Copying the Lumia_drivers_enable.sh script =============="

log "============Start of copying the sys_tx2read to /bin/vendor/=========="
if [[ "$CHECKSUM_sys_tx2read" == "$CHECKSUM_sys_tx2read_system" ]] ; then
log "sys_tx2read is already exist with same md5sum. Not copying...."
status5=0
else
log "copying the sys_tx2read to /bin/vendor/"
status5=$(copy_file -f sys_tx2read -d /bin/vendor/ -m $CHECKSUM_sys_tx2read -p 775 -o root:root)$?
fi
log "==========End of Copying the sys_tx2read =============="


log "============Start of copying the blacklist.conf to /etc/modprobe.d/ =========="
if [[ "$CHECKSUM_blacklist" == "$CHECKSUM_blacklist_system" ]] ; then
log "blacklist.conf is already exist with same md5sum. Not copying...."
status6=0
else
log "copying the blacklist.conf to /etc/modprobe.d/"
status6=$(copy_file -f blacklist.conf -d /etc/modprobe.d/ -m $CHECKSUM_blacklist -p 644 -o root:root)$?
fi

log "==============Removing the lumia device check status file======="
rm -rf /bin/vendor/lumia_status.txt
log "=========End of removing lumia_status.txt file========"


if [[ $status1 == 0 && $status2 == 0 && $status3 == 0 && $status4 == 0 && $status5 == 0 && $status6 == 0 ]]; then
check_status $(basename $(pwd)) 0
else
check_status $(basename $(pwd)) 1
fi

log "==============End of copying the cp210x.ko ,lumia3_gpio_configure.sh,lumia_configure,Lumia_drivers_enable.sh and sys_tx2read,blacklist.conf files ==========="
