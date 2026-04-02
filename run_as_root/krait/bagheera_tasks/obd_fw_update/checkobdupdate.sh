#!/bin/bash 

set -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "===============Start of checkobdupdate==================="
#Checks if the obd update is successful or not
result=1

log "Checking the file is present or not in that folder"
if [[ -f /home/ubuntu/.nddevice/ota_temp/app1.txt && -f /home/ubuntu/.nddevice/ota_temp/app2.txt ]]
then 
	if [[ `cat /home/ubuntu/.nddevice/ota_temp/app1.txt | awk '{print $2}'` -eq 0 && `cat /home/ubuntu/.nddevice/ota_temp/app2.txt | awk '{print $2}'` -eq 0 ]]
	then
		log "Obd update success"
		result=0
	else
		log "Status is not expected.Obd update failed. Stopping the OTA run. Try again by checking the logs."
	fi
else 
	log "Status files are not present.Obd update failed. Stopping the OTA run. Try again by checking the logs." 
fi
exit $result
log "===============End of checkobdupdate==================="
