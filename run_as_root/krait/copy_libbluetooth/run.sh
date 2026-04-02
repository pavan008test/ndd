#!/usr/bin/env bash
source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "=============Copying the bluetooth library files=================="
md5sum_libbluetooth=$(md5sum libbluetoothdefault.so.0.0.0 | awk '{print $1}')
md5sum_libbluetooth_system=$(md5sum /usr/lib64/libbluetoothdefault.so.0.0.0 | awk '{print $1}')

log "start of Copying the libbluetoothdefault.so"
if [[ -s /usr/lib64/libbluetoothdefault.so.0.0.0 && $md5sum_libbluetooth == $md5sum_libbluetooth_system ]]; then
    log "Already Updated libbluetoothdefault.so is there, So not copying"
    status=0
    rename_status=0
else
    log "Copying the latest libbluetoothdefault.so"
    if [[ -s /usr/lib64/libbluetoothdefault.so.0.0.0 ]]; then
        mv /usr/lib64/libbluetoothdefault.so.0.0.0 /usr/lib64/libbluetoothdefault.so.0.0.0_org
        rename_status=$?
        log "Renamed existing file to libbluetoothdefault.so.0.0.0_org to take the backup"
    else 
        log "No file /usr/lib64/libbluetoothdefault.so.0.0.0 to take the backup as _org"
        rename_status=0
    fi
    copy_file -f libbluetoothdefault.so.0.0.0 -d /usr/lib64/ -m $md5sum_libbluetooth -p 755 -o root:root
    status=$?
fi

if [[ $status -eq 0 && $rename_status -eq 0 ]]; then
    log "All operations completed successfully for copying libbluetoothdefault.so"
    final_status=0
else
    final_status=1
    log "Summary: Copying libbluetoothdefault.so" 
    log " copy status: $status, rename status: $rename_status"
fi
check_status $(basename $(pwd)) $final_status 

log "===============End Copying Bluetooth Library=================="
