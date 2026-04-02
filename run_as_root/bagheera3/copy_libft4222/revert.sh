#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "============ reverting the libft4222 ==========="

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

log "Removing the existing libft4222.so.1.4.4.232 and its link"
rm -f /usr/lib/libft4222.so.1.4.4.232 /usr/lib/libft4222.so

log "Reverting the expander_init"
if [[ -f /home/ubuntu/backup/run_as_root/expander_init/expander_init ]]; then
	log "Reverting expander_init"
	cp -f /home/ubuntu/backup/run_as_root/expander_init/expander_init /bin/vendor/
	status=$?
else
	log "No expander_init file in backup"
	status=0
fi

log "Done with replacing the expander_init and removing,linking the libft4222.so"

check_status $(basename $(pwd)) $status

log "============End of removing the libft4222 and reverting the expander_init============="
