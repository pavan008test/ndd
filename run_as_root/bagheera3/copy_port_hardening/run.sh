#!/bin/bash

source  /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "========= Start of copy_port_hardening  =========="

log " i. Checking if /etc/iptables.port-rules exists and comparing md5sum"

iptables_port_rules_md5sum=$(md5sum iptables.port-rules | awk '{print $1}')
log "    Local iptables.port-rules md5sum: $iptables_port_rules_md5sum"

if [[ -f /etc/iptables.port-rules ]]; then
    target_md5sum=$(md5sum /etc/iptables.port-rules | awk '{print $1}')
    log "    Target /etc/iptables.port-rules md5sum: $target_md5sum"
    
    if [[ "$iptables_port_rules_md5sum" == "$target_md5sum" ]]; then
        log "    Both target and new file md5sum matching. Skipping copy."
        status1=0
    else
        log "    md5sum mismatch. Copying new iptables.port-rules"
        copy_file -f iptables.port-rules -d /etc/ -m $iptables_port_rules_md5sum -p 775 -o root:root
        status1=$?
    fi
else
    log "    /etc/iptables.port-rules does not exist. Copying new file"
    copy_file -f iptables.port-rules -d /etc/ -m $iptables_port_rules_md5sum -p 775 -o root:root
    status1=$?
fi

log " ii. Checking if rc-local.service is present, backing up if exists, and copying new file"
rc_local_service_md5sum=$(md5sum rc-local.service | awk '{print $1}')
log "    Local rc-local.service md5sum: $rc_local_service_md5sum"

if [[ -f /lib/systemd/system/rc-local.service ]]; then
    target_md5sum=$(md5sum /lib/systemd/system/rc-local.service | awk '{print $1}')
    log "    Target rc-local.service md5sum: $target_md5sum"

    if [[ "$rc_local_service_md5sum" == "$target_md5sum" ]]; then
        log "    Both target and new rc-local.service md5sum matching. Skipping copy."
        status2=0
    else
        log "    md5sum mismatch. Backing up and copying new rc-local.service"
        mv /lib/systemd/system/rc-local.service /lib/systemd/system/rc-local_backup.service
        rename_status=$?
        if [[ $rename_status -ne 0 ]]; then
            log "    Failed to backup rc-local.service"
            status2=1
        else
            copy_file -f rc-local.service -d /lib/systemd/system/ -m $rc_local_service_md5sum -p 644 -o root:root
            status2=$?
        fi
    fi
else
    log "    /lib/systemd/system/rc-local.service does not exist. Copying new file"
    copy_file -f rc-local.service -d /lib/systemd/system/ -m $rc_local_service_md5sum -p 644 -o root:root
    status2=$?
fi

status3=0
log " iii. Checking if /etc/rc.local exists and backing up if present, then copying new file"

rc_local_md5sum=$(md5sum rc.local | awk '{print $1}')
log "    Local rc.local md5sum: $rc_local_md5sum"

if [[ -f /etc/rc.local ]]; then
    target_md5sum=$(md5sum /etc/rc.local | awk '{print $1}')
    log "    Target /etc/rc.local md5sum: $target_md5sum"
    
    if [[ "$rc_local_md5sum" == "$target_md5sum" ]]; then
        log "    Both target and new file md5sum matching. Skipping copy."
        status3=0
        status4=0
    else
        log "    md5sum mismatch. Backing up existing /etc/rc.local to /etc/rc.local_backup"
        mv /etc/rc.local /etc/rc.local_backup
        rename_status=$?
        if [[ $rename_status -ne 0 ]]; then
            log "    Failed to backup /etc/rc.local"
            status3=1
            status4=1
        else
            status3=0
            log "    Copying new rc.local"
            copy_file -f rc.local -d /etc/ -m $rc_local_md5sum -p 775 -o root:root
            status4=$?
        fi
    fi
else
    log "    /etc/rc.local does not exist. Copying new file"
    status3=0
    copy_file -f rc.local -d /etc/ -m $rc_local_md5sum -p 775 -o root:root
    status4=$?
fi


if [[ $status1 -eq 0 && $status2 -eq 0 && $status3 -eq 0 && $status4 -eq 0 ]]; then
    log " Port Hardening rules copied and rc.local setup successfully with all status 0"
    check_status $(basename $(pwd)) 0
else
    log " Error in copying Port Hardening rules or setting up rc.local."
    log "      iptables.port-rules copy status: $status1"
    log "      rc-local.service backup / copy status: $status2"
    log "      /etc/rc.local backup status: $status3"
    log "      rc.local copy status: $status4"
    check_status $(basename $(pwd)) 1
fi

log "========= End of copy_port_hardening  =========="

