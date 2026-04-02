#!/usr/bin/env bash


source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "==============Start of revert_root_passwd ================="

log "Checking for any *.lock files in /etc/ which hinders update"
if [[ -f /etc/passwd.lock || -f /etc/shadow.lock ]]; then
	log "Removing /etc/passwd.lock file or /etc/shadow.lock"
	rm -f /etc/passwd.lock
	rm -f /etc/shadow.lock
else
	log "No lock files present in /etc proceeding to password update"
fi

log "reverting the root passwd to original"
echo "root:oelinux123" | chpasswd
status=$?
check_status $(basename $(pwd)) $status
log "root password is reverted successfully"

log "==============END of revert_root_passwd ================="

exit 0
