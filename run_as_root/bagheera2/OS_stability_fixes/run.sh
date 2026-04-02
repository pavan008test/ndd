#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling copy funtion from lib 

#copy service file for nvpmodel
log "Copying nvpmodel.service....."
CHKSUM=$(md5sum nvpmodel.service |awk '{print $1}')
status=$(copy_file -f nvpmodel.service -d /etc/systemd/system/ -b /home/ubuntu/.nddevice/backup -m ${CHKSUM} -p 664 -o root:root)$?
check_status "nvpmodel.service" $status
log "running nvpmodel service..."
sudo systemctl enable nvpmodel.service

#copy nv-l4t-usb-device-mode-config.sh
log"copyin nv-l4t-usb-device-mode-config.sh ........"
CHKSUM=$(md5sum nv-l4t-usb-device-mode-config.sh |awk '{print $1}')
status=$(copy_file -f nv-l4t-usb-device-mode-config.sh -d /opt/nvidia/l4t-usb-device-mode/ -b /home/ubuntu/.nddevice/backup -m ${CHKSUM} -p 755 -o root:root)$?
check_status "nv-l4t-usb-device-mode-config.sh" $status

#copy libsys.so
log "copying libsys.so ........."
CHKSUM=$(md5sum libsys.so |awk '{print $1}')
status=$(copy_file -f libsys.so -d /lib/ -b /home/ubuntu/.nddevice/backup -m ${CHKSUM} -p 755 -o root:root)$?
check_status "libsys" $status

#copy ntdi_bag2_startup
log "copy ntdi_bag2_startup ......."
CHKSUM=$(md5sum ntdi_bag2_startup |awk '{print $1}')
status=$(copy_file -f ntdi_bag2_startup -d /etc/init.d/ -b /home/ubuntu/.nddevice/backup -m ${CHKSUM} -p 755 -o ubuntu:ubuntu)$?
check_status "ntdi_bag2_startup" $status

#Deleting duplicated module directory (Actual directory is sudo rm -rf /lib/modules/4.9.140 )
log "Deleting duplicated module dir"
if [ -d /lib/modules/4.9.140/ ]; then
    log "/lib/modules/4.9.140 directory is present, Removing it..."
    sudo rm -rf /lib/modules/4.9.140/
    check_status "remove_module_dir" $status
else
    log "/lib/modules/4.9.140 directory is not present, skipping..."
fi

