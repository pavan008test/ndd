#!/bin/bash

Print_Help()
{
    cat << EOH >&2
 $0 reboot management script

 1) Getting help or info about $0 :
  $0 --help   Print this message
  .......................................................................

  $0 --time <time to shutdown> 
  -time | --time <time in min>    
  -process | --process <Calling Process>  
  -action | --action <action> 	

EOH
}

#default value
time="none"
process="none"
action="none"
act="-r"

#scan the arguments
while true
do
    case "$1" in
    -h | --help)
	Print_Help
	exit 0
	;;
     -time | --time )
	time="$2"
	shift 2
	;;
     -process | --process )
	process="$2"
	shift 2
	;;
     -action | --action )
	action="$2"
	shift 2
	;;


     *)
	break;;
	
   esac
done


#check if no time then flag the error and error out
if [ "$time" == "none" ]; then
	echo "No time provided, setting default as ...5"
	time="30"
fi 
echo "time  = $time"

#check if process is provided if not then default is cron
if [ "$process" == "none" ]; then
	echo -e "No process is provided \nMaking process as cron"
	process="cron"
fi 
echo "Process = $process"

#check if action is provided
if [ "$action" == "none" ]; then
	echo -e "No action is provided \nMaking action as reboot"
	action="shutdown"
fi 
echo "action = $action"


if [ "$action" == "shutdown" ]; then 
	act="-P"
fi


if [ "$action" == "reboot" ]; then 
	act="-r"
fi

echo "/sbin/shutdown $act $time" 
/sbin/shutdown $act $time
