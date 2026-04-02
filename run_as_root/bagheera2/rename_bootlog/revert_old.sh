#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Script to fix the cron service

log "==============Start of rename_boot_log revert================="

log "reverting renamed boot_log"
sync
if [[ -s /bin/boot_log.bak ]]
then
status=$(execute_command mv -f /bin/boot_log.bak /bin/boot_log)$?
check_status $(basename $(pwd)) $status
sync
else 
log "boot_log.bak is not present in the bin"
fi

if [[ $(md5sum /bin/boot_log | awk -F " " '{print $1}') == df90064c723f09046e66b902d45907d7 ]]
then
log "reverting boot_log binary is success"
else
log "reverting not happened properly"
exit 1
fi
log "==============End of rename_boot_log================="

exit 0

