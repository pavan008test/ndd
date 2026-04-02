#!/bin/sh

#script for invoking the conn_mgr service

# kill any existing processes
if [ -n "$(sudo pgrep connection_mgr)" ]; then
    sudo pkill -9 connection_mgr;
fi

if [ -n "$(sudo pgrep slqssdk)" ]; then
    sudo pkill -9 slqssdk;
fi

. /home/ubuntu/config/conn_mgr_config.txt

value=`sudo md_test 0x7000e5b4 4 | grep -v grep | awk '{print $2}'`
echo "Boot reason is:"
echo $value
if [ "$value" -ne 0 ]; then
    sleep 1
    echo "Resetting sierra modem"
    gpio_test -n 1019 -s 1 > /dev/null
    sleep 5
    gpio_test -n 1019 -s 0 > /dev/null
fi

echo "ConnMgr: Checking for Sierra Module Enumeration "

while [ ! -e "/dev/qcqmi0" ]
do
    sleep 2
done

echo "ConnMgr: Enumeration succeeded , profile & APN name are"

# log if some process is still running
if [ -n "$(sudo pgrep connection_mgr)" ]; then
    echo "connection_mgr process still running"
fi

if [ -n "$(sudo pgrep slqssdk)" ]; then
    echo "slqssdk process still running"
fi

echo $profileName
echo $apnName

sudo /home/ubuntu/.nddevice/latest/service/conn_mgr/connection_mgr /home/ubuntu/.nddevice/latest/service/conn_mgr/slqssdk "$profileName" "$apnName"

echo END OF SCRIPT



