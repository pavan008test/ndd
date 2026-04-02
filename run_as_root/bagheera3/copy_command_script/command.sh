#!/bin/bash

# Script to log basic PRE and POST OS_OTA_UPGRADE  Sanity Info

#updating the /etc/nd_os_ver.ini and /etc/vendor/version_ntdi.txt
argument=$1
if [[ $# == 0 ]]; then
sed -i s/"nd_os.*"/"nd_os=12.4.3.1"/g /etc/nd_os_ver.ini
sed -i s/vvdn_os.*/vvdn_os=\"Version:1.4.1.5\"/g /etc/nd_os_ver.ini
sed -i -e "$ a \\\n\[os_ota_upgrade\]\nflashed_date=$(date)" /etc/nd_os_ver.ini
sed -i s/"\"Version.*"/"\"Version:1.4.1.5\""/g /etc/vendor/version_ntdi.txt
fi

epochtime=$(date +%s%3N)
log=/home/ubuntu/.nddevice/log/svc/log_osota_$epochtime.log

echo "------ Dpkg installed version for 4 debs -----" 2>&1 >> $log
dpkg -l nvidia-l4t-kernel-dtbs nvidia-l4t-kernel netradyne-rootfs nvidia-l4t-bootloader 2>&1 >> $log


#dtb
echo -e " \n -------- DTB discontinuous clock status --------" 2>&1 >> $log
find /sys/ -iname "discontinuous_clk" | xargs -I {} ls {} 2>&1 >>  $log
find /sys/ -iname "discontinuous_clk" | xargs -I {} cat {}  2>&1 >>  $log
echo -e " \n ---------- cat /sys/class/i2c-dev/i2c-2/device/bus_clk_rate----------" 2>&1 >> $log
cat /sys/class/i2c-dev/i2c-2/device/bus_clk_rate 2>&1 >> $log
echo -e "\n --------md_test 0x02441008 4-----------" 2>&1 >> $log
sudo  md_test 0x02441008 4 2>&1 >> $log
sudo  md_test 0x0243d008 4 2>&1 >> $log


#kernel
echo -e " \n --------Kernel version--------" 2>&1 >> $log
uname -v 2>&1 >>  $log
#rootfs
echo -e " \n ------- /etc/nd_os_ver.ini ---------" 2>&1 >> $log
cat /etc/nd_os_ver.ini  2>&1 >>  $log
echo -e " \n------- /etc/vendor/version_ntdi.txt -------" 2>&1 >> $log
cat /etc/vendor/version_ntdi.txt 2>&1 >>  $log
#bootloader
echo -e " \n ---------- Bootloader version -----------" 2>&1 >> $log
fw_printenv  | grep "ver=U-Boot"  2>&1 >>  $log

rm -f /home/ubuntu/.nddevice/com_root
