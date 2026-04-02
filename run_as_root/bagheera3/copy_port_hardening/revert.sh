#!/bin/bash

source  /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "========= Start Revert of copy_port_hardening  =========="

log " i. Removing iptables port hardening rules from /etc/iptables.port-rules if exists"

if [ -f /etc/iptables.port-rules ]; then
    rm -f /etc/iptables.port-rules
    status1=$?
    log "    /etc/iptables.port-rules found and removed."
else
    log "    /etc/iptables.port-rules not found."
    status1=0
fi

log " ii. Checking and restoring rc-local.service and rc.local from backups if they exist"

if [ -f /lib/systemd/system/rc-local.service ]; then
    log "    Found /lib/systemd/system/rc-local.service. Removing it to allow restoration from backup."
    rm -f /lib/systemd/system/rc-local.service
    status2=$?
else
    log "    /lib/systemd/system/rc-local.service does not exist. No removal needed."
    status2=0
fi

if [ -f /lib/systemd/system/rc-local_backup.service ]; then
    log "    Found backup /lib/systemd/system/rc-local_backup.service. Restoring it as rc-local.service."
    mv /lib/systemd/system/rc-local_backup.service /lib/systemd/system/rc-local.service
    status3=$?
else
    log "    No backup found for rc-local.service at /lib/systemd/system/rc-local_backup.service. Skipping restore."
    status3=0
fi

if [ -f /etc/rc.local ]; then
    log "    Found /etc/rc.local. Removing it to allow restoration from backup."
    rm -f /etc/rc.local
    status4=$?
else
    log "    /etc/rc.local does not exist. No removal needed."
    status4=0
fi

if [ -f /etc/rc.local_backup ]; then
    log "    Found backup /etc/rc.local_backup. Restoring it as rc.local."
    mv /etc/rc.local_backup /etc/rc.local
    status5=$?
else
    log "    No backup found for rc.local at /etc/rc.local_backup. Skipping restore."
    status5=0
fi

if [[ $status1 -eq 0 && $status2 -eq 0 && $status3 -eq 0 && $status4 -eq 0 && $status5 -eq 0 ]]; then
    log " Port Hardening rules reverted and rc.local/rc-local.service restored successfully with all status 0"
    check_status $(basename $(pwd)) 0
else
    log " Error in reverting Port Hardening rules or restoring rc.local/rc-local.service."
    log "   iptables.port-rules removal status: $status1"
    log "   rc-local.service remove status: $status2"
    log "   rc-local.service restore status: $status3"
    log "   rc.local remove status: $status4"
    log "   rc.local restore status: $status5"
    check_status $(basename $(pwd)) 1
fi

log "========= End Revert of copy_port_hardening  =========="
