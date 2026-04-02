#!/bin/bash 

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "===============Start of checkobdupdate==================="
#Checks if the obd update is successful or not
result=1

log "Checking whether the status file present in ota_temp folder"
if [[ -f /home/ubuntu/.nddevice/ota_temp/app1.txt && -f /home/ubuntu/.nddevice/ota_temp/app2.txt ]]
then 
	if [[ $(cat /home/ubuntu/.nddevice/ota_temp/app1.txt | awk '{print $2}') == 0 && $(cat /home/ubuntu/.nddevice/ota_temp/app2.txt | awk '{print $2}') == 0 ]]
	then
		log "Obd update success"
		result=0
	else
		#log "Status is not expected.Obd update failed. Stopping the OTA run. Try again by checking the logs."
		#Below result can be removed if OTA want to stop on obd fw failure
		log "Status is not expected. Obd update failed. Try again by checking the logs."
		result=0
	fi
else 
	log "Status files are not present.Obd update failed. Try again by checking the logs."
	#Below result can be removed if OTA want to stop on obd fw failure
	result=0 
fi
exit $result
log "===============End of checkobdupdate==================="
