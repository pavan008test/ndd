#!/usr/bin/env bash

set -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh


log "==============Start of revert standy_setup_cron================="

log "Reverting the crontab changes to original with OS"
status=$(execute_command_sync crontab /home/ubuntu/.nddevice/root_conf.cron)
check_status "install_standby_setup" $status

log "==============End of revert standby_setup_cron================="

exit 0
