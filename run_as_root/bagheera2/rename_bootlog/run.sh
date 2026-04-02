#!/usr/bin/env bash


source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling execute_script function from lib

log "=============Start of rename boot_log==============="

log "Checking if boot_log script is already present"
if [[ -s /bin/boot_log && $(md5sum /bin/boot_log | awk -F " " '{print $1}') == 4874fb4aef5dd21ab96495c47125685f \
&& $(md5sum /bin/boot_log.bak | awk -F " " '{print $1}') == df90064c723f09046e66b902d45907d7 ]]
then 
log "boot_log script is already present and also binary is backed up"
exit 0
else
log "boot_log script is not present."
log "renaming the bootlog binary"
status=$(execute_command mv /bin/boot_log /bin/boot_log.bak)$?
check_status $(basename $(pwd)) $status

log "Copying the dummy bootlog script to bin"
status=$(copy_file -f boot_log -d /bin/ -b /home/ubuntu/.nddevice/backup -m 4874fb4aef5dd21ab96495c47125685f -p 555 -o root:root)$?
check_status $(basename $(pwd)) $status
fi
log "============End of rename boot_log=================="