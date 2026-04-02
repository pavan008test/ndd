#!/usr/bin/env bash


source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Script to revert ntdi_icdc_done task

log "==============Start of ntdi_icdc_done revert================="

current_ota=$(cat /home/ubuntu/.nddevice/nddevice.ini | grep -a1 version | grep nddevice | awk -F "=" '{print $2}' | tr -d ' ' | cut -d "." -f 1-3)
ota_list=(0.5.3 0.5.4)
for version in ${ota_list[@]}
do
    if [[ $current_ota == $version ]]
    then
        log "This revert task is not required for this current OTA $current_ota"
      exit 0
    fi
done

if [ -f "/etc/startup.sh" ] ; then
	log "reverting the /etc/startup.sh from ntdi_icdc_done changes"
	if  grep -q "touch /dev/shm/ntdi_icdc.done" /etc/startup.sh ; then
		sed -i "/touch \/dev\/shm\/ntdi_icdc.done/d" /etc/startup.sh
		if ! grep -q "touch /dev/shm/ntdi_icdc.done" /etc/startup.sh ; then
				log "reverting the /etc/startup.sh from ntdi_icdc_done changes is successful"
		elif [[ -f /home/ubuntu/.nddevice/backup/startup.sh ]]; then
				cp -f /home/ubuntu/.nddevice/backup/startup.sh /etc/
				[[ $? == 0 ]] && log "backup startup.sh file has been copied to /etc/ succesfully"
		else
				log "reverting the /etc/startup.sh from ntdi_icdc_done changes is failed"
		fi

	else
			log "No touch command present in /etc/startup.sh"
	fi

else
	log "/etc/startup.sh file not found"
fi

log "==============END of ntdi_icdc_done revert================="

exit 0
