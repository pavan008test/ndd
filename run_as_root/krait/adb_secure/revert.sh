#!/usr/bin/env bash


source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling execute_script function from lib
log "=============Command revert to adb secure =================="
log "Checking the adb is secured with ro.adb.secure..."
if grep -q "ro.adb.secure" /build.prop ; then
    log "making adb secured, OFF"
    sed -i "/ro.adb.secure/d" /build.prop
    status=$(echo $?)
    check_status $(basename $(pwd)) $status
else
    log "adb is not secured with ro.adb.secure, nothing to revert..."
fi

log "=============End of revert adb secure==================="

