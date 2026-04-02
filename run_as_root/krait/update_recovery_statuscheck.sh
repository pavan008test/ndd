#!/bin/bash 

function log {
       echo -n "`date +%Y-%m-%d` `date +"%T,%3N"` - __update_recovery_statuscheck__ - INFO - ">> /home/ubuntu/.nddevice/log/bhcopy.log 2>&1
       echo $1 >> /home/ubuntu/.nddevice/log/bhcopy.log 2>&1
}

log "===============Check update_recovery_status START=============="
#Checks if command_run script is successfully or not
log "Waiting max 5 min for run_as_root service to complete its task"
timeout -t 300 sh -c 'while : ; do if [ -n "`grep "update_recovery" /home/ubuntu/.nddevice/ota_temp/run_as_root/update_recovery_status`" ] ; then break; else sleep 2; continue; fi ; done' && echo "update_recovery success"

status=`grep "update_recovery" /home/ubuntu/.nddevice/ota_temp/run_as_root/update_recovery_status | awk '{print $2}'`
log "Status = $status"
if [[ $status != 0 ]]
then
log "update_recovery is failed. Check above failure"
log "Stoping and cleaning the update_recovery service"
/usr/bin/nd_service.sh -c clean -n update_recovery -p /home/ubuntu/.nddevice/__version__/service/
rm -rf /home/ubuntu/.nddevice/ota_temp
else
log "update_recovery successfully executed"
/usr/bin/nd_service.sh -c clean -n update_recovery -p /home/ubuntu/.nddevice/__version__/service/
rm -rf /home/ubuntu/.nddevice/ota_temp
fi

log "================Check update_recovery_status END==============="


