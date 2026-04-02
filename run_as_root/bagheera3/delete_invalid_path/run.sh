#!/bin/env bash
  
source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling copy funtion from lib

log "=========== Deleting the invalid paths ==========="

if [[ -d /home/ubntu/ ]] ; then
        log "removing the ubntu folder"
        rm -rf /home/ubntu/
fi

path=/data/nd_files/state_files

if [[ -d /data/nd_files/ ]] ; then
log "/data/nd_files folder is present"
	if [[ -n $(ls $path/*) ]] ; then
	log "observations files are present $(ls $path/*),moving into /home/ubuntu/.nddevice/"
        mv $path/* /home/ubuntu/.nddevice/
        else
        log "observations files are not present,removing the /data/nd_files/ folder"
        fi
	status=$( rm -rf /data/nd_files/ )$?
        check_status $(basename $(pwd)) $status
else
log "/data/nd_files/ folder is not present,Exiting"
check_status $(basename $(pwd)) 0
fi
log "============Done with deleting the invalid path======"
