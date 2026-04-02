#!/bin/bash

. /home/ubuntu/config/conn_mgr_config.txt

source  /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

CONN_MGR_PATH=/home/ubuntu/.nddevice/latest/service/conn_mgr

#Calling copy funtion from lib

SIERRA_FW_CARRIER_NAME=$(lte_gps_sample_app 'at!impref?' | grep "current carrier name" | awk -F ":" '{print $2}' | tr -cd [a-z][A-Z])
SIERRA_FW_CARRIER_VER=$(lte_gps_sample_app 'at!impref?' | grep "current fw version" | awk -F ":" '{print $2}' | tr -cd [0-9][.])

log "========= Start of copy Sierra FW  =========="

log "unzipping firmwares"
unzip -o firmware_WP7609.zip
status1=$?
if [[ $status1 == 0 ]] ; then log "Unzipped successfully" ; else log "Unzipping not successful. Exiting..."; exit 1 ; fi;
chmod +x firmware_WP7609/*

log "Copying the All Sierra FW files .spk to /etc/vendor"
cp -f firmware_WP7609/*.spk /etc/vendor/
if [[ $status1 == 0 ]] ; then log "Copied FWs successfully" ; else log "Copy FWs not successful. Exiting..."; exit 1 ; fi;

log "Copying all scripts to /usr/bin/"
cp -f fwdldarm64le /usr/bin/
status2=$?
cp -f slqssdk /usr/bin/
status3=$?
cp -f sierra_fw_upgrade_WP7609_*.sh /usr/bin/
status4=$?
cp -f lte_gps_sierra_upgrade_9x*.sh /usr/bin/
status5=$?
if [[ $status2 == 0 ]] && [[ $status3 == 0 ]] && [[ $status4 == 0 ]] && [[ $status5 == 0 ]] ; then log "Copy successfully" ; else log "Copy not successful. Exiting..."; exit 1 ; fi;

#log "Checking FW to install"
log "Present FW - Carrier Name : ${SIERRA_FW_CARRIER_NAME} - Carrier Version : ${SIERRA_FW_CARRIER_VER}"

if [[ "$SIERRA_FW_CARRIER_NAME" == "GENERIC" ]] && [[ "$SIERRA_FW_CARRIER_VER" == "02.18.05.00" ]]; then
	log "firmware already updated so skipping it"
else
    if [[ "$apnName" == "telstra.internet" ]] || [[ "$apnName" = "m2m" ]];then
	    log "Checking For Sierra Module Enumeration"

        count=1
        while [ ! -e "/dev/qcqmi0" ]
        do
            sleep 2;
            log "========= Check qcqmi0 - Count $count ========== "
            lsusbNodes=$(lsusb);
            log "$lsusbNodes"
            log  ""

            if [ $count -eq 30 ]; then
                log "Sierra Modem Not Enumerated. Resetting Lumia..."
                python3 $CONN_MGR_PATH/lumia_reset.py
            fi

            if [ $count -eq 60 ]; then
                log "Sierra Modem Not Enumerated. Failed To Update Firmware "
                exit 1;
            fi

            count=$((count+1))
        done

        log "Existing FW is not matching with expected so proceeding with FW installation of 02.18.05.00 GENERIC FW"
	    log "stopping connection manager service"
	    systemctl stop conn_mgr.service
	    sleep 2
	    log "executing sierra_fw_upgrade_WP7609_02.18.05.00_GENERIC.sh"
	    timeout -t 240 bash /usr/bin/sierra_fw_upgrade_WP7609_02.18.05.00_GENERIC.sh
	    status=$?
	    check_status $(basename $(pwd)) $status
	    log "Loging AT!IMPREF? output after update....."
	    sleep 2
	    lte_gps_sample_app 'at!impref?' >> /home/ubuntu/.nddevice/log/bhcopy.log

     else
        log "As Carrier Is Not Telstra Or Spark, Firmware Update Is Not Required"

     fi
fi

log "========= End of copy Sierra FW  ========="

