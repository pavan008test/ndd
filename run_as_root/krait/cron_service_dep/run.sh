#!/usr/bin/env bash


source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling execute_script function from lib
log "============= Start of cron_service_dep =============="
current_os=$(grep -A1 "\[version\]" /etc/nd_os_ver.ini | grep nd_os | awk -F "=" '{print $2}')
log "Checking the current OS..."

if [[ $current_os != "10.4.4" ]] ; then
    log "=============Copying the cron_dep.sh file=================="
    copy_file -f cron_dep.sh -d /home/ubuntu/bin/ -b /home/ubuntu/.nddevice/backup -m c9c796d1713c3f038b9b5ea49bf15210 -p 755
    status=$(echo $?)
    check_status $(basename $(pwd)) $status
    log "=============End of Copying the cron_dep.sh file===================="

    log "=============Copying the crond.service file=================="
    copy_file -f crond.service -d /lib/systemd/system/ -b /home/ubuntu/.nddevice/backup -m 5980796a724f8c30e533123807d99c0e -p 644
    status=$(echo $?)
    check_status $(basename $(pwd)) $status

else
    echo "Current OS is 10.4.4 which has this changes. So skipping the changes..."
fi

log "=============End of Copying the crond.service file===================="
log "============= End of cron_service_dep =============="
