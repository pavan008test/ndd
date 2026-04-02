#!/usr/bin/env bash


source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#calling copy function from lib
log "=============updating the reboot.sh =================="
copy_file -f reboot.sh -d /home/ubuntu/.nddevice/ -b /home/ubuntu/.nddevice/backup -m 871c11fa9aa8c13882da0ec9cc79a4b0
status=$(echo $?)	
check_status $(basename $(pwd)) $status
log "Checking if com_reboot file is present in .nddevice"
if [[ -f /home/ubuntu/.nddevice/com_reboot_ren ]] ; then
    execute_command "mv /home/ubuntu/.nddevice/com_reboot_ren /home/ubuntu/.nddevice/com_reboot"
    status=$(echo $?)
    check_status $(basename $(pwd)) $status
else
    log "com_reboot file might be already present. Skipping the rename..."
fi
log "=============End of updating reboot.sh===================="
