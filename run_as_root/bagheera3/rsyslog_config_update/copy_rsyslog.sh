
#!/usr/bin/env bash

set -x

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling copy funtion from lib

md5sum_rsyslog=$(md5sum rsyslog  |awk '{print $1}')
md5sum_rsyslog_system=$(md5sum /etc/logrotate.d/rsyslog  |awk '{print $1}')

log "============ Copying rsyslog file  ==========="
log "checking md5sum of existing rsyslog file"
if [[ "$md5sum_rsyslog" == "$md5sum_rsyslog_system" ]]; then
log " rsyslog file already exist with same md5sum. Not copying...."
else
log "copying old rsyslog to rsyslog_old for backup"
cp /etc/logrotate.d/rsyslog /etc/logrotate.d/rsyslog_old
log "Checksum not matching copying the rsyslog file"
copy_file -f rsyslog -d /etc/logrotate.d/ -b /home/ubuntu/.nddevice/backup -m $md5sum_rsyslog -p 644 -o root:root
fi

log "================End of copying rsyslog file ==========="

