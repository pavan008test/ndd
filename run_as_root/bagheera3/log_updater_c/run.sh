#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh


log "Copying updater_logs_move script and its service file"
## Copying a service file and script folder

md5sum_updaterlogsmove=$(md5sum updater_logs_move.sh | awk -F " " '{print $1}')
status=$(copy_file -f updater_logs_move.sh -d /dev/shm/ -m ${md5sum_updaterlogsmove} -p 775 -o root:root)$?
cp -f onetimelogupdate.service /etc/systemd/system/
check_status $(basename $(pwd)) $status
log "============== End of copy onetimereboot service =============="

log "Starting the onetimereboot service"
sudo systemctl start onetimelogupdate.service
check_status $(basename $(pwd)) $status
log "============== End of copying  updater_logs_move.sh script =============="
