#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "==============Start of Disable_apt_autoupdate================="

log "Disable and remove snapd service"
sudo systemctl disable snapd snapd.socket 
sudo apt -y purge snapd
sudo apt -y purge appstream

log "Modifying the apt config in the file"
sudo sed -i "/APT::Periodic::Update-Package-Lists/c\APT::Periodic::Update-Package-Lists \"0\";" /etc/apt/apt.conf.d/10periodic
sudo sed -i "/APT::Periodic::Download-Upgradeable-Packages/c\APT::Periodic::Download-Upgradeable-Packages \"0\";" /etc/apt/apt.conf.d/10periodic
sudo sed -i "/APT::Periodic::AutocleanInterval/c\APT::Periodic::AutocleanInterval \"0\";" /etc/apt/apt.conf.d/10periodic
sudo sed -i "/APT::Periodic::Unattended-Upgrade/c\APT::Periodic::Unattended-Upgrade \"0\";" /etc/apt/apt.conf.d/10periodic

log "Removing the permission for apt-get binary"
sudo chmod 0 /usr/bin/apt-get

sync

log "Validating the changes" 

if [ "$(grep "Update-Package-Lists" /etc/apt/apt.conf.d/10periodic | grep -o '".*"' | sed s/\"//g)" == 0 ]; then log "Update-Package-Lists is 0" ; else log "Update-Package-Lists is not 0 !!!!!!!" ; fi
if [ "$(grep "Download-Upgradeable-Packages" /etc/apt/apt.conf.d/10periodic | grep -o '".*"' | sed s/\"//g)" == 0 ]; then log "Download-Upgradeable-Packages is 0" ; else log "Download-Upgradeable-Packages is not 0 !!!!!!!" ; fi
if [ "$(grep "AutocleanInterval" /etc/apt/apt.conf.d/10periodic | grep -o '".*"' | sed s/\"//g)" == 0 ]; then log "AutocleanInterval is 0" ; else log "AutocleanInterval is not 0 !!!!!!!" ; fi
if [ "$(grep "Unattended-Upgrade" /etc/apt/apt.conf.d/10periodic | grep -o '".*"' | sed s/\"//g)" == 0 ]; then log "Unattended-Upgrade is 0" ; else log "Unattended-Upgrade is not 0 !!!!!!!" ; fi


log "apt and snapd services were diabled successfully"
log "==============End of Disable_apt_autoupdate================="

