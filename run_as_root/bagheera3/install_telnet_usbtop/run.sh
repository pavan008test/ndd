#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

CURRENT_OS_VERSION=$(grep "nd_os" /etc/nd_os_ver.ini | cut -d'=' -f2)

if [[ $CURRENT_OS_VERSION != 13.0.19 ]] && [[ $CURRENT_OS_VERSION != 13.0.1A ]] ; then
    echo "This script is intended for OS version 13.0.19 or 13.0.1A,So Exiting..."    
    exit 0
fi

logs=/home/ubuntu/.nddevice/log/bhcopy.log

#calling copy function from lib
log "============Installing telnet_0.17-41_arm64.deb and usbtop_1.0+dfsg-1build1_arm64.deb========="

sudo dpkg -i telnet_0.17-41_arm64.deb 2>&1 | tee -a "$logs"
sudo dpkg -i usbtop_1.0+dfsg-1build1_arm64.deb 2>&1 | tee -a "$logs"

log "Installed all the deb files"
sleep 3

log "Verifying the telnet_0.17-41_arm64.deb installation"
   if [[ -n $(sudo dpkg -l | grep  telnet) ]]; then
   log "telnet  package was installed successfully"
   status1=0
   else
   log "telnet was not installed. Please check!!!!!!!!! "
   status1=1
   fi
log "Successfully installed and verified the telnet deb package"


log "Verifying the usbtop_1.0+dfsg-1build1_arm64.deb installation"
    if [[ -n $(sudo dpkg -l | grep usbtop) ]]; then
    log "usbtop  package was installed successfully"
    status2=0
    else 
    log "usbtop was not installed. Please check!!!!!!!!! "
    status2=1
    fi
log "Successfully installed and verified the usbtop deb package"

if [[ $status1 == 0 && $status2 == 0 ]]; then
check_status $(basename $(pwd)) 0
else
check_status $(basename $(pwd)) 1
fi

log "=======End of installing the telnet and usbtop deb packages==========="

