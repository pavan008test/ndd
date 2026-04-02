#!/usr/bin/env bash

set -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "=====================Start of Taking backup of certificate files ====================="

md5sum_root=$(md5sum /home/ubuntu/.nddevice/certificate/root-CA.crt |awk '{print $1}')
md5sum_ed2=$(md5sum /home/ubuntu/.nddevice/certificate/ed25519key.pem |awk '{print $1}')
md5sum_pub=$(md5sum /home/ubuntu/.nddevice/certificate/pub-ed25519.pem |awk '{print $1}')

log "Taking backup of root-CA.crt"
if [[ -f /home/ubuntu/.nddevice/certificate/root-CA.crt ]];
then
log "root-CA.crt file exists"
    if [[ -f /home/ubuntu/backup/root-CA.crt_$md5sum_root ]];
    then
    log "Already root-CA.crt backup is done"
    status0=0    
    else
    status0=$(backup /home/ubuntu/.nddevice/certificate/root-CA.crt /home/ubuntu/backup/root-CA.crt_$md5sum_root)$?
    fi
else
log "root-CA.crt file not exists"
status0=0
fi

log "Taking backup of ed25519key.pem"
if [[ -f /home/ubuntu/.nddevice/certificate/ed25519key.pem ]];
then
log "ed25519key.pem file exists"
    if [[ -f /home/ubuntu/backup/ed25519key.pem_$md5sum_ed2 ]];
    then
    log "Already ed25519key.pem backup is done"
    status1=0
    else
    status1=$(backup /home/ubuntu/.nddevice/certificate/ed25519key.pem /home/ubuntu/backup/ed25519key.pem_$md5sum_ed2)$?
    fi
else
log "ed25519key.pem file not exists"
status1=0
fi

log "Taking backup of pub-ed25519.pem"
if [[ -f /home/ubuntu/.nddevice/certificate/pub-ed25519.pem ]];
then
log "pub-ed25519.pem file exists"
    if [[ -f /home/ubuntu/backup/pub-ed25519.pem_$md5sum_pub ]];
    then
    log "Already pub-ed25519.pem backup is done"
    status2=0
    else
    status2=$(backup /home/ubuntu/.nddevice/certificate/pub-ed25519.pem /home/ubuntu/backup/pub-ed25519.pem_$md5sum_pub)$?
    fi
else
log "pub-ed25519.pem file not exists"
status2=0
fi

if [[ $status0 == 0 && $status1 == 0 && $status2 == 0 ]];
then
check_status $(basename $(pwd)) 0
else
check_status $(basename $(pwd)) 1
fi
log "======================End of certificate files backup========================"
