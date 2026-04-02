
#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

set -e

#Calling function from lib to replace/update
log "Copying the brcmfmac_oldbck.ko to brcmfmac.ko to revert "
status=$(execute_command_sync cp -f /lib/modules/misc/backup/* /lib/modules/misc/)$?

#Remove old modules
rmmod brcmfmac brcmutil cfg80211 compat
sleep 1
#Install new modules
insmod /lib/modules/misc/compat.ko
insmod /lib/modules/misc/cfg80211.ko
insmod /lib/modules/misc/brcmutil.ko
insmod /lib/modules/misc/brcmfmac.ko debug=0x100000
sleep 2

#Toggle the device for interface to come up
echo -n "3440000.sdhci" > /sys/bus/platform/drivers/sdhci-tegra/unbind
sleep 2
echo -n "3440000.sdhci" > /sys/bus/platform/drivers/sdhci-tegra/bind

check_status $(basename $(pwd)) $status

