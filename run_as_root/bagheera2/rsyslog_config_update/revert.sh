#!/usr/bin/env bash

set -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#revert the rsyslog_config_update task

log "==============Start of revert rsyslog_config_update ================="

log "Checking if rsyslog_old file is present in /etc/logrotate.d/"
if [[ -f /etc/logrotate.d/rsyslog_old ]] ; then
    log "rsyslog_old file is present in /etc/logrotate.d/, reverting it..."
    log "renaming rsyslog_old to rsyslog"
    execute_command "mv /etc/logrotate.d/rsyslog_old /etc/logrotate.d/rsyslog"
    status=$?
    check_status $(basename $(pwd)) $status
else
    log "/etc/logrotate.d/rsyslog_old is not present. Revert failed..."
    check_status $(basename $(pwd)) 0
fi

log "==============End of revert rsyslog_config_update================="

