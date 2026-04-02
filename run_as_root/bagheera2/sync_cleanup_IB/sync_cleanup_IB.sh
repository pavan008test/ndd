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
		if [[ -f /media/data/nd_sdcard/$file ]]; then
			ib_size=$(du -b /home/iriscli/internal_buff/$file | awk -F " " '{print $1}')
			cb_size=$(du -b /media/data/nd_sdcard/$file | awk -F " " '{print $1}')
			log "Checking size of file"
			if [[ $ib_size -gt $cb_size ]]; then
				log "Copying $file from IB to sdcard"
				cp -f /home/iriscli/internal_buff/$file /media/data/nd_sdcard/$file
				sync /media/data/nd_sdcard/$file
			else
				log "File is exists with same size. So skip copy of $file"
			fi
		else
			log "File is not present. Copying to sdcard"
			cp -f /home/iriscli/internal_buff/$file /media/data/nd_sdcard/$file
			sync /media/data/nd_sdcard/$file
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
rm -f /home/ubuntu/.nddevice/internal_buffer.db

### Commented the code which is not usable in B2

#####Sync files folder to sdcard and remove it
#log "Checking the /home/iriscli/files/ folder contents"
#if [ -d /home/iriscli/files/ ]; then
#    log "files folder present @ /home/iriscli/files/, copying contents to sdcard"
#    cp -r /home/iriscli/files/ /media/SdCard/files/
#    rm -rf /home/iriscli/files
#else
#    log "No directory - /home/iriscli/files/"
#fi

#####Move /media/sdcard/files/* to /media/SdCard and remove it
#log "Checking the /media/data/nd_sdcard/files/ folder contents"
#if [ -d /media/data/nd_sdcard/files/ ]; then
#    if [[ -n $(ls /media/data/nd_sdcard/files/*) ]]; then
#        log "files folder present @ /media/data/nd_sdcard/files/, copying contents to /home/iriscli/files/"
#        if [[ ! -d /home/iriscli/files/ ]]; then
#            log "Directory /home/iriscli/files/ is not present, creating..."
#            mkdir -p /home/iriscli/files/
#        fi
#        cp -rf /media/data/nd_sdcard/* /home/iriscli/files/
#        sync /home/iriscli/files
#        rm -rf /media/data/nd_sdcard/files
#    else
#	log "No files present in /media/data/nd_sdcard/, removing it"
#        rm -rf /media/data/nd_sdcard/files
#    fi
#else
#    log "No directory - /media/data/nd_sdcard/files/"
#fi

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

#####Deleting Transcoder log file/directory
log "Checking contents of /home/ubuntu/.nddevice/log/transcoder/ directory"
if [[ -d /home/ubuntu/.nddevice/log/transcoder ]]; then
        if [[ -n $(ls /home/ubuntu/.nddevice/log/transcoder/) ]] ; then
                log "transcoder logs found @ /home/ubuntu/.nddevice/log/, copying files : $(ls /home/ubuntu/.nddevice/log/transcoder/) to circ_buff..."
                mv /home/ubuntu/.nddevice/log/transcoder/*.log /home/ubuntu/.nddevice/log/circ_buff/
                if [ $? = 0 ]; then
                        log "transcoder log copied successfully to circ_buff. Taking action to clean transcoder..."
                        rmdir /home/ubuntu/.nddevice/log/transcoder
                else
                        log "transcoder logs moving to cb is not successful. Exiting..."
                        exit 1
                fi
        else
                log "transcoder folder does not have logs. Deleting folder"
                rm -rf /home/ubuntu/.nddevice/log/transcoder
        fi
else
    log "transcoder logs folder is not present. so skipping the transcoder log cleanup actions..."
fi

### Removing the cron entries of cron clean ib scripts
log "Remoing the cron entry for clean_ib script call"

if [[ -n $(sudo crontab -l | grep clean_ib.sh) ]]
then
    sudo crontab -l | sed '/clean_ib.sh/d' | sudo crontab -
    status="${PIPESTATUS[0]} ${PIPESTATUS[1]} ${PIPESTATUS[2]}"
    sync
    [ "$status" == "0 0 0" ]
    log "Commands are successful to remove clean_ib.sh from crontab"
else
    log "Clean_ib script is not present in crontab. It would have already removed"
fi

