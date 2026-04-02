#!/usr/bin/env bash

set -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh


log "============start of Removing pts,thermal folders and bash_history======================"

rm -rvf /pts_app
rm -rvf /thermal_app
rm -rvf /home/ubuntu/.bash_history
rm -rvf /home/ubuntu/.viminfo
rm -rvf /home/ubuntu/.history
rm -rvf /root/.viminfo
rm -rvf /root/.bash_history


status=$?

if [[ $status == 0 ]]; then
	log "pts,thermal and bash_history removed successfully"
else
	log "pts,thermal folders and bash_history not removed please check..!"
fi
check_status $(basename $(pwd)) $status

log "=================End of Removing pts,thermal folders and bash_history ==================="
