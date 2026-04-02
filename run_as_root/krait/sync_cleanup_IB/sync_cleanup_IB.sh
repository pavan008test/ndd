#!/usr/bin/env bash 

set -e 

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#####Syncing IB file to CB 

log "Collecting file present in internal_buffer folder"
if [[ -d /home/iriscli/internal_buff/ ]] ; then
    IB_FILES=$(ls /home/iriscli/internal_buff/)
else
    log "internal_buffer directory is not present @ /home/iriscli/"
fi
if [[ ${#IB_FILES[@]} != 0 ]]; then
	for file in ${IB_FILES[@]}; do
		log "Checking $file present in sdcard"
		if [[ -f /data/nd_files/nd_sdcard/$file ]]; then
			ib_size=$(du -b /home/iriscli/internal_buff/$file | awk -F " " '{print $1}')
			cb_size=$(du -b /data/nd_files/nd_sdcard/$file | awk -F " " '{print $1}')
			log "Checking size of file"
			if [[ $ib_size -gt $cb_size ]]; then
				log "Copying $file from IB to sdcard"
				cp -f /home/iriscli/internal_buff/$file /data/nd_files/nd_sdcard/$file
				sync /data/nd_files/nd_sdcard/$file
			else
				log "File is exists with same size. So skip copy of $file"
			fi
		else
			log "File is not present. Copying to sdcard"
			cp -f /home/iriscli/internal_buff/$file /data/nd_files/nd_sdcard/$file
			sync /data/nd_files/nd_sdcard/$file
		fi
	done
else
	log "No file in IB. Doing nothing"
fi

#####Deleting IB folder 
log "Removing internal_buff folder after syncing IB"
#deleting the internal_buff folder
rm -rf /home/iriscli/internal_buff

#####Deleting IB db file
log "Removing internal_buffer.db from /data/nd_files/db/"
rm -f /data/nd_files/db/internal_buffer.db

#####Sync files folder to sdcard and remove it
#log "Checking the /home/iriscli/files/ folder contents"
#if [ -d /home/iriscli/files/ ]; then
#    log "files folder present @ /home/iriscli/files/, copying contents to sdcard"
#    cp -r /home/iriscli/files/ /data/nd_files/nd_sdcard/files/
#    rm -rf /home/iriscli/files
#else
#    log "No directory - /home/iriscli/files/"
#fi

#####Move /media/sdcard/files/* to /data/nd_files/nd_sdcard and remove it
log "Checking the /data/nd_files/nd_sdcard/files/ folder contents"
if [ -d /data/nd_files/nd_sdcard/files/ ]; then
    if [[ -n $(ls /data/nd_files/nd_sdcard/files/*) ]]; then
        log "files folder present @ /data/nd_files/nd_sdcard/files/, copying contents to /home/iriscli/files/"
        if [[ ! -d /home/iriscli/files/ ]]; then
            log "Directory /home/iriscli/files/ is not present, creating..."
            mkdir -p /home/iriscli/files/
        fi
        cp -rf /data/nd_files/nd_sdcard/files/* /home/iriscli/files/
        sync /home/iriscli/files
        rm -rf /data/nd_files/nd_sdcard/files
    else
	log "No files present in /data/nd_files/nd_sdcard/files/, removing it"
        rm -rf /data/nd_files/nd_sdcard/files
    fi
else
    log "No directory - /data/nd_files/nd_sdcard/files/"
fi

#####Deleting IB log file/directory
log "Checking contents of /home/ubuntu/.nddevice/log/internal_buff/ directory"
if [[ -d /home/ubuntu/.nddevice/log/internal_buff ]]; then
	if [[ -n $(ls /home/ubuntu/.nddevice/log/internal_buff/) ]] ; then
		log "internal_buff logs found @ /home/ubuntu/.nddevice/log/, copying files : $(ls /home/ubuntu/.nddevice/log/internal_buff/) to circ_buff..."
	   	mv /home/ubuntu/.nddevice/log/internal_buff/*.log /home/ubuntu/.nddevice/log/circ_buff/
    		if [ $? = 0 ]; then
        		log "internal_buff log copied successfully to circ_buff. Taking action to clean internal_buff..."
        		rmdir /home/ubuntu/.nddevice/log/internal_buff
        	else
        		log "internal_buff logs moving to cb is not successful. Exiting..."
        		exit 1
        	fi
    	else
        	log "internal_buff folder does not have logs. Deleting folder"
        	rm -rf /home/ubuntu/.nddevice/log/internal_buff
    	fi
else
    log "internal_buff logs folder is not present. so skipping the ib log cleanup actions..."
fi

