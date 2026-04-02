#!/usr/bin/env bash

set -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Script to fix the cron service

log "==============Start of bootstrap_bin_copy================="
log "Taking backup of old bootstrap binaries"
status1=$(backup /home/ubuntu/.nddevice/bootstrap/otacheck /home/ubuntu/.nddevice/backup/)$?
status2=$(backup /home/ubuntu/.nddevice/bootstrap/updateEngine /home/ubuntu/.nddevice/backup/)$? 
if [[ $status1 == 0 && $status2 == 0 ]]
then
	log "Backup is successful and stored in /home/ubuntu/.nddevice/backup/"
else
	log "Backup is not successful....Exiting...."
	exit 1
fi
log "Fetching the package folder to get the latest binaries"
version=$(grep -A1 upgrade /home/ubuntu/.nddevice/nddevice.ini | grep nddevice | cut -d "=" -f2 | tr -d ' ')
log "Got the latest version to get the new binaries - $version"
log "Checking the md5sum of binaries"
otacheck_md5sum=$(md5sum /home/ubuntu/.nddevice/$version/otacheck | awk -F " " '{print $1}')
updateEngine_md5sum=$(md5sum /home/ubuntu/.nddevice/$version/updateEngine | awk -F " " '{print $1}')
cp -f --preserve /home/ubuntu/.nddevice/$version/otacheck /home/ubuntu/.nddevice/bootstrap/
cp -f --preserve /home/ubuntu/.nddevice/$version/updateEngine /home/ubuntu/.nddevice/bootstrap/
sync
otacheck_bootstrap=$(md5sum /home/ubuntu/.nddevice/bootstrap/otacheck | awk -F " " '{print $1}')
updateEngine_bootstrap=$(md5sum /home/ubuntu/.nddevice/bootstrap/updateEngine | awk -F " " '{print $1}') 
if [[ $otacheck_md5sum == $otacheck_bootstrap && $updateEngine_md5sum == $updateEngine_bootstrap ]] 
then
log "Latest binaries were replaced successfully"
else
log "Latest binaries copy failed"
exit 1
fi
log "==============End of bootstrap_bin_copy================="
			
exit 0

