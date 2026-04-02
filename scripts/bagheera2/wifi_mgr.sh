#!/bin/sh

#SC7 wakeup require unbind and bind of the wifi device
#For Wi-Fi

#this sleep is to compensate the ExecStartPre removal in service file
sleep 5

# the below changes are added because wifi hotspot creation fails due to the delay added
# in ntdi_bag2_startup : BGR2-917
wait_count=0
while [ ! -f /dev/shm/bt_firmware.done ];
do
    wait_count=$((wait_count+1))
    if [ $wait_count -gt 30 ]; then
        echo "reached timeout while waiting for /dev/shm/bt_firmware.done"
        break;
    fi
    sleep 1;
    echo "wait_count "$wait_count " for service wifi_mgr on bt_firmware.done"
done

echo -n "3440000.sdhci" > /sys/bus/platform/drivers/sdhci-tegra/bind

count=0
wifi_working=0
while [ $count -lt 5 ]
do
    ifconfig wlan0 2>/dev/null | head -1 | grep -q UP
    if [ $? -ne 0 ]; then
        echo "wlan0 Interface Not Found/Inactive - Forcing wlan0 Interface UP"
        sudo nmcli radio wifi off
        sleep 1
        sudo nmcli radio wifi on
        sleep 1
        sudo ifconfig wlan0 up
        sleep 5
    else
        wifi_working=1
        echo "wifi interface is up !!!!!!!!!"
        break
    fi
    count=$((count+1))
done

if [ $wifi_working -eq 0 ]; then
    echo "Network Manager Failed To Bring Up wifi Interface - Stoping wifi_mgr Service Till Next Reboot"
    sudo systemctl stop wifi_mgr
    exit 1
fi

sudo /home/ubuntu/.nddevice/latest/service/wifi_mgr/wifi_mgr
echo END OF SCRIPT
