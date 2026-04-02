#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling execute_script function from lib

log "=============Copying ndzramconfig.sh file in /usr/bin/ =================="
CHECKSUM=$(md5sum ndzramconfig.sh | awk -F " " '{print $1}')
status1=$(copy_file -f ndzramconfig.sh -d /usr/bin/ -b /home/ubuntu/.nddevice/backup -m ${CHECKSUM} -p 775)$?

log "=============Copying ndzram.service file in /etc/systemd/system/ =================="

value=$(awk -F' *= *' '$1=="'"devicetype"'" {print $2}' "/home/ubuntu/config/deviceconfig.ini")

if [[ $value == "krait" ]]
then
        #Calling function from lib to replace/upgrade the ndzram_D210.service @ /etc/systemd/system/
        mv ndzram_D210.service ndzram.service
        CHECKSUM=$(md5sum ndzram.service | awk -F " " '{print $1}')
        status2=$(copy_file -f ndzram.service -d /etc/systemd/system/ -m ${CHECKSUM} -p 775 -o root:root)$?
elif [[ $value == "krait2" ]]
then
        #Calling function from lib to replace/upgrade the ndzram_D210.service @ /etc/systemd/system/
        mv ndzram_D215.service ndzram.service
        CHECKSUM=$(md5sum ndzram.service | awk -F " " '{print $1}')
        status2=$(copy_file -f ndzram.service -d /etc/systemd/system/ -m ${CHECKSUM} -p 775 -o root:root)$?
else
        log "$value is not matching to krait or krait2"
        status2=1
fi

if [[ $status1 == 0 && $status2 == 0 ]]
then
        log "ndzram.service and ndzramconfig.sh file copied successfully"
else
        log "Something wrong to ndzram.service or ndzramconfig.sh file.Please check !!!!"
        exit 1
fi

# Enabling the ndzram service
systemctl enable ndzram
# Starting the ndzram service
systemctl start ndzram

if [ $? -eq 0 ]; then
    log "ndzram enabled and started successfully"
    check_status $(basename $(pwd)) 0
else
    log "Failed to enable and start ndzram"
    check_status $(basename $(pwd)) 1
fi

log "=============End of Copy ndzramconfig.sh and ndzram.service files===================="
