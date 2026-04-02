#!/usr/bin/env bash


source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling execute_script function from lib
log "=============Installing the isp firmware=================="
copy_file -f OV495_firmware_201709050848_1FD2.bin -d /data/ -b /home/ubuntu/.nddevice/backup -m 92ae801a122a7805691f9831d93f5439 -p 775  -o root:root
log "Executing the command bash /etc/isp_upgrade/ov491_isp_upgrade.sh /data/OV495_firmware_201709050848_1FD2.bin >/home/ubuntu/.nddevice/log/isp_flash.log"
bash /etc/isp_upgrade/ov491_isp_upgrade.sh /data/OV495_firmware_201709050848_1FD2.bin >/home/ubuntu/.nddevice/log/isp_flash.log 2>&1
status=$(echo $?)	
check_status $(basename $(pwd)) $status

log "=============End of isp firmware==================="
