
#!/usr/bin/env bash

set -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling copy funtion from lib

md5sum_syslog_rotate_conf=$(md5sum syslog_rotate.conf  |awk '{print $1}')
md5sum_syslog_rotate_conf_system=$(md5sum /etc/logrotate.d/syslog_rotate.conf  |awk '{print $1}')

log "============ Copying syslog_rotate.conf file  ==========="
log "checking md5sum of existing syslog_rotate.conf file"
if [[ "$md5sum_syslog_rotate_conf" == "$md5sum_syslog_rotate_conf_system" ]]; then
log "syslog_rotate.conf file already exist with same md5sum. Not copying...."
else
log "Copying old syslog_rotate.conf to syslog_rotate.conf_old for backup"
cp /etc/logrotate.d/syslog_rotate.conf /etc/logrotate.d/syslog_rotate.conf_old
log "Checksum not matching copying the syslog_rotate.conf file"
copy_file -f syslog_rotate.conf -d /etc/logrotate.d/ -b /home/ubuntu/.nddevice/backup -m $md5sum_syslog_rotate_conf  -p 644 -o root:root
fi

log "================End of copying syslog_rotate.conf file ==========="

