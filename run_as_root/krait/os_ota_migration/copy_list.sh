#!/bin/bash -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

function copy
{
   source_file_path=$1
   source_file=$(basename ${source_file_path})
   dest_folder=$2
   [[ -d ${dest_folder} ]] ||  mkdir -p ${dest_folder} 
   if [[ -f ${source_file_path} ]]
   then
		if [[ ! -f ${dest_folder}/${source_file} ]]
		then
			log "copying $source_file_path to ${dest_folder}"
			cp -fp ${source_file_path} ${dest_folder}
			[[ $? == 0 ]] || exit 1
		fi
   else
		if [[ -d ${source_file_path} ]]
		then
			log "copying $source_file_path to ${dest_folder}"
			cp -rfp ${source_file_path} ${dest_folder}
			[[ $? == 0 ]] || exit 1

		fi
		if [[ -f ${dest_folder}/${source_file} || -d ${dest_folder}/${source_file} ]]
		then
			log "File : $source_file_path is already present in $dest_folder"
		else
			log "File : $source_file_path is present no where. So skipping actions"
		fi
   fi
}

function clean
{
   file_to_be_deleted=$1
   if [[ -f $file_to_be_deleted ]]
   then
	rm -f $file_to_be_deleted
   else
	log "No file name : $file_to_be_deleted to delete"
   fi 
}

#copy loop
while read line
do
source=$(echo $line | awk -F "=" '{print $1}')
destination=$(echo $line | awk -F "=" '{print $2}')
copy $source $destination
done < copy_list

