#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "============ Start of copying cli_mgr ==========="

md5sum_cli_mgr=$(md5sum cli_mgr | awk '{print $1}')
md5sum_cli_mgr_system=$(md5sum /bin/vendor/cli_mgr | awk '{print $1}')

if [[ $md5sum_cli_mgr == $md5sum_cli_mgr_system ]]; then

	log "Already Updated cli_mgr is there,So not copying"
	status=0
else
	copy_file -f cli_mgr -d /bin/vendor/ -b /home/ubuntu/.nddevice/backup -m $md5sum_cli_mgr -p 755 -o root:root
	status=$?
fi
check_status $(basename $(pwd)) $status


log "============ End of copying cli_mgr to /bin/vendor/ ==========="
