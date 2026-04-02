#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "===========Assigning CPU affinities to ND Services==============="
log "Executing the  systemd_override_b2.sh"    
execute_script systemd_override_b2.sh  
status1=$?  
 
if [[ $status1 == 0 ]];then
    check_status $(basename $(pwd)) 0
    log "All checks passed successfully"
else
    check_status $(basename $(pwd)) 1
    log "Something went wrong please check the logs"
fi
log "===========End of Assigning CPU affinities to ND Services==============="
