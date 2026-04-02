#!/usr/bin/env bash
#########################################################################
#
# Script to manage service
#	1. install service
#	2. clean  service
#	3. start service
#	4. stop service
#	5. restart service
#	6. status service
#
#########################################################################


# Prints help message
function print_help {
  cat << EOH >&2
  Description:
    $0 script [OPTION]

  OPTIONS:
    -c | --command	<action>	Command to perform (install|clean|start|stop|restart|disbale) (default insatll)
    -f | --file 	<filename with service list seprated by newline>
    -n | --name 	<name of service>
    -p | --path 	<path of service location>
EOH
}



#fuction to perform action for  a service
function service_action()
{
        act=$1
	name=$2
	echo "Doing action $act  for SERVICE $name"

    	case "${act}" in
    		install)
		echo -e "\tACTION = $act Service $name" 

		#write actual script to perform it
		echo -e "\tsudo systemctl stop $name.service"
		sudo systemctl stop $name.service
 
		echo -e "\tsudo rm /etc/systemd/system/$name.service"
		sudo rm /etc/systemd/system/$name.service

		echo -e "\tsudo cp $name.service /etc/systemd/system/"
		sudo cp $path/$name/$name.service /etc/systemd/system/

		echo -e "\tsudo systemctl enable /etc/systemd/system/$name.service"
		sudo systemctl enable /etc/systemd/system/$name.service
		
		#echo -e "\tsudo systemctl start $name.service"
		#sudo systemctl start $name.service

		#echo -e "\tsudo systemctl status $name.service"
		#sudo systemctl status $name.service
		#sudo systemctl stop $name.service
		;;

     		clean)
		echo -e "\tACTION = $act Service $name" 
		echo -e "\tsudo systemctl stop $name.service"
		#sudo systemctl stop $name.service
 
		echo -e "\tsudo rm /etc/systemd/system/$name.service"
		sudo rm /etc/systemd/system/$name.service
		;;

     		start)
		echo -e "\tACTION = $act Service $name" 
		echo -e "\tsudo systemctl start $name.service"
		sudo systemctl start $name.service
		;;

     		stop)
		echo -e "\tACTION = $act Service $name" 
		echo -e "\tsudo systemctl stop $name.service"
		sudo systemctl stop $name.service
		;;

		disable)
		echo -e "\tACTION = $act Service $name"

		echo -e "\tsudo systemctl disable $name.service"
		sudo systemctl disable $name.service
		;;

     		restart)
		echo -e "\tACTION = $act Service $name" 
		echo -e "\tsudo systemctl restart $name.service"
		#sudo systemctl stop $restart.service
		;;

     		status)
		echo -e "\tACTION = $act Service $name" 
		echo -e "\tsudo systemctl status $name.service"
		sudo systemctl status $name.service
		;;

                daemon-reload)
                echo -e "\tACTION = $act Service $name"
                echo -e "\tsudo systemctl $act"
                sudo systemctl daemon-reload
                ;;

    		*)
		;;
	
	esac
	return;
	

}

#function to read service list and perform required action
function perform_action()
{
	act=$1
  	file=$2
  	name=$3

	echo " -----------------------START :::perform_action:::-------------------"
	echo "Action = $act , FileName = $file, Name= $name "
	if [ $file != "none" ]
	then
		count=0	
		while read line; do    
			act=$1
			count=$((count+1));
    			echo -e "  Count = $count Line = $line "  
			service_action $act $line
		done < $file
	else
		service_action $act $name
	fi
	echo " -----------------------END :::perform_action:::-------------------"
 


}


#main program
#default value
action="none"
path="none"
fileName="none"
name="none"
#scan the arguments 
while true
do
    case "$1" in
    -h | --help)
	Print_Help
	exit 0
	;;
     -c | --command )
	action="$2"
	shift 2
	;;
     -f | --file ) 
	fileName="$2"
	shift 2
	;; 
     -n | --name )
	name="$2"
	shift 2
	;; 

     -p | --path )
	path="$2"
	shift 2
	;; 

     *)
	break;;
	
   esac
done

#check if action is provided else make it install by default
if [ "$action" == "none" ]; then
	echo "No action provided making default as install ...."
 	action="install"
fi 

#check if filename exists if provided
if [ "$fileName" == "none" ]; then
	echo -e "No service filename is provided ...."
else
	if [ -f "$fileName" ]
	then
		echo "$fileName present"
	else
		echo "$fileName not present. Exiting ...."
		exit -1;
	fi 
fi
#check if path is not   provided
if [ "$path" == "none" ]; then
	echo -e "No Name is provided ...."
      	path="/home/ubuntu/.ndevice/bootstrap/service/"
fi

#check if name is provided
if [ "$name" == "none" ]; then
	echo -e "No service name is provided ...."
fi 

if [ "$name" == "none" ] && [ "$fileName" == "none" ]; then
	echo -e "No service name or filename for services are provided. Exiting ...."
	exit -2;
fi


perform_action $action $fileName $name
