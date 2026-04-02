#!/usr/bin/env bash

set -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Script to revert bootstrap binaries copy

log "==============Start of revert to bootstrap_bin_copy================="

log "replacing the old binaries in the bootstrap"
status1=$(rollback /home/ubuntu/.nddevice/backup/otacheck /home/ubuntu/.nddevice/bootstrap/)$?
status2=$(rollback /home/ubuntu/.nddevice/backup/updateEngine /home/ubuntu/.nddevice/bootstrap/)$?
sync
log "checking the status of rollback"
if [[ $status1 == 0 && $status2 == 0 ]]
then
	log "rollback has done successfully"
else
	log "rollback has failed"
	exit 1
fi
log "==============End of revert to bootstrap_bin_copy================="
			
exit 0
