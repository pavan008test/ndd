#!/usr/bin/env bash


export PARENT_SCRIPT=update_recovery
source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh
root_folder=/home/ubuntu/.nddevice/ota_temp/run_as_root
log "==========Recovery script Start=========="
##revert of each script
success_folder=$(cat $root_folder/run_as_root_status | head -n -1)
recovery_status=0
for task in $success_folder
do
	cd $root_folder/$task
	if [ -f revert.sh ]
	then
    bash revert.sh
    status=$?

    if [ $status == 0 ]
    then
    	continue
    else
    	echo "$task $status" >>/home/ubuntu/.nddevice/ota_temp/run_as_root/update_recovery_status
		recovery_status=1
    fi
fi
done  
if [ $recovery_status == 0 ]
then
	end_of_recovery
else
	echo "update_recovery 1" >>/home/ubuntu/.nddevice/ota_temp/run_as_root/update_recovery_status
	log "revert script for some task is failed. Check the logs for more info."
fi
log "==========Recovery script END=========="


 
