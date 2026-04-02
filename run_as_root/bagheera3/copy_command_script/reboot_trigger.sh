#!/bin/bash

echo "`date +%Y-%m-%d` `date +"%T,%3N"` - oneTimeReboot - INFO - Scheduled a reboot after 3 mits. Sleeping now">> /home/ubuntu/.nddevice/log/bhcopy.log
sleep 180
echo "`date +%Y-%m-%d` `date +"%T,%3N"` - oneTimeReboot - INFO - Rebooting now">> /home/ubuntu/.nddevice/log/bhcopy.log
sudo rm -f /etc/systemd/system/onetimereboot.service
sudo shutdown -r now

