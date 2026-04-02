#!/usr/bin/env bash


source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling execute_script function from lib
log "============== OS check ==============="

current_os=$(cat /etc/nd_os_ver.ini | grep nd_os | awk -F "=" '{print $2}'| cut -c 4-)
current_ota=$(grep -i -A1 "\[version\]" /home/ubuntu/.nddevice/nddevice.ini | grep -i nddevice | awk -F "=" '{print $2}' | tr -d " ")
#instead of list usage, we are using relational operator for os check
#supported_os=(12.3.2)
#supported_ota=(3.5.6.rc.10 3.5.6.rc.11)

log "Checking the compatibility of OS for this OTA"

#Getting major and minor num fom os version and comparing. Current OS should be greater or equal to 3.2 (12.3.2)
if [[ $current_os > 3.2 || $current_os == 3.2 ]] ; then
	log "Current OS is greater than or equal to 3.2 (12.3.2)" #Proceeding with OTA check"
	#if [[ "${supported_ota[@]}" =~ "$current_ota" ]]; then 
	check_status $(basename $(pwd)) 0
	#else
	#log "Current OTA is not supported. It expects 3.5.6.rc.10.or 3.5.6.rc.11"
	#log "Hence OTA is not installed. Failing..."
	#check_status $(basename $(pwd)) 1
	#fi

else
	log "Current OS is not supported with this OTA. Hence OTA is not installed. Failing..."
	check_status $(basename $(pwd)) 1
fi

log "============== End of OS check ==============="

