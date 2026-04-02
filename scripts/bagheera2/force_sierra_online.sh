#!/bin/sh
#Script to continuously monitor the SIM Status, every 5 minutes
FILE=$1

epoch_time=$(date +%s%3N)
status=$(lte_gps_sample_app 'at!gstatus?' | head -10 | awk ' /Mode:/ {print $5}')
echo "ConnMgr: Force Online Modem Status  at $epoch_time is $status" >>  $1

while [ 1 ]
do
    status=$(lte_gps_sample_app 'at!gstatus?' | head -10 | awk ' /Mode:/ {print $5}')
    if [ $status != "ONLINE" ]
    then
        sleep 3
        lte_gps_sample_app 'at+cfun=1'
        epoch_time=$(date +%s%3N)
        echo "ConnMgr: Moving Modem To ONLINE State at $epoch_time" >> $1
        sleep 3
        status=$(lte_gps_sample_app 'at!gstatus?' | head -10 | awk ' /Mode:/ {print $5}')
        echo "ConnMgr: Modem Status After CFUN=1 $status" >> $1
    fi

    echo "ConnMgr: Modem Check Done - Sleeping" >> $1

    sleep 300
done
