#!/usr/bin/env bash

set -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Script to fix the cron service

log "==============Start of Disable_apt_autoupdate================="

log "Modifying the apt config in the file"
sync
sudo sed -i "/APT::Periodic::Update-Package-Lists/c\APT::Periodic::Update-Package-Lists \"0\";" /etc/apt/apt.conf.d/20auto-upgrades
sudo sed -i "/APT::Periodic::Unattended-Upgrade/c\APT::Periodic::Unattended-Upgrade \"0\";" /etc/apt/apt.conf.d/20auto-upgrades
sudo sed -i "/APT::Periodic::Update-Package-Lists/c\APT::Periodic::Update-Package-Lists \"0\";" /etc/apt/apt.conf.d/10periodic
sync
log "Validating the changes" 

[ `grep "Update-Package-Lists" /etc/apt/apt.conf.d/20auto-upgrades | grep -o '".*"' | sed s/\"//g` == 0 ]
[ `grep "Unattended-Upgrade" /etc/apt/apt.conf.d/20auto-upgrades | grep -o '".*"' | sed s/\"//g` == 0 ]
[ `grep "Update-Package-Lists" /etc/apt/apt.conf.d/10periodic | grep -o '".*"' | sed s/\"//g` == 0 ]


log "Config was modified successfully"
log "==============End of Disable_apt_autoupdate================="
			
exit 0

