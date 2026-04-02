#!/usr/bin/env bash


source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling execute_script function from lib
log "=============Command revert to configure/modify crontab=================="
CHECKSUM=$(md5sum krait_conf_old.cron | awk -F " " '{print $1}')
log "Coping krait_conf_old.cron into /home/ubuntu/.nddevice/"
status=$(copy_file -f krait_conf_old.cron -d /home/ubuntu/.nddevice/ -m ${CHECKSUM} -p 664 -o root:root)$?
check_status $(basename $(pwd)) $status

log "Executing the crontab and configure data from file krait_conf_old.cron"
execute_command crontab krait_conf_old.cron
status=$(echo $?)
check_status $(basename $(pwd)) $status
log "=============End of revert configure/modify crontab==================="

