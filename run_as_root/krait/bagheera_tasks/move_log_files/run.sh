#!/usr/bin/env bash


source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling execute_script function from lib
log "============== Moving the logs into folders for new log architecture ================="
log "============== First to remove the logfile.json to reset the logs rotation ================"
status=$(execute_command_sync rm -f /home/ubuntu/.nddevice/logfile.json)$?
check_status "Remove of logfile.json from .nddevice" $status

folder=(reboot uploader inference_inertial inference health deleter otacheck updater keep_alive_command scheduler unifieduploader inertialAnalyticsClient inwardAnalyticsClient outwardAnalyticsClient)
for name in ${folder[@]}
do
#Deleting the curr_logs for above mentioned logs since it is moving to new process
log_file1=curr_${name}.log
log_file2=curr_${name}_service.log
if [[ -f /home/ubuntu/.nddevice/logsUpload/$log_file1 ]]
then
log "Removing the old curr logs for $log_file1"
rm -f /home/ubuntu/.nddevice/logsUpload/$log_file1
elif [[ -f /home/ubuntu/.nddevice/logsUpload/$log_file2 ]]
then
log "Removing the old curr logs for $log_file2"
rm -f /home/ubuntu/.nddevice/logsUpload/$log_file2
fi

status1=$(execute_command_sync mkdir -p /home/ubuntu/.nddevice/log/$name)$?
check_status "Making dir of $name" $status1
status2=$(execute_command_sync chown ubuntu:ubuntu /home/ubuntu/.nddevice/log/$name)$?
check_status "Permissions changed for $name" $status2
if [[ -s /home/ubuntu/.nddevice/log/$name.log ]]
then
status3=$(execute_command_sync mv /home/ubuntu/.nddevice/log/$name.log /home/ubuntu/.nddevice/log/$name/$name.log.old)$?
check_status "Moving log into $name" $status3
elif [[ -s /home/ubuntu/.nddevice/log/${name}_service.log ]]
then
status4=$(execute_command_sync mv /home/ubuntu/.nddevice/log/${name}_service.log /home/ubuntu/.nddevice/log/$name/$name.log.old)$?
check_status "Moving log into $name" $status4
else
log "There is no file named $name.log or ${name}_service.log"
fi
done

log "============== End of moving logs ================="
