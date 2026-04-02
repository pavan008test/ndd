#!/bin/bash 

function log {
       echo -n "`date +%Y-%m-%d` `date +"%T,%3N"` - __run_as_root_statuscheck__ - INFO - ">> /home/ubuntu/.nddevice/log/bhcopy.log 2>&1
       echo $1 >> /home/ubuntu/.nddevice/log/bhcopy.log 2>&1
}

log "===============Check run_as_root_status START=============="
#Checks if command_run script is successfully or not
log "Waiting max 5 min for run_as_root service to complete its task"
timeout 300 sh -c 'while : ; do if [ -n "`grep "run_as_root" /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_status`" ] ; then break; else sleep 2; continue; fi ; done' && echo "run_as_root success"

status=`grep "run_as_root" /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_status | awk '{print $2}'`
log "Status = $status"
if [[ $status != 0 ]]
then
log "run_as_root is failed. Check above failure"
log "Stoping and cleaning the run_as_root service"
sudo /usr/bin/nd_service.sh -c stop -n run_as_root -p /home/ubuntu/.nddevice/__version__/service/
sudo /usr/bin/nd_service.sh -c clean -n run_as_root -p /home/ubuntu/.nddevice/__version__/service/
exit 1
else
log "run_as_root successfully executed"
sudo /usr/bin/nd_service.sh -c clean -n run_as_root -p /home/ubuntu/.nddevice/__version__/service/
fi

log "================Check run_as_root_status END==============="

