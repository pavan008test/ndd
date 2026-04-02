#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "======= Start of rsyslog_config_update task execution ======="
execute_command bash ${PWD}/copy_syslog_rotate_conf.sh
status=$?
check_status $(basename $(pwd)) $status
log "============= End of rsyslog_config_update task execution ==================="

