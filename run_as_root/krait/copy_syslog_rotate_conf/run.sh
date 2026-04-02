#!/usr/bin/env bash


source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling execute_script function from lib
log "=============Copying syslog_rotate.conf file=================="
CHECKSUM=$(md5sum syslog_rotate.conf | awk '{print $1}')
copy_file -f syslog_rotate.conf -d /etc/vendor/ -b /home/ubuntu/.nddevice/backup -m $CHECKSUM
status=$?
check_status $(basename $(pwd)) $status

log "=============End of Copying syslog_rotate.conf file===================="
