#!/bin/sh

new_version=$(grep -A1 upgrade "/home/ubuntu/.nddevice/nddevice.ini" | awk -F "=" '{print $2}' | tail -1 | tr -d " ") 

export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/home/ubuntu/.nddevice/$new_version/:/home/ubuntu/.nddevice/latest/libs:/nd_lib

OTA_PATH=${1:-ota_temp}

if [ "$OTA_PATH" == "ota_temp" ]; then
	source /home/ubuntu/.nddevice/$OTA_PATH/run_as_root/run_as_root_lib.sh
	LOG=log
else
	LOG=echo
fi

BASE_PATH=/home/ubuntu/bin/obd_fw/

$LOG "============Start of obd_FW_update script================="
$LOG "Creating the status files app1 and app2"
touch /home/ubuntu/.nddevice/$OTA_PATH/app1.txt
touch /home/ubuntu/.nddevice/$OTA_PATH/app2.txt

$LOG "Executing the obd_fm_upgrade script from bin folder for APP1"
$BASE_PATH/obd_fm_upgrade.sh --updatePath $BASE_PATH/obd/update/App1.s19 --stablePath $BASE_PATH/obd/stable/App1.s19 --partitionId 35 --otaPath $OTA_PATH >>/home/ubuntu/.nddevice/log/bhcopy.log

val1=$?

$LOG "Recording the run status of APP1 script to app1.txt"
if [ "$OTA_PATH" == "ota_temp" ]; then
    echo "status $val1" > /home/ubuntu/.nddevice/$OTA_PATH/app1.txt
else
    echo "status $val1"
fi

$LOG "Executing the obd_fm_upgrade script from bin folder for APP2"
$BASE_PATH/obd_fm_upgrade.sh --updatePath $BASE_PATH/obd/update/App2.s19 --stablePath $BASE_PATH/obd/stable/App2.s19 --partitionId 36 --otaPath $OTA_PATH >>/home/ubuntu/.nddevice/log/bhcopy.log

val2=$?

$LOG "Recording the run status of APP2 script to app2.txt"
if [ "$OTA_PATH" == "ota_temp" ]; then
    echo "status $val2" > /home/ubuntu/.nddevice/$OTA_PATH/app2.txt
else
    echo "status $val2"
fi

$LOG "============End of obd_FW_update script================="

