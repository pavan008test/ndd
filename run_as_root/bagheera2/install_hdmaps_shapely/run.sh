#!/usr/bin/env bash


source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling execute_script function from lib

#execute_command_sync sudo dpkg -i libgeos-3.5.0_3.5.0-1ubuntu2_arm64.deb
#status1=$?
#check_status $(basename $(pwd)) $status1
#if [[ -n $(sudo dpkg -l | grep libgeos-3.5.0) ]]
#then
#	log "libgeos library was installed successfully"
#else
#	log "libgeos was not installed. Please check!!!!!!!!! "
#fi

#execute_command_sync sudo dpkg -i libgeos-c1v5_3.5.0-1ubuntu2_arm64.deb
#status2=$?
#check_status $(basename $(pwd)) $status2
#if [[ -n $(sudo dpkg -l | grep libgeos-c1v5) ]]
#then
#	log "libgeos-c1v5 library was installed successfully"
#else
#	log "libgeos-c1v5 was not installed. Please check!!!!!!!!! "
#fi

execute_command_sync pip install /home/ubuntu/.nddevice/ota_temp/run_as_root/install_hdmaps_shapely/Shapely-1.6.4.post2-py2.py3-none-any.whl
status3=$?
check_status $(basename $(pwd)) $status3

