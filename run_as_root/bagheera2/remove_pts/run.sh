#!/usr/bin/env bash

set -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh


log "============start of Removing pts scripts and bash_history======================"

rm -rvf /home/ubuntu/pts/conf
rm -rvf /home/ubuntu/pts/build/MD5SUM.txt
rm -rvf /home/ubuntu/pts/build/prepare_pts_img
rm -rvf /home/ubuntu/pts/build/flashfirmware_bagheera2.sh
rm -rvf /home/ubuntu/pts/build/validate_deploy_nd
rm -rvf /home/ubuntu/pts/build/nddeviceptslib.so
rm -rvf /home/ubuntu/pts/build/nd_enable_services
rm -rvf /home/ubuntu/pts/build/qa_script
rm -rvf /home/ubuntu/.bash_history
rm -rvf /home/ubuntu/.viminfo
rm -rvf /home/ubuntu/.history
rm -rvf /root/.viminfo
rm -rvf /root/.bash_history
status=$?

if [[ $status == 0 ]]; then
	log "pts scripts and bash_history removed successfully"
else
	log "pts scripts not removed please check..!"
fi
check_status $(basename $(pwd)) $status

log "=================End of Removing pts scripts and bash_history================="
