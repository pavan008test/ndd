#!/usr/bin/env bash
source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "=============Reverting the bluetooth library files=================="
md5sum_libbluetooth=$(md5sum /usr/lib64/libbluetoothdefault.so.0.0.0_org | awk '{print $1}')
md5sum_libbluetooth_system=$(md5sum /usr/lib64/libbluetoothdefault.so.0.0.0 | awk '{print $1}')

log "start of reverting the libbluetoothdefault.so"
if [[ -f /usr/lib64/libbluetoothdefault.so.0.0.0_org && $md5sum_libbluetooth == $md5sum_libbluetooth_system ]]; then
    log "Original file already restored, no revert needed"
    status=0
    restore_status=0
else
    log "Reverting to original libbluetoothdefault.so"
    if [[ -f /usr/lib64/libbluetoothdefault.so.0.0.0_org ]]; then
        mv /usr/lib64/libbluetoothdefault.so.0.0.0_org /usr/lib64/libbluetoothdefault.so.0.0.0
        restore_status=$?
        status=0
        log "Restored original file from libbluetoothdefault.so.0.0.0_org"
    else
        log "Original backup file not found, cannot revert"
        log "Revert : /usr/lib64/libbluetoothdefault.so.0.0.0_org does not exist so making restore status as 0"
        status=0
        restore_status=0
    fi
fi

if [[ $status -eq 0 && $restore_status -eq 0 ]]; then
    log "All operations completed successfully for reverting libbluetoothdefault.so"
    final_status=0
else
    final_status=1
    log "Summary: Reverting libbluetoothdefault.so"
    log " restore status: $status, revert status: $restore_status"
fi

log "===============End Reverting Bluetooth Library=================="
