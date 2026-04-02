#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh


log "=================Start of deleting bagheera_reboot_token_file.bin file======================"

if [[ -f /dev/shm/bagheera_reboot_token_file.bin ]]; then
log "File /dev/shm/bagheera_reboot_token_file.bin  Deleting it"
rm -f /dev/shm/bagheera_reboot_token_file.bin
status=$?
else
log "bagheera_reboot_token_file.bin not present. Skipping it"
check_status $(basename $(pwd)) 0
log "=================End of removing bagheera_reboot_token_file.bin file==================="
exit 0
fi

if [[ $status == 0 ]]; then
        log "bagheera_reboot_token_file.bin deleted successfully"
else
        log "bagheera_reboot_token_file.bin not deleted, please check..!"
fi
check_status $(basename $(pwd)) $status

log "=================End of removing bagheera_reboot_token_file.bin file==================="
