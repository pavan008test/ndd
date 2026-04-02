#!/bin/sh

cnt=0

while [ 1 ]
do

ifconfig | grep wlan0;

if [ $? -eq 0 ]
then
    echo "wlan0 - Interface Found"
    break;
else
    if [ $cnt -lt 24 ]; then
        echo "wlan0 - Interface Not Found. Retrying in 5 seconds..."
        cnt=$((cnt+1))
    else
        echo "wlan0 - Interface Not Found after $cnt tries."
        break
    fi
fi
sleep 5
done

/home/ubuntu/.nddevice/latest/service/wifi_mgr/wifi_mgr

echo END OF SCRIPT



