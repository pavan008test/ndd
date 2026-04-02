#!/bin/sh
#To return the success or failure status of Sierra firmware upgrade result
#exit 100 ---> Sierra firmware to be upgraded does not exist
#exit 101 ---> Binary 1 /usr/bin/fwdldarm64le does not exist
#exit 102 ---> Binary 2 /usr/bin/slqssdk does not exist
#exit 103 ---> Sierra Firmware to be upgraded does not consist of .spk file
#exit 104 ---> Failed to enter Bootloader mode
#exit 105 ---> Sierra Modem does not exist in Bootloader Mode USB0
#exit 106 ---> More than One USB nodes are detected, Error for Firmware download
#exit 107 ---> USB node not detected
#exit 108 ---> qcqmi0 node is not detected after firmware upgrade
#exit 109 ---> Given firmware version and Current firmware version doesn't match - Failed
#exit 110 ---> Given firmware config, version and current firmware config, version doesn't match -Failed
#exit 0   ---> Sierra Firmware Upgrade Success

echo "Sierra firmware to be upgraded is $1"

FILE1=/usr/bin/fwdldarm64le
FILE2=/usr/bin/slqssdk
TIMEOUT=240

function verify_firmware {
     ############ FIRMWARE VERIFICATION #######################################
                                                                                                 
     ## Given Firmware                                                                                 
                                                                                                        
     Given_Firmware=$(tail -c +285 $(ls $1/*.spk| head -1) | head -c 60)                                                                  
     Given_fw_version=${Given_Firmware:25:11}                                                                                             
     Given_config_name=${Given_Firmware:40:20}                                                                                            
                                                                                                                                        
     ## Current Firmware                                                                                                                     
                                                                                                                                        
     fw_version=$(lte_gps_sample_app 'at!gobiimpref?'| grep "current fw version: "| cut -d ' ' -f10)
     current_fw_version=${fw_version%?}                                                             
                                                                                                 
     config_name=$(lte_gps_sample_app 'at!gobiimpref?'| grep "current config name: " | cut -d ' ' -f9)
     current_config_name=${config_name%?}                                                             
                                                                                                 
                                                                                                 
     if [[ "$current_config_name" = "$Given_config_name"  ]]            
     then                                                                             
         echo "Given firmware config and current Firmware config matches: SUCCESS"                
         if [[ "$Given_fw_version" = "$current_fw_version"  ]]                                            
         then                                                                                             
             echo "Firmware Version matches: SUCCESS"                         
         else                                                  
             echo "Firmware Version does not match: Failed"  
             echo "Given Firmware version: $Given_fw_version"    
             echo "current Firmware version: $current_fw_version"
             return 109                                            
         fi                                                          
     else                                                                    
         echo "Given Firmware and current Firmware does not match Failed"                         
         echo "Given Firmware config name: $Given_config_name Given Fw version: $Given_fw_version"        
         echo "current Firmware config name: $current_config_name current Fw version: $current_fw_version"
         return 110                                                                                         
     fi
     return 0
}

pre_flash_usb_count=$(lsusb 2> /dev/null | grep 1199 | wc -l)                                             
if [[ "$pre_flash_usb_count" -ne 1 ]]                                                      
then 
     echo "Sierra is not connected"
     exit 107
fi
                       
echo "Pre-flash Lumia version check"
verify_firmware $1
if [ $? -eq 0 ]; then
    echo "Same Lumia firmware already exists"
    exit 0
fi
echo "Lumia versions are not matching, proceeding with image upgrade"

echo "IMAGE UPGRADE FROM SIERRA FIRMWARE"

if [ -d "$1" ]
then
	echo "Sierra firmware to be upgraded $1 exist"
else
	echo "Sierra firmware to be upgraded $1 does not exist" >&2
	exit 100
fi

if [ -f "$FILE1" ]
then
	echo "File 1 $FILE1 exist"
else
	echo "File 1 $FILE1 does not exist" >&2
	exit 101
fi

if [ -f "$FILE2" ]
then
	echo "File 2 $FILE2 exist"
else
	echo "File 2 $FILE2 does not exist" >&2
	exit 102
fi

count=$(ls $1/*.spk | wc -l)
if [ $count -ne 0 ]
then
	echo "Sierra firmware to be upgraded $1 consist of .spk file"
else
	echo "Sierra firmware to be upgraded $1 does not consist of .spk file"
	exit 103
fi

lte_gps_sample_app 'AT!BOOTHOLD' >> /dev/null
if [ $? -ne 0 ]
then
	echo "Failed to enter Bootloader mode"
	exit 104
fi

echo "Entering Bootloader mode ..."
loop=1
while [[ $loop -le 50 ]]
do
	sleep 1
	usb_count=$(ls /dev/ttyUSB* 2> /dev/null | wc -l)
	if [[ "$usb_count" -eq 1 ]]
	then
		echo "Only one USB node detected"
		ls /dev/ttyUSB0
		if [ $? -eq 0 ]
		then
			echo "USB0 node is detected"
			break
		else
			echo "USB0 node is not detected, Modem is not in Bootloader mode"
			exit 105
		fi
	fi
	loop=$(expr $loop + 1)
	echo -n "."
done
if [[ "$loop" -ge 50 ]]
then
	if [[ "$usb_count" -ge 1 ]]
	then
		echo "More than One usb nodes are detected, Error for Firmware download"
		exit 106
	else
		echo "USB node not detected"
		exit 107
	fi
fi
sleep 5
echo "LTE_GPS: Download Starts"
(fwdldarm64le -s $FILE2 -d 9x15 -p $1 ) & pid=$!
    (sleep $TIMEOUT && kill -HUP $pid ) 2>/dev/null & watcher=$!
	if wait $pid 2>/dev/null;then
		echo "LTE_GPS Download is done and reset modem for recovery mode $pid"
		pkill -HUP -P $watcher
		wait $watcher
	else
		echo "FAILED in Downloading LTE_GPS $pid"
	fi

echo "qcqmi detection"
for loop in 1 2 3 4 5
do
	sleep 1
	echo -n "."
	ls /dev/qcqmi0 2> /dev/null
	if [ $? -eq 0 ]
	then
		echo "qcqmi0 node is created"
		break
	fi
	loop=$(expr $loop + 1)
done
if [[ "$loop" -ge 5 ]]
then
	echo "qcqmi0 node is not created"
	exit 108
fi

verify_firmware $1
ret_val = $?                                                                                       
if [ $ret_val -ne 0 ]; then                                                                                    
    exit $ret_val                                                                                                
fi 

echo "ND Commands to setup lumia"
lte_gps_sample_app 'AT!GPSSATINFO?'
lte_gps_sample_app 'AT!GPSAUTOSTART?'
lte_gps_sample_app 'AT!GPSAUTOSTART=1,1,250,250,1'
lte_gps_sample_app 'AT!ENTERCND="A710"'
lte_gps_sample_app 'AT!GPSNMEA=1'
lte_gps_sample_app 'AT!GPSNMEACONFIG=1,1'
lte_gps_sample_app 'AT!GPSXTRADATAENABLE=2,3,10,1,24,24'
lte_gps_sample_app 'AT!GPSXTRAINITDNLD'
echo "LTE_GPS SIERRA FIRMWARE UPGRADE IS COMPLETED"
exit 0
