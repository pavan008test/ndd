#!/usr/bin/env bash

#Function for formatting logs
function log {

	if [ "$PARENT_SCRIPT" == "run_as_root" ]
	then
       echo -n "`date +%Y-%m-%d` `date +"%T,%3N"` - __run_as_root__ - INFO - ">> /home/ubuntu/.nddevice/log/bhcopy.log 2>&1
       echo $1 >> /home/ubuntu/.nddevice/log/bhcopy.log 2>&1
   elif [ "$PARENT_SCRIPT" == "update_recovery" ]
   	then
   		echo -n "`date +%Y-%m-%d` `date +"%T,%3N"` - __update_recovery__ - INFO - ">> /home/ubuntu/.nddevice/log/bhcopy.log 2>&1
    	echo $1 >> /home/ubuntu/.nddevice/log/bhcopy.log 2>&1
    fi
}


function check_status {
	SCRIPT=$1
	STATUS=$2
	if [[ $2 -eq 0 ]]
	then 
		if [ "$PARENT_SCRIPT" == "run_as_root" ]
		then
			if ! grep -q "$SCRIPT" /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_status
			then
        			echo "$SCRIPT" >>/home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_status
				log "$SCRIPT is successfull"
			fi

   		elif [ "$PARENT_SCRIPT" == "update_recovery" ]
   		then
   			echo "$SCRIPT" >>/home/ubuntu/.nddevice/ota_temp/run_as_root/update_recovery_status
			log "$SCRIPT is successfull"
    		fi
	
	else
		if [ "$PARENT_SCRIPT" == "run_as_root" ]
		then    
			if ! grep -q "$SCRIPT" /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_status
                        then
				echo "$SCRIPT" >>/home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_status
			fi
        		echo "run_as_root 1" >>/home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_status
			log "$SCRIPT is not executed properly. Check the logs for more info. Exiting the OTA update."
			exit 1
   		elif [ "$PARENT_SCRIPT" == "update_recovery" ]
   		then
   			echo "$SCRIPT" >>/home/ubuntu/.nddevice/ota_temp/run_as_root/update_recovery_status
   			echo "update_recovery 1" >>/home/ubuntu/.nddevice/ota_temp/run_as_root/update_recovery_status
			log "$SCRIPT is not executed properly. Check the logs for more info. Exiting the OTA update."
    	fi
	
	fi
}

function check_md5sum {

	FILE_PATH=$1
	REF_MDSUM=$2
	log "Checking the md5sum of $FILE_PATH"
	mdsum=`md5sum $FILE_PATH |awk '{print $1}'`
	log "md5sum : $mdsum"
	if [ $mdsum != "$REF_MDSUM" ]
		then
			log "md5sum not matched. Take action"			
			return 1
		else
			log "md5sum is matched Successfully"
			return 0
	fi
}

function backup {
	original_file=$1
	backuppath=$2
	file=`basename $original_file`
	log "checking $original_file is present or not"
	if [ -s $original_file ]
	then
		log "copying $original_file to backup path : $backuppath"
		cp -rf --preserve $original_file $backuppath
		return $?
		
	else
		log "File is not present in the dest path $original_file to take backup"
	fi
}

function rollback {
	backpath=$1
	destpath=$2
	filename=`basename $backpath`
	log "checking file is present is backup path"
	if [ -s $backpath ]
	then
		log "copying the $filename from $backpath to $destpath"
		cp -f --preserve $backpath $destpath
		status5=$?
		if [ $status5 == 0 ]
		then 
			log "File is rolled back properly"
			return 0
		else
			log "File is not rolled back properly"
			return 1
		fi
	else
		log "$filename is not present in the backup path."
		
	fi
}


function execute_script {
	SCRIPT_NAME=$1
	Default_script_path=`pwd`
	bash $Default_script_path/$1
	status=$?
	return $status
}

function execute_command {
	command=$@
	log "======Start of execute_command======"
	log "Executing the command : $command"
	$command
	status7=$?
	return $status7
}
function execute_command_sync {
	command=$@
	log "======Start of execute_command_sync======"
	log "Executing the command : $command"
	sync
	$command
	status7=$?
	sync
	return $status7
}

function copy_file {
	default_source_path=`pwd`
	command_mode=""
	file_name=""
	dest_path=""
	backup_path=""
	mdsum=""
	permission=""
	ownership=""
	while [ $# -gt 0 ]
	do
		case $1 in
			-c) command_mode=$2 ; shift;; 
			-f) file_name=$2 ; shift;;
			-d) dest_path=$2 ; shift;;
			-b) backup_path=$2 ; shift;;
			-m) mdsum=$2 ; shift;;
			-p) permission=$2 ; shift;;
			-o) ownership=$2 ; shift;;
			*) echo "odd parameter $1" ;;
			esac
			shift
	done

	if [ -z $command_mode ] ; then
		command_mode=copy
	fi

	log "============Start of copy_file $file_name============="
	log "Checking the file in source path"
	if [ -s $default_source_path/$file_name ]
	then 
		log "$file_name is present"
		if [[ -n $(echo $file_name | grep \/) ]]; then
			dest_filename=$(basename $file_name)
		else
			dest_filename=$file_name
		fi
	else
		log "$file_name is not present in the default source path: $default_source_path "
		return 1
	fi
	
	log "Checking the destination directory"
	if [ -d $dest_path ]
	then 
		log "Dest path is already exist"
	else
		log "Dest path is not present"
		log "Creating the directory $dest_path"
		mkdir -p $dest_path
	fi

	if [ -n "$backup_path" ]
	then 
		log "Checking the backup directory"
		if [ -d $backup_path ]
		then 
			log "backup path is already exist"
		else
			log "Back path is not present"
			log "Creating the directory $backup_path"
			mkdir -p $backup_path
		fi

		log "Taking the backup of $dest_path/$dest_filename"
		backup "$dest_path/$dest_filename" "$backup_path"
		if [ $? == 0 ]
		then 
			log "backup is successful/NotApplicable"
		else
			log "backup is not successful. Stopping the OTA update"
			return 1
		fi
	else
		log "There is no backup path provided. Proceed with your own risk"
	fi

    if [ -n "$permission" ]
	then
		log "Changing the permission and ownership of the $default_source_path/$file_name"
		log "Changing $file_name permissions to $permission"
		chmod $permission $default_source_path/$file_name
			if [ $? == 0 ]
			then
				log "permission are changed"
			else
				log "permission are not changed"
			return 1
			fi
	else
		log "No permission has been given"
	fi

	if [ -n "$ownership" ]
	then
		log "Changing $file_name ownership to $ownership"
		chown $ownership $default_source_path/$file_name
		if [ $? == 0 ]
		then
			log "ownership is changed"
		else
			log "ownership is not changed"
		return 1
		fi
	else
		log "ownership is not provided it will copy as existing"
	fi


	log "$command_mode the file from $default_source_path/$file_name to $dest_path"
	if [ $command_mode == "copy" ] ; then
	cp -f --preserve $default_source_path/$file_name $dest_path
	 elif [ $command_mode == "move" ] ; then
	mv -f $default_source_path/$file_name $dest_path
        fi
	sync $dest_path/$dest_filename
	log "$command_mode is done. Checking for md5sum of the new file"

	if [ -n "$mdsum" ]
	then
	check_md5sum "$dest_path/$dest_filename" "$mdsum"
		if [ $? == 0 ]
		then
			log "$command_mode is successful"
		else
			log "$command_mode is not successful. Rolling back the changes"
			rollback "$backup_path/$file_name" "$dest_path"
			return 1
		fi
	else
		log "No md5sum has been given"
	fi

	log "Copying $file_name is successfully completed"
	log "================End of copy_file $file_name==========="

}

#Shutdown command
function schedule_shutdown {
log "================START of run_as_root SCRIPT==============="
chown ubuntu:ubuntu /home/ubuntu/.nddevice/log/bhcopy.log
log "Command to schedule the shotdown in 30 min"
shutdown -c
status1=$?
shutdown -r 60
status2=$?
if [[ $status1 == 0 && $status2 == 0 ]]
then 
log "Reboot has been initiated properly. System will reboot in 30 min if anything goes wrong"
else
log "Reboot was not initiated properly. Check the service logs"
log "OTA-update will fail due to shutdown command has not properly executed. Exiting status 1. check the logs"
echo "run_as_root 1" >/home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_status
exit 1
fi
log "==========execution continues with other copies and scripts=========="
}

function end_of_run {

	log "This is the end of the script. Status is written to ota_temp/run_as_root/run_as_root_status"
	echo "run_as_root 0" >>/home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_status
	log "==========END of run_as_root_script=========="
}

function end_of_recovery {

	log "This is the end of the script. Status is written to ota_temp/run_as_root/update_recovery_status"
	echo "update_recovery 0" >>/home/ubuntu/.nddevice/ota_temp/run_as_root/update_recovery_status
	log "==========END of update_recovery_status_script=========="
}

function create_link_to_nd_config() {
    log "==================Creating link to nd_config.ini================="
    ND_DEVICE_FILE_PATH="/home/ubuntu/.nddevice/nddevice.ini"
    UPGRADE_VERSION=$(awk -F '=' '/^\[upgrade\]/{found=1; next} found && $1 ~ /nddevice/ {gsub(/ /, "", $2); print $2; exit}' "$ND_DEVICE_FILE_PATH")
    GEO_FILE_PATH="/home/ubuntu/.nddevice/geo.ini"
    ND_CONFIG_PATH="/home/ubuntu/.nddevice/${UPGRADE_VERSION}/nd_config.ini"
    if [[ -f $GEO_FILE_PATH ]]; then
	#Extract the country from geo.ini
        country=$(awk -F '=' '/^\[present_location\]/{found=1; next} found && $1 ~ /country/ {gsub(/ /, "", $2); print $2; exit}' "$GEO_FILE_PATH")
	if [[ -n "$country" && -f "/home/ubuntu/.nddevice/${UPGRADE_VERSION}/nd_config_${country}.ini" ]]; then
            ND_CONFIG_COUNTRY_FILE="nd_config_${country}.ini"
            if [[ -L "$ND_CONFIG_PATH" && "$(readlink "$ND_CONFIG_PATH")" == "$ND_CONFIG_COUNTRY_FILE" ]]; then
                log "nd_config.ini is already correctly linked to $(readlink "$ND_CONFIG_PATH")"
            else
                ln -sf "$ND_CONFIG_COUNTRY_FILE" "$ND_CONFIG_PATH"
                log "Linked nd_config.ini to $(readlink "$ND_CONFIG_PATH")"
            fi
        else
            log "Country not found in geo.ini or there is no corresponding nd_config_${country}.ini, falling back to default link as : $(readlink "$ND_CONFIG_PATH")"
        fi
    else
        log "geo.ini not found, using default link as : $(readlink $ND_CONFIG_PATH)"
    fi
	log "==================Link to nd_config.ini creation completed================="
}
