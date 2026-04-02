#!/usr/bin/env bash 

set -e 

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

keys_copied_to="/data/misc/adb/"
CHECKSUM=$(md5sum adb_keys | awk '{print $1}')
CHECKSUM_DATA_ADB=$(md5sum ${keys_copied_to}/adb_keys | awk '{print $1}')
CHECKSUM_ROOT_ADB=$(md5sum /adb_keys | awk '{print $1}')

log "Copying adb_keys to /data/misc/adb/"
if [[ ! -f ${keys_copied_to}/adb_keys || ${CHECKSUM_DATA_ADB} != ${CHECKSUM} ]]  ; then
    log "adb_keys are not present @ ${keys_copied_to}, copying..."
    copy_file -f adb_keys -d ${keys_copied_to} -m ${CHECKSUM} -p 644 -o root:root
else
    log "adb_keys is already present @ ${keys_copied_to} for adb secure mode, skipping copy..."
fi

log "Copying adb_keys to / "
if [[ ! -f /adb_keys || ${CHECKSUM_ROOT_ADB} != ${CHECKSUM} ]] ; then
    log "adb_keys are not present @ /, copying..."
    cp --preserve adb_keys /
    sync /adb_keys
else
    log "adb_keys is already present @ / for adb secure mode, skipping copy..."
fi

log "Adding adb secure property in build.prop"
#Removing all existing adb.secure entries in build.prop and adding only one
sed -i "/ro.adb.secure/d" /build.prop
echo "ro.adb.secure=1" >> /build.prop
