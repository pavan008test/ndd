#!/usr/bin/env bash


source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling copy funtion from lib

md5sum_GobiNet=$(md5sum GobiNet.ko  |awk '{print $1}')
md5sum_GobiSerial=$(md5sum GobiSerial.ko |awk '{print $1}')
md5sum_GobiNet_system=$(md5sum /lib/modules/4.9.140-tegra/kernel/drivers/net/usb/GobiNet/GobiNet.ko  |awk '{print $1}')
md5sum_GobiSerial_system=$(md5sum /lib/modules/4.9.140-tegra/kernel/drivers/usb/serial/GobiSerial/GobiSerial.ko |awk '{print $1}')

log "============ Copying the GobiNet and GobiSerial ko files  ==========="
log "checking md5sum of existing GobiNet.ko file"
if [[ "$md5sum_GobiNet" == "$md5sum_GobiNet_system" ]]; then

log "GobiNet.ko file already exist with same md5sum. Not copying...."
else
log "Checksum not matching copying the ko file"
status=$(copy_file -c move -f GobiNet.ko -d /lib/modules/4.9.140-tegra/kernel/drivers/net/usb/GobiNet/ -b /home/ubuntu/.nddevice/backup -m $md5sum_GobiNet  -p 644 -o root:root)$?
check_status $(basename $(pwd))_Net $status
fi

log "checking md5sum of existing GobiSerial.ko file"
if [[ "$md5sum_GobiSerial" == "$md5sum_GobiSerial_system" ]]; then
log "GobiSerial.ko file already exist with same md5sum. Not copying...."
else
log "Checksum not matching copying the ko file"
status=$(copy_file -c move -f GobiSerial.ko -d /lib/modules/4.9.140-tegra/kernel/drivers/usb/serial/GobiSerial/ -b /home/ubuntu/.nddevice/backup -m $md5sum_GobiSerial  -p 644 -o root:root)$?
check_status $(basename $(pwd))_Serial $status
fi
log "================End of copying the GobiNet and GobiSerial ko files ==========="






