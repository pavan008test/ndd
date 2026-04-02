#!/bin/bash 

function log 
{
    echo "`date +%Y-%m-%d` `date +"%T,%3N"` - onetime_service - INFO - $1">> /home/ubuntu/.nddevice/log/bhcopy.log 2>&1
}

function clean
{
   file_to_be_deleted=$1
   if [[ -f $file_to_be_deleted ]]
   then
        log "Removing the file : $file_to_be_deleted"
        rm -f $file_to_be_deleted
   elif [[ -d $file_to_be_deleted ]]; then
        log "Removing the folder : $file_to_be_deleted"
        rm -rf $file_to_be_deleted
   else
        log "No such file or folder : $file_to_be_deleted to remove"
   fi 
}

#copy loop
log "############# Start of onetime_service ##############"

clean_list_file=$(dirname $(realpath $0))/clean_list
log " Started cleaning up the file mentioned in the clean_list"
while read line
do
  for file in $line ; do
    clean $file
  done
done < $clean_list_file

log "############# End of the onetime_service ############"

log "+++++++ Disabling the onetime_service +++++++"
systemctl disable onetime_service

log "@@@@@@@@@@@ Exiting @@@@@@@@@@@@"
exit 0

