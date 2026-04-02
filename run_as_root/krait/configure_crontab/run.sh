#!/usr/bin/env bash


source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling execute_script function from lib
log "=============Command to configure/modify crontab=================="
CHECKSUM=$(md5sum krait_conf.cron | awk -F " " '{print $1}')
log "Coping krait_conf.cron into /home/ubuntu/.nddevice/"
status1=$(copy_file -f krait_conf.cron -d /home/ubuntu/.nddevice/ -m ${CHECKSUM} -p 664 -o root:root)$?
check_status $(basename $(pwd)) $status

log "Executing the crontab and configure data from file krait_conf.cron"
execute_command crontab krait_conf.cron
status2=$?

kill -9 $(ps -ef | grep scheduler | grep -v grep | awk '{print $2}')
kill -9 $(ps -ef | grep inference | grep -v grep | awk '{print $2}')

if [[ $status1==0 && $status2==0 ]]; then
log "crontab update is successful"
check_status $(basename $(pwd)) 0
else
log "crontab update  failed"
check_status $(basename $(pwd)) 1
fi
log "=============End of configure/modify crontab==================="

