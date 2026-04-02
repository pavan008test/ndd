 #!/usr/bin/env bash

set -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling copy funtion from lib

log "=========== Start of copy CLE_tool ==========="
log "CLE_tool_B2.tar.gz  unzipping the tar file to /home/ubuntu/.nddevice/"
log "Checking for any existing CLE_TOOL File"

if [[ -d /home/ubuntu/.nddevice/CLE_tool/ ]]; then
	
	log "CLE_tool alreddy exists so removing it and copying latest CLE_tool"
	rm -rf /home/ubuntu/.nddevice/CLE_tool
fi

tar -xvf CLE_tool_B2.tar.gz -C /home/ubuntu/.nddevice/
status=$?

if [[ $status == 0 ]]; then
	log "tar file successfully unzipped"
else
	log "tar file not unzipped please check...!"

fi
log "Unzipping of CLE_tool_B2.tar.gz tar file is Done"
check_status $(basename $(pwd)) $status
log "=========== End of copy CLE_tools ============"
