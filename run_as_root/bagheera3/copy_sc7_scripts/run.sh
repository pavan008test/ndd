#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling copy funtion from lib

CHECKSUM_exit=$(md5sum sc7_exit.sh | awk -F " " '{print $1}')
CHECKSUM_entry=$(md5sum sc7_entry.sh | awk -F " " '{print $1}')
CHECKSUM_exit_system=$(md5sum /etc/init.d/sc7_exit.sh |awk '{print $1}')
CHECKSUM_entry_system=$(md5sum /etc/init.d/sc7_entry.sh |awk '{print $1}')

log "==========Copying the sc7_exit and sc7_entry scripts============"
log "checking the md5sum of the sc7_exit.sh scripts"

if [[ "$CHECKSUM_exit" == "$CHECKSUM_exit_system" ]] ; then
log "sc7_exit.sh file already exist with same md5sum. Not copying...."
status1=0
else
log "copying the sc7_exit.sh to /etc/init.d/"
status1=$(copy_file -f sc7_exit.sh -d  /etc/init.d/ -m ${CHECKSUM_exit} -p 755 -o root:root)$?
fi

log "checking the md5sum of the sc7_entry.sh scripts"
if [[ "$CHECKSUM_entry" == "$CHECKSUM_entry_system" ]] ; then
log "sc7_entry.sh file already exist with same md5sum. Not copying...."
status2=0
else
log "copying the sc7_entry.sh to /etc/init.d/"
status2=$(copy_file -f sc7_entry.sh -d  /etc/init.d/ -m ${CHECKSUM_entry} -p 755 -o root:root)$?
fi

if [[ $status1 == 0 && $status2 == 0 ]]; then	
check_status $(basename $(pwd)) 0
else
check_status $(basename $(pwd)) 1
fi
log "=======End of copying the sc7_exit and sc7_entry scripts==========="
