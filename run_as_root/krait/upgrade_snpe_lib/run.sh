#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#upgrading the required snpe lib @ /nd_lib
log "======= Start of upgrade_snpe_lib task execution ======="

snpe_lib_path="/nd_lib/snpe"
nd_lib_path="/nd_lib"

log "Upgrading the snpe lib from $snpe_lib_path"
if [[ -d "${snpe_lib_path}" ]] ; then
    log "${snpe_lib_path} is present. cleaning it before copy the latest snpe and dependencies libs"
    rm -rf ${snpe_lib_path}
else
    log "${snpe_lib_path} directory is not present, skipping the cleaning process."
fi

if [[ ! -d ${nd_lib_path} ]] ; then
    mkdir -p ${nd_lib_path}
fi

log "Untaring snpe libs and dependencies files tar directly into ${nd_lib_path}..."
execute_command "sudo tar -xzf snpe_upgrade_nd_lib.tar.gz -C ${nd_lib_path}"
if [[ $? != 0 ]] ; then
    log "tar command failed to untar the snpe_upgrade_nd_lib.tar.gz into ${nd_lib_path}" ; 
    status=1 ; 
fi 

check_status $(basename $(pwd)) $status
log "============= End of upgrade_snpe_lib task execution ==================="

