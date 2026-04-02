#!/bin/sh

#script for invoking the conn_mgr service

#Script to kill vvdn_sierra_powerdown binary
if [ -n "$(pgrep sierra_powerdown)" ]; then
    pkill -9 sierra_powerdown;
fi

# kill any existing processes
if [ -n "$(pgrep connection_mgr)" ]; then
    pkill -9 connection_mgr;
fi

if [ -n "$(pgrep slqssdk)" ]; then
    pkill -9 slqssdk;
fi

. /home/ubuntu/config/conn_mgr_config.txt

echo "ConnMgr: Checking for Sierra Module Enumeration "

gpio-app -n 134 -o 1

count=1
while [ ! -e "/dev/qcqmi0" ]
do
    sleep 2
    if [ $count -eq 60 ]; then
        echo "Sierra modem not enumerated. Resetting ..."
        python3 /home/ubuntu/.nddevice/latest/service/conn_mgr/lumia_reset.py
    fi
    count=$((count+1))
done

echo "ConnMgr: Enumeration succeeded , profile & APN name are"

sleep 5
dhcpcd usb0 &

# log if some process is still running
if [ -n "$(pgrep connection_mgr)" ]; then
    echo "connection_mgr process still running"
fi

if [ -n "$(pgrep slqssdk)" ]; then
    echo "slqssdk process still running"
fi

sh /home/ubuntu/.nddevice/latest/service/conn_mgr/force_sierra_online.sh &

echo $profileName
echo $apnName

sudo /home/ubuntu/.nddevice/latest/service/conn_mgr/connection_mgr /home/ubuntu/.nddevice/latest/service/conn_mgr/slqssdk "$profileName" "$apnName"

echo END OF SCRIPT
