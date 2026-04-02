#!/usr/bin/env bash

set -e

source  /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh


log "========= Start of copying analytic logs into analytic folder =========="

log "Moving all the analytic related logs in to analytics folder"

status1=0
status2=0

mkdir -p /home/ubuntu/.nddevice/log/analytics/ 

if [[ -n $(ls /home/ubuntu/.nddevice/log/analyticsService_service.*) ]]; then
	mv /home/ubuntu/.nddevice/log/analyticsService_service.* /home/ubuntu/.nddevice/log/analytics/
	status1=$?
fi

check_status $(basename $(pwd)) $status1

log "Moving curr_analyticsService_service.log into analytic folder"


if [[ -n $(ls /home/ubuntu/.nddevice/logsUpload/curr_analyticsService_service*.log) ]]; then
        mv /home/ubuntu/.nddevice/logsUpload/curr_analyticsService_service*.log /home/ubuntu/.nddevice/log/analytics/
        status2=$?
fi

check_status $(basename $(pwd)) $status2

log "========= End of copying analytic logs into analytic folder ========="
