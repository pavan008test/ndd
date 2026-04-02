#!/usr/bin/env bash

set -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling copy funtion from lib



log "===============  start of copying libasound ================"
md5sum_lib=$(md5sum libasound.so.2.0.0)
log "checking for libasound if it present"
if [ -f /usr/lib/aarch64-linux-gnu/libasound.so ] && [ -f /usr/lib/aarch64-linux-gnu/libasound.so.2 ] && [ -f /usr/lib/aarch64-linux-gnu/libasound.so.2.0.0 ];then
	log "libasound files are already present at /usr/lib/aarch64-linux-gnu"
	status1=0
	status2=0
	status3=0
	status4=0
else
	log "Missing libasound at /usr/lib/aarch64-linux-gnu so removing and freshly copying"
	rm -f /usr/lib/aarch64-linux-gnu/libasound.so*
	log "Copying libasound.so.2.0.0 at /usr/lib/aarch64-linux-gnu/ and /bin/vendor/"
	copy_file -f libasound.so.2.0.0 -d /usr/lib/aarch64-linux-gnu/ -b /home/ubuntu/.nddevice/backup -m $md5sum_lib -p 755 -o root:root
	status1=$?
	copy_file -f libasound.so.2.0.0 -d /bin/vendor/ -b /home/ubuntu/.nddevice/backup -m $md5sum_lib -p 755 -o root:root
	status2=$?
	log "Creating Links of libasound"
	ln -nfs libasound.so.2.0.0 /usr/lib/aarch64-linux-gnu/libasound.so.2
	status3=$?
	ln -nfs libasound.so.2.0.0 /usr/lib/aarch64-linux-gnu/libasound.so
	status4=$?
fi
if [ $status1  == 0 ] && [ $status2 == 0 ] && [ $status3 == 0 ] && [ $status4 == 0 ]; then
	log "libasound files copied successfully"
else
	log "libasound files copied Not Copied successfully please check the logs....!!!!"
fi
log "=============== End of copy libasound ================"
