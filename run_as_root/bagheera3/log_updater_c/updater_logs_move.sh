#!/bin/bash

echo "`date +%Y-%m-%d` `date +"%T,%3N"` - onetimelogupdate - INFO -checking the keepalivecount">> /home/ubuntu/.nddevice/log/bhcopy.log

count=$(cat /home/ubuntu/.nddevice/log/keepalive_count.txt)
divisor=10
remainder=$((count%divisor))
echo "Keepalive count remainder value  -  $remainder ">> /home/ubuntu/.nddevice/log/bhcopy.log


if [[ $remainder == 0 || $remainder -gt 8 ]] ; then 
echo "changing the keepalivecount value" >> /home/ubuntu/.nddevice/log/bhcopy.log
counter=$(( $count - 2 ))
     echo $counter > /home/ubuntu/.nddevice/log/keepalive_count.txt
else
echo "$remainder is not equal to 0 or greater than 8 so skipping" >> /home/ubuntu/.nddevice/log/bhcopy.log
fi

echo "`date +%Y-%m-%d` `date +"%T,%3N"` - onetimelogupdate - INFO - Scheduled a task. Sleep for 60 sec">> /home/ubuntu/.nddevice/log/bhcopy.log
sleep 60

echo "`date +%Y-%m-%d` `date +"%T,%3N"` - onetimelogupdate - INFO - checking the updater_c folder exists or not">> /home/ubuntu/.nddevice/log/bhcopy.log
if [[ ! -d /home/ubuntu/.nddevice/log/updater_c ]] ; then
     mkdir -p /home/ubuntu/.nddevice/log/updater_c/
fi

if [[ -n $(ls /home/ubuntu/.nddevice/log/updater/*) ]]; then
echo "updater folder contains updater logs"
cp /home/ubuntu/.nddevice/log/updater/* /home/ubuntu/.nddevice/log/updater_c/
else 
echo "updater folder is empty, please check"
fi
     
echo "`date +%Y-%m-%d` `date +"%T,%3N"` - onetimelogupdate - INFO - updater_c folder will contains the updater.log* files in few sec's">> /home/ubuntu/.nddevice/log/bhcopy.log
sudo rm -f /etc/systemd/system/onetimelogupdate.service
