#!/bin/sh

#script for invoking the ext_cam service
sudo nmcli radio wifi off
sleep 1

echo "ConnMgr removing saved connections"
sudo rm /etc/NetworkManager/system-connections/*

sudo nmcli radio wifi on
sleep 1

sudo /home/ubuntu/.nddevice/latest/service/wifi_mgr/wifi_mgr

echo END OF SCRIPT



