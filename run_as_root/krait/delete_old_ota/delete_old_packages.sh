#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "Starting with delete_old_package.sh script"

log "Space available in /dev/root/ before cleanup old ota packges(if any) is $(df -h | grep "/dev/root" | awk '{print $4}')"

log "Truncate /var/log/ if anyfile more than 50M"
find.findutils /var/log/ -type f -size +50M | xargs -ri truncate -s5M {}

latest_dir=$(basename $(realpath /home/ubuntu/.nddevice/latest))
log "Getting the package linked to latest- $latest_dir"

present_dir=$(cat /home/ubuntu/.nddevice/nddevice.ini | grep -A1 "\[upgrade\]" | tail -1| awk -F " = " '{print $2}')
log "Getting the ota applying package - $present_dir"

log "Checking for any extra packages other than $latest_dir and $present_dir ota package folders..."

if [[ -n $(find /home/ubuntu/.nddevice/ -type d -maxdepth 1 | grep /*.rc.* | grep -v $latest_dir | grep -v $present_dir) ]]
then
        log "removing the unwanted package folders - $(find /home/ubuntu/.nddevice/ -type d -maxdepth 1 | grep /*.rc.* | grep -v $latest_dir | grep -v $present_dir)"
        find /home/ubuntu/.nddevice/ -type d -maxdepth 1 | grep /*.rc.* | grep -v $latest_dir | grep -v $present_dir | xargs -r rm -rf {}
else
        log "No extra package folder to remove"
fi

log "Space available in /dev/root/ after cleanup old ota packges(if any) is $(df -h | grep "/dev/root" | awk '{print $4}')"

