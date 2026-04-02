#!/usr/bin/env bash

source  /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "=============Start of Reverting 99-nd-usb_hub-devices.rules ================="

current_version_full=$(sed -n '/^\[version\]/,/^\[/{s/^nddevice *= *//p}' /home/ubuntu/.nddevice/nddevice.ini)
upgrade_version_full=$(sed -n '/^\[upgrade\]/,/^\[/{s/^nddevice *= *//p}' /home/ubuntu/.nddevice/nddevice.ini)

current_version=$(echo "$current_version_full" | cut -d. -f1-3)
upgrade_version=$(echo "$upgrade_version_full" | cut -d. -f1-3)

log "Current version detected: $current_version"
log "Upgrade version detected: $upgrade_version"

    if [[ "$current_version" == "$upgrade_version" ]]; then
        log "Current version and upgrade version match ($current_version). Skipping revert operation."
        exit 0
    fi

log "Versions differ (current: $current_version, upgrade: $upgrade_version). Proceeding with revert operation."

if [[ -f /home/ubuntu/backup/run_as_root/usb_rules/99-nd-usb_hub-devices.rules ]]; then

	log "Reverting 99-nd-usb_hub-devices.rules"
	cp -f /home/ubuntu/backup/run_as_root/usb_rules/99-nd-usb_hub-devices.rules /etc/udev/rules.d/
	status=$?
else
	log "No 99-nd-usb_hub-devices.rules file in backup"
	status=0

fi

check_status $(basename $(pwd)) $status

log "===============End of Reverting 99-nd-usb_hub-devices.rules ==================="
