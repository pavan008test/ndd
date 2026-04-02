#!/bin/sh

#SC7 wakeup require unbind and bind of the wifi device
#For Wi-Fi

#this sleep is to compensate the ExecStartPre removal in service file
sleep 5
# the below changes are added because wifi hotspot creation fails due to the delay added
# in ntdi_bag2_startup : BGR2-917
wait_count=0
while [ ! -f /dev/shm/ntdi_bag3.done ];
do
    wait_count=$((wait_count+1))
    if [ $wait_count -gt 30 ]; then
        echo "reached timeout while waiting for /dev/shm/ntdi_bag3.done"
        break;
    fi
    sleep 1;
    echo "wait_count "$wait_count " for service wifi_mgr on ntdi_bag3 startup"
done

/bin/vendor/wifi_device_bind.sh bind

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


#-------------To reinit wifi if scan result file is empty----------------
AZUREWAVE_MODULE=0
REALTEK_MODULE=1
UNKNOWN_MODULE=2
WIFI_BT_MODULE=1
AZUREWAVE_WIFI_BT_MODULE_STR="Broadcom"
REALTEK_WIFI_BT_MODULE_STR="Realtek"

check_wifi_bt_module() {
    # Checks to See AzureWave or RealTek Wifi/BT module
    lspci | grep -q $AZUREWAVE_WIFI_BT_MODULE_STR
    if [ $? -eq 0 ]; then
        WIFI_BT_MODULE=$AZUREWAVE_MODULE
        echo "Azurewave Wifi/BT module is detected."
    else
        # Check if RealTek Module is present
        lspci | grep -q $REALTEK_WIFI_BT_MODULE_STR
        if [ $? -eq 0 ]; then
            WIFI_BT_MODULE=$REALTEK_MODULE
            echo "RealTek Wifi module is detected."
        else
            WIFI_BT_MODULE=$UNKNOWN_MODULE
            echo "Neither Azurewave nor RealTek Wifi is detected."
        fi
    fi
}

reload_azurewave_wifi_drivers() {
    # Removed the drivers
    rmmod brcmfmac brcmutil cfg80211 compat
    sleep 5
    # insmod wifi drivers
    insmod /lib/modules/misc/compat.ko
    insmod /lib/modules/misc/cfg80211.ko
    insmod /lib/modules/misc/brcmutil.ko
    insmod /lib/modules/misc/brcmfmac.ko debug=0x100000
    sleep 10
}

restart_networkmanager() {
    systemctl start NetworkManager
    sleep 5
    nmcli radio wifi off
    sleep 1
    nmcli radio wifi on
    sleep 1
    rfkill unblock wlan
    sleep 1
    ifconfig wlan0 down
    sleep 1
    ifconfig wlan0 up
    sleep 5
}

monitor_reinit_file() {
    while true; do
        if [ -f /dev/shm/wifi_reinit ]; then
            echo "/dev/shm/wifi_reinit file found. Reinitializing Wi-Fi"

            # Call the functions to reinitialize Wi-Fi
            check_wifi_bt_module
            if [ $WIFI_BT_MODULE -eq $AZUREWAVE_MODULE ]; then
                systemctl stop NetworkManager
                sleep 2
                reload_azurewave_wifi_drivers
                restart_networkmanager
            else
                echo "To be done for non Azurewave modules"
            fi

            sudo rm -f /dev/shm/wifi_reinit
            sudo rm /etc/NetworkManager/system-connections/*
            kill -TERM $$
        fi

        sleep 1
    done
}


monitor_reinit_file &
#-----------------------------------------

#-------------To reinit wifi if scan result file is empty----------------
AZUREWAVE_MODULE=0
REALTEK_MODULE=1
UNKNOWN_MODULE=2
WIFI_BT_MODULE=1
AZUREWAVE_WIFI_BT_MODULE_STR="Broadcom"
REALTEK_WIFI_BT_MODULE_STR="Realtek"

check_wifi_bt_module() {
    # Checks to See AzureWave or RealTek Wifi/BT module
    lspci | grep -q $AZUREWAVE_WIFI_BT_MODULE_STR
    if [ $? -eq 0 ]; then
        WIFI_BT_MODULE=$AZUREWAVE_MODULE
        echo "Azurewave Wifi/BT module is detected."
    else
        # Check if RealTek Module is present
        lspci | grep -q $REALTEK_WIFI_BT_MODULE_STR
        if [ $? -eq 0 ]; then
            WIFI_BT_MODULE=$REALTEK_MODULE
            echo "RealTek Wifi module is detected."
        else
            WIFI_BT_MODULE=$UNKNOWN_MODULE
            echo "Neither Azurewave nor RealTek Wifi is detected."
        fi
    fi
}

reload_azurewave_wifi_drivers() {
    # Removed the drivers
    rmmod brcmfmac brcmutil cfg80211 compat
    sleep 5
    # insmod wifi drivers
    insmod /lib/modules/misc/compat.ko
    insmod /lib/modules/misc/cfg80211.ko
    insmod /lib/modules/misc/brcmutil.ko
    insmod /lib/modules/misc/brcmfmac.ko debug=0x100000
    sleep 10
}

restart_networkmanager() {
    systemctl start NetworkManager
    sleep 5
    nmcli radio wifi off
    sleep 1
    nmcli radio wifi on
    sleep 1
    rfkill unblock wlan
    sleep 1
    ifconfig wlan0 down
    sleep 1
    ifconfig wlan0 up
    sleep 5
}

monitor_reinit_file() {
    while true; do
        if [ -f /dev/shm/wifi_reinit ]; then
            echo "/dev/shm/wifi_reinit file found. Reinitializing Wi-Fi"

            # Call the functions to reinitialize Wi-Fi
            check_wifi_bt_module
            if [ $WIFI_BT_MODULE -eq $AZUREWAVE_MODULE ]; then
                systemctl stop NetworkManager
                sleep 2
                reload_azurewave_wifi_drivers
                restart_networkmanager
            else
                echo "To be done for non Azurewave modules"
            fi

            sudo rm -f /dev/shm/wifi_reinit
            sudo rm /etc/NetworkManager/system-connections/*
            kill -TERM $$
        fi

        sleep 1
    done
}


monitor_reinit_file &
#-----------------------------------------
sudo /home/ubuntu/.nddevice/latest/service/wifi_mgr/wifi_mgr
echo END OF SCRIPT
