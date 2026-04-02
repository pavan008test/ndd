#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Copying the obd_config,obd_info.txt,fw_update_mcs,obd_fm_upgrade.sh to their respective paths
log "Preparing for the obd firware update"
log "copying obd_info.txt"
	cp --preserve obd_info.txt /home/ubuntu/.nddevice/
	[[ $? == 0 ]] || log "obd_info.txt not copied"
log "copying obd_config.ini"
	cp --preserve obd_config.ini /home/ubuntu/.nddevice/
	[[ $? == 0 ]] || log "obd_config.ini not copied"
	cp --preserve obd_config.ini /home/ubuntu/config/
	[[ $? == 0 ]] || log "obd_config.ini not copied"
log "copying obd_fm_upgrade.sh"
	cp --preserve obd_fm_upgrade.sh /home/ubuntu/bin/
	[[ $? == 0 ]] || log "obd_fm_upgrade.sh not copied"
log "copying fw_update_mcs"
	cp --preserve fw_update_mcs /home/ubuntu/bin/
	[[ $? == 0 ]] || log "fw_update_mcs not copied"


#Calling execute_script function from lib
#Executing the bootloader check executable to perform OBD firware update
log "Before performing the OBD firware update, checking the bootloader"
bl_status=$(sudo ./obd_bl_version_check >>/home/ubuntu/.nddevice/log/bhcopy.log)$? && echo "checking...."
if [ $bl_status == 0 ]
then
	set -e 
	log "Bootloader check is successful and executing the OBD firware update"
	#execute_script obd_FW_update.sh
	status=$(execute_script obd_FW_update.sh)$?
	check_status $(basename $(pwd)) $status
	#execute_script checkobdupdate.sh
	status=$(execute_script checkobdupdate.sh)$?
	check_status $(basename $(pwd))_check $status
else
	log "Either bootloader not flashed from factory or corrupted so skipping OBD firmware update"
fi
