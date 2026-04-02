#!/usr/bin/env bash


source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#calling copy function from lib
log "============= updating the root passwd =================="
log "Checking for any *.lock files in /etc/ which hinders update"
if [[ -f /etc/passwd.lock || -f /etc/shadow.lock ]]; then
	log "Removing /etc/passwd.lock file or /etc/shadow.lock"
	rm -f /etc/passwd.lock
	rm -f /etc/shadow.lock
else
	log "No lock files present in /etc proceeding to password update"
fi

echo "root:EKM2020123Krait" | chpasswd
status=$(echo $?)
check_status $(basename $(pwd)) $status
log "root passwd is updated ok"

log "============= End of updating root passwd ===================="
