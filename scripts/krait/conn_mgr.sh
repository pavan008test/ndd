#!/bin/sh
#script for invoking the conn_mgr service

CONN_MGR_DEBUG_PATH=/home/ubuntu/.nddevice/log/conn_mgr_debug/
mkdir -p $CONN_MGR_DEBUG_PATH
epoch_time=$(date +%s%3N)
FILE=$CONN_MGR_DEBUG_PATH/log_${epoch_time}.log
CONN_MGR_PATH=/home/ubuntu/.nddevice/latest/service/conn_mgr

echo "ConnMgr: Started at $epoch_time" >> $FILE

sleep 5;

echo "" >> $FILE;
echo "========= USB Enumerations ========== " >> $FILE;
lsusbNodes=$(lsusb | grep 1199);
echo "$lsusbNodes" >> $FILE;
echo "" >> $FILE;
echo "========= GobiNet Info === ========== " >> $FILE;
gobiNetVersion=$( modinfo GobiNet | grep -i "version:" | head -1);
echo "$gobiNetVersion" >> $FILE;
gobiNetUsage=$(lsmod | grep GobiNet);
echo "$gobiNetUsage"   >> $FILE;
echo "" >> $FILE;
echo "========= GobiSerial Info =========== " >> $FILE;
gobiSerialVersion=$( modinfo GobiSerial | grep -i "version:" | head -1);
echo "$gobiSerialVersion" >> $FILE;
gobiSerialUsage=$(lsmod | grep GobiSerial);
echo "$gobiSerialUsage"   >> $FILE;
echo "===================================== " >> $FILE;
echo "" >> $FILE;

# kill any existing processes
if [ -n "$(pgrep connection_mgr)" ]; then
    pkill -9 connection_mgr;
fi

if [ -n "$(pgrep slqssdk)" ]; then
    pkill -9 slqssdk;
fi

. /home/ubuntu/config/conn_mgr_config.txt

echo "ConnMgr: Checking For Sierra Module Enumeration " >> $FILE

count=1
while [ ! -e "/dev/qcqmi0" ]
do
    sleep 2;
    echo "========= Check qcqmi0 - Count $count ========== " >> $FILE;
    lsusbNodes=$(lsusb | grep 1199);
    echo "$lsusbNodes" >> $FILE;
    echo "" >> $FILE;

    if [ $count -eq 30 ]; then
        echo "ConnMgr: Sierra Modem Not Enumerated. Resetting ..." >> $FILE
        python3 $CONN_MGR_PATH/lumia_reset.py
    fi

    if [ $count -eq 60 ]; then
        echo "ConnMgr: Resetting LDBOS ..." >> $FILE
        echo 1 > /sys/kernel/debug/regulator/soc:rpmh-regulator-ldob7-pm660l_l7/enable
    fi

    if [ $count -eq 90 ]; then
        echo "ConnMgr: Sierra Modem Not Enumerated. forcefully starting conn_mgr service." >> $FILE
        python3 $CONN_MGR_PATH/lumia_reset.py
        break
    fi

    count=$((count+1))
done

if [ -e "/dev/qcqmi0" ]; then
    epoch_time=$(date +%s%3N);
    echo "ConnMgr: Enumeration Succeeded at $epoch_time" >> $FILE
fi

echo "ConnMgr: Profile Name : $profileName" >> $FILE

echo "ConnMgr: APN Name     : $apnName"     >> $FILE

sleep 5
dhcpcd usb0 &

# log if some process is still running
if [ -n "$(pgrep connection_mgr)" ]; then
    echo "ConnMgr: connection_mgr process still running" >> $FILE
fi

if [ -n "$(pgrep slqssdk)" ]; then
    echo "ConnMgr: slqssdk process still running" >> $FILE
fi

sleep 15

echo "ConnMgr: Starting Force Sierra Online Script" >> $FILE

sh $CONN_MGR_PATH/force_sierra_online.sh $FILE &

echo "ConnMgr: Starting Connection Manager Application" >> $FILE

sudo $CONN_MGR_PATH/connection_mgr $CONN_MGR_PATH/slqssdk "$profileName" "$apnName"

#Print if Sierra module Node 1199 is present
echo "ConnMgr: Sierra module status" >> $FILE
lsusbNodes=$(lsusb | grep 1199);
echo "$lsusbNodes" >> $FILE;

echo "ConnMgr: Issuing AT Commands For Debugging" >> $FILE

atcommand=$(lte_gps_sample_app 'ATI');
echo "$atcommand" >> $FILE
echo "" >> $FILE;

atcommand=$(lte_gps_sample_app 'ATI8');
echo "$atcommand" >> $FILE
echo "" >> $FILE;

atcommand=$(lte_gps_sample_app 'AT!IMPREF?');
echo "$atcommand" >> $FILE
echo "" >> $FILE;

atcommand=$(lte_gps_sample_app 'AT!PRIID?');
echo "$atcommand" >> $FILE
echo "" >> $FILE;

atcommand=$(lte_gps_sample_app 'AT!GSTATUS?');
echo "$atcommand" >> $FILE
echo "" >> $FILE;

epoch_time=$(date +%s%3N);
echo "ConnMgr: Connection Manager Exited at $epoch_time" >> $FILE
