#!/usr/bin/env bash

source  /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling copy funtion from lib 


log "====================Start of installing shapely==2.0.1====="
log "installing shapely==2.0.1"
pip3 install shapely-2.0.1-cp39-cp39-manylinux_2_17_aarch64.manylinux2014_aarch64.whl
status1=$?
check_status $(basename $(pwd)) $status1

log "=========== End of installing the shapely==2.0.1 ==========="
