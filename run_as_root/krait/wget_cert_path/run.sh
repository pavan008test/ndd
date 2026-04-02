#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling execute_script function from lib
log "===== Starting of wget_cert_path ====="
log "Adding the cert path in the wgetrc file"
status=$(grep -qxF 'ca-certificate = /etc/ssl/certs/ca-certificates.crt' /etc/wgetrc || bash -c 'echo "ca-certificate = /etc/ssl/certs/ca-certificates.crt" >> /etc/wgetrc')$?
check_status $(basename $(pwd)) $status
log "===== End of wget_cert_path ====="
