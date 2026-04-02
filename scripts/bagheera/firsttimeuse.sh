#!/bin/bash

uuencode=1
binary=0

Print_Help()
{
    cat << EOH >&2
 $0 bagheera firsttime use script version v:0.0.1

 1) Getting help or info about $0 :
  $0 --help   Print this message
  .......................................................................

  $0 --deviceId <deviceId>  [additional arguments to embedded script]
  with following options (in that order)
  -deviceId | --deviceId <deviceId>    	Driver identification
  -sessionId | --sessionId <sessionId>  Session Id
  -ssid | --ssid <wifi ssid> 		Wifi HotSpot SSID 
  -ssidPassword | --ssidPassword        Wifi SSID password
  -lanecal | --lanecal	 [keep|delete]		laneCal.json to clean on reboot, default delete 
  -saveMP4 | --saveMP4	 [keep|delete]		saveMP4 folder to clean on reboot default delete
  -device  | --deviceType 		[bagheera|device]	specify the device type (default is bagheera)
  -deviceSub | --deviceSubType		[dvt1|dvt2]		specify the device sub (type default is dvt2)
  -vehClass| --vehClass  <vehClass>     [Class1|Class2..8]	used by the analytics engine to determine the vehicle class to pick the correct lane cal parameter (default Class1)
  -profileName | --profileName	[T-MOBILE|JIO]			LTE carrier profile name (Dafault = "T-MOBILE" India = "JIO")
  -apnName | --apnName          [fast.t-mobile.com|jionet]	LTE carrier apn name
  -cloudServer | --cloudServer          [prod|staging]        	default prod 

EOH
}

#delete old certificate
function delete_old_certificate()
{
	echo "Going to delete the default/old certificate"
	#delete autocam .nddevice  bagheera_service service
        sudo rm -rf /home/ubuntu/.nddevice/certificate/certificate.pem.crt 
        sudo rm -rf /home/ubuntu/.nddevice/certificate/private.pem.key
        sudo rm -rf /home/ubuntu/.nddevice/certificate/temp.txt
	echo "Default default/old certificate ..."
}

#Add new files, folder and configuration
function update_old_files_conf()
{
		
	echo "Going to Add new configuration"
	folder_home="/home/ubuntu/.nddevice"
	folder_conf="/home/ubuntu/config"
	folder_saveMP4="/home/iriscli/saveMP4"
	bashrc_file="/home/ubuntu/.bashrc"
	
	#add new configuration

	#Create /home/ubuntu/config folder
	if [ ! -d "$folder_conf" ]; then
		mkdir "$folder_conf"
	fi 

	echo  "Going to update a deviceconfig.ini"
	#Go to /home/ubuntu/config and create nd_config.ini
	#Sample:-

        #       [identity]
        #       deviceId = NDPRAVEEN
        #      sessionId = NDPRAVEEN
	if [ -f "$folder_conf/deviceconfig.ini" ]; then
		rm "$folder_conf/deviceconfig.ini"
	fi

	if [ ! -f "$folder_conf/deviceconfig.ini" ]; then
		touch "$folder_conf/deviceconfig.ini"
	fi

	#identity	
	echo -e "\t[identity]\n:::\tdeviceId = $deviceId\tsessionId = $sessionId \tdeviceType =$deviceType\tdeviceSubType =$deviceSubType\n"
	echo "[identity]" > "$folder_conf/deviceconfig.ini"
	echo "deviceId = $deviceId" >> "$folder_conf/deviceconfig.ini"
	echo "sessionId = $sessionId" >> "$folder_conf/deviceconfig.ini"
	#deviceType
	echo "deviceType = $deviceType">> "$folder_conf/deviceconfig.ini"
	#deviceSubType
	echo "deviceSubType = $deviceSubType">> "$folder_conf/deviceconfig.ini"
	echo -e "\n\n" >> "$folder_conf/deviceconfig.ini"
        #vehicle
	echo -e "\t[vehicle]\nvehClass = $vehClass"
	echo "[vehicle]" >> "$folder_conf/deviceconfig.ini"
	echo "vehClass = $vehClass" >> "$folder_conf/deviceconfig.ini"
	echo -e "\n\n" >> "$folder_conf/deviceconfig.ini"

	
	#cleanup	
	echo -e "\t[cleanup]\nlanecal = $lanecal\nsaveMP4 = $saveMP4"
	echo "[cleanup]" >> "$folder_conf/deviceconfig.ini"
	echo "lanecal = $lanecal" >> "$folder_conf/deviceconfig.ini"
	echo "saveMP4 = $saveMP4" >> "$folder_conf/deviceconfig.ini"

	if [ "$lanecal" == "keep" ]; then
		#if lanecal is keep than we will add a default one to /home/ubuntu/config/
	
		echo -e "Moving laneCal.json to /home/ubuntu/config/ as keep is enabled for it"
		mv /home/ubuntu/.nddevice/laneCal.json /home/ubuntu/config/.

	fi 

	echo "New configuration added successfully"


}

#Add APN config
function add_apn_config_binary()
{
	echo "Adding APN config ..."
	#Apn Configuration
 	echo "profileName=\"$profileName\"" > /home/ubuntu/.nddevice/latest/service/sierra/config.txt
        echo "apnName=\"$apnName\"" >> /home/ubuntu/.nddevice/latest/service/sierra/config.txt
        mkdir -p /home/ubuntu/config/conn_mgr
 	echo "export profileName=\"$profileName\"" > /home/ubuntu/config/conn_mgr/config.txt
        echo "export apnName=\"$apnName\"" >> /home/ubuntu/config/conn_mgr/config.txt
	echo "Adding APN config done"
}


#reboot system
function reboot_system()
{
	echo "Rebooting system ..."

	sudo reboot
}

#change cloud Config
function change_cloud_config()
{
	echo "Changing cloud config ..."
	sed -i -e "s/^.*server.*=.*/server = $cloudServer/" /home/ubuntu/.nddevice/latest/cloudconfig.ini
	echo "Changing cloud config done"
}


#create SSID with wifi
function configure_wifi_ssid()
{
	#connect to wifi
	echo "....................................................."
	echo "Removed existing WiFi connections"
	sudo rm /etc/NetworkManager/system-connections/*
	echo "....................................................."
	echo "Adding wifi configuration ($ssid)...."
	echo "....................................................."
	count=0
	while true
	do
		read -p "        Ensure you have setup the '$ssid' on Nexus 5X with password '$ssidPassword' Press Enter Key  to continue" ch

		count=$((count+1))
		sudo nmcli d wifi connect $ssid password $ssidPassword
		if [ $? -eq 0 ] ; then
			echo -e "WIFI SSID Configuration succeeded"
			break
		fi
		#Error message
		echo -e "Your WIFI SSID '$ssid' on Nexus 5X is not up or Password '$ssidPassword' is wrong."

		if [ $count -gt 3 ]; then 
			echo -e "Disable / Enable wifi on NVIDIA Shield TV, Check Tethering & portable hotspot in Nexus 5X"
		fi
		read -p "        Please check and press enter to continue..." ans
	done
}

#stop bagheera services
function stop_bagheera_services()
{
	echo "STOP Bagheera Service started"
	echo "Going to stop services please add if any more service needs to be added for stop"
	

	FILES="bagheera awsiot circular_buffer uploader speed"
	echo $FILES
	read -p "Press any to contiue or CTRL+C to exit" ans2
	echo $ans2

	for file in $FILES
	do
		#stop service if exists
		if [ -f "/home/ubuntu/.nddevice/latest/service/$file/$file" ]; then
			sudo nd_service.sh -c stop -n $file.service
		fi
	done
	
	echo "STOP Bagheera Service finished..."
}


#default value
deviceId="none"
sessionId="none"
ssid="none"
ssidPassword="none"
lanecal="none"
saveMP4="none"
deviceType="none"
deviceSubType="none"
vehClass="none"
profileName="none"
apnName="none"
cloudServer="none"

#Read deviceID and devicetype, subtype
deviceId="$(sudo sys_tx1read | sed -n 's/^Product serial_num:[[:space:]]*//p')"
if [ "$deviceId" == "" ]; then
	deviceId="none"
fi

sudo /bin/revision_icdc
deviceSubType_t="$(echo $?)"

if [ "$deviceSubType_t" == "0" ]; then
	deviceSubType="dvt1"
fi

if [ "$deviceSubType_t" == "1" ]; then
	deviceSubType="dvt2"
fi

if [ "$deviceSubType_t" == "2" ]; then
	deviceSubType="dvt3"
fi

if ! [ -e /bin/revision_icdc ]; then
	deviceSubType="none"
fi

if [ "$deviceId" != "none" ] && [ "$deviceSubType" != "none" ]
then
	deviceType="bagheera"
fi

echo "AUTO Device Id  = $deviceId"
echo "AUTO deviceType  = $deviceType"
echo "AUTO deviceSubType  = $deviceSubType"

#scan the arguments
while true
do
    case "$1" in
    -h | --help)
	Print_Help
	exit 0
	;;
     -deviceId | --deviceId )
	deviceId="$2"
	shift 2
	;;
     -sessionId | --sessionId )
	sessionId="$2"
	shift 2
	;;
     -ssid | --ssid )
	ssid="$2"
	shift 2
	;;

     -ssidPassword | --ssidPassword)
	ssidPassword="$2"
	shift 2
	;; 

     -lanecal | --lanecal)
	lanecal="$2"
	shift 2
	;; 

     -saveMP4 | --saveMP4)
	saveMP4="$2"
	shift 2
	;; 

     -vehClass | --vehClass)
        vehClass="$2"
        shift 2
        ;;

     -device | --deviceType)
        deviceType="$2"
        shift 2
        ;;

     -deviceSub | --deviceSubType)
        deviceSubType="$2"
        shift 2
        ;;


     -profileName | --profileName)
        profileName="$2"
        shift 2
        ;;

     -apnName | --apnName)
        apnName="$2"
        shift 2
        ;;

     -cloudServer | --cloudServer)
        cloudServer="$2"
        shift 2
        ;;


     *)
	break;;
	
   esac
done

#check if no deviceId then flag the error and error out
if [ "$deviceId" == "none" ]; then
	echo "No Device Id provide exiting ...."
	echo "/bin/sys_tx1read couldnt find Product serial_num"
	Print_Help
	exit;
fi 
echo "Device Id  = $deviceId"

#check if no sessionId then just use the same sessionId as deviceId
if [ "$sessionId" == "none" ]; then
	echo -e "No sessionId is provided \nMaking sessionId same as Device Id = $deviceId"
	sessionId="$deviceId"
fi 
echo "Session Id = $sessionId"

#check if ssid is provided
if [ "$ssid" == "none" ]; then
	echo -e "No wifi ssid is provided \nMaking wifi ssid as = $deviceId"
	ssid="$deviceId"
fi 
echo "ssid = $ssid"

#check if ssid is provided
if [ "$ssidPassword" == "none" ]; then
	echo -e "No wifi password is provided Making wifi ssidPassword as default i.e., 'in37\$12@PA'"
	ssidPassword="in37\$12@PA"
fi 
echo "ssidPassword = $ssidPassword"


#check if lanecal is provided
if [ "$lanecal" == "none" ]; then
	echo -e "No option for lancal is provided making it as none"
	lanecal="none"
fi 
echo "laneCal = $lanecal"

#check if saveMP4 is provided
if [ "$saveMP4" == "none" ]; then
	echo -e "No option for saveMP4 is provided making it as delete"
	saveMP4="delete"
fi 
echo "saveMP4 = $saveMP4"

#check if vehClass is provided 
if [ "$vehClass" == "none" ]; then
	echo -e "No vehClass is provided \nMaking vehClass as default = Class1"
	vehClass="Class1"
fi
echo "vehClass = $vehClass"

#check if deviceType is provided 
if [ "$deviceType" == "none" ]; then
	echo -e "No deviceType is provided \nMaking deviceType as default = bagheera"
	deviceType="bagheera"
fi
echo "deviceType = $deviceType"

#check if deviceSubType is provided 
if [ "$deviceSubType" == "none" ]; then
	echo -e "No deviceSubType is provided \nMaking deviceSubType as default = dvt2"
	deviceSubType="dvt2"
fi
echo "deviceSubType = $deviceSubType"


#check if no profileName then flag the error and error out
if [ "$profileName" == "none" ]; then
        echo "No profileName provided setting default as T-MOBILE ...."
        profileName="T-MOBILE"
fi
echo "profileName = $profileName"

#check if no apnName then flag the error and error out
if [ "$apnName" == "none" ]; then
        echo "No apnName provided setting default as fast.t-mobile.com ...."
        apnName="fast.t-mobile.com"
fi


#check if cloud server name is provided
if [ "$cloudServer" == "none" ]; then
        echo "No cloud Server provided setting default to prod ...."
        cloudServer="prod"
fi

read -p "Press Y|y to contiue ::" ans
if [[ "$ans" == "Y"  ||  "$ans" == "y" ]]; then

	echo "**************STARTING FIRST-TIME CONFIGURATION*********************"		
	#stop all services
	stop_bagheera_services
	#update old config 
	update_old_files_conf
	#add apn configuration
        add_apn_config_binary
	#delete certificate
	delete_old_certificate
	#change cloud config point 
	change_cloud_config
	#reboot system
	reboot_system	
	echo "**************FIRST-TIME CONFIGURATION COMPLETED*********************"		
fi

exit 0

