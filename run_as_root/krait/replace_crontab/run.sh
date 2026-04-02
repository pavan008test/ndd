#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling execute_script function from lib
log "=============replace the crontab file=================="
copy_file -f crontab -d /etc/ -b /home/ubuntu/.nddevice/backup -m 083210c3a47cc2cd1631ec1ed099eaed -p 644
status=$(echo $?)
check_status $(basename $(pwd)) $status
rm -f /var/lib/logrotate.status.tmp-*.backup
status=$(echo $?)
check_status $(basename $(pwd)) $status

log "=============End of replacing the crontab file===================="
