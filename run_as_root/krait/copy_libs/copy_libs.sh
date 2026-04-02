#!/usr/bin/env bash 

set -e 

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

nd_lib_path="/nd_lib"
current_os=$(grep -A1 "\[version\]" /etc/nd_os_ver.ini | grep nd_os | awk -F "=" '{print $2}')
current_ota_pkg_dir=$(basename $(realpath /home/ubuntu/.nddevice/latest))
log "Current OS in the device : $current_os"
log "Current OTA package present at device --> $current_ota_pkg_dir"

if [[ $current_os == "10.4.1" ]] ; then

    log "Checking the libs directory in $current_ota_pkg_dir OTA package"
    if [[ -d "/home/ubuntu/.nddevice/${current_ota_pkg_dir}/libs" ]] ; then
        if [[ ! -d  ${nd_lib_path} ]] ; then
            log "${nd_lib_path} is not present, creating ${nd_lib_path} now"
            mkdir -p "${nd_lib_path}"
        fi
        log "Copying libs from ${current_ota_pkg_dir}/libs to ${nd_lib_path}..."
        cp -vfr --preserve /home/ubuntu/.nddevice/${current_ota_pkg_dir}/libs/* "${nd_lib_path}"
        sync
        log "removing the lib*.a files which are not necessary"
        find ${nd_lib_path} -iname '*.a'  -exec rm {} \; # @ delete all static libraries from /nd_lib if present
        sync
    else
        log "${current_ota_pkg_dir} not have lib dir... skipping the copy_libs"
    fi
else
    log "Current_os is not same as 10.4.1. Skipping the libs copy"
fi
