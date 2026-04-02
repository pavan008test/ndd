#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

####### file path's to change permission ########
pingResponse_PATH=/home/ubuntu/.nddevice/pingResponse.txt
keepaliveresponse_PATH=/dev/shm/nd_files_c/keepaliveresponse.txt
device_action_PATH=/dev/shm/device_action_result.txt
keep_alive_command_PATH=/dev/shm/nd_files_c/keep_alive_command.txt



#function to give permissons to keep_alive_command files

file_pemsn() {
	file=$1

file_name=$(echo $file | sed "s/.*\///")
if [[ -f $file ]]; then 
     log "changing file permissons for $file_name file"
     status1=$(chmod 766 $file)$?
     
if [[ $status1 == 0 ]]; then
        log "giving permission for $file_name successfully"
        else
        log "Something wrong in $file_name Please check !!!!"
        fi
else
        log "$file_name is not present. skipping the action"
fi
		
}

log "============Start of changing the file permissons RAR task============"
file_pemsn $pingResponse_PATH
file_pemsn $keepaliveresponse_PATH
file_pemsn $device_action_PATH
file_pemsn $keep_alive_command_PATH

if [[ -d /home/ubuntu/.nddevice/log/keep_alive_command ]] ; then
log "changing the keep_alive_command dir and it files permissions"
status_dir=$(chmod -R 766 /home/ubuntu/.nddevice/log/keep_alive_command)$?
else
log "keep_alive_command dir is not present,so skipping"
status_dir=0
fi

check_status $(basename $(pwd)) $status1

log "============End of changing the file permissons RAR task============"
