#!/bin/sh

var="latest"

if [ "$#" -eq 1 ];
    then echo "Folder path is :$1 "
    var=$1
fi

. /home/ubuntu/.nddevice/latest/service/sierra/config.txt

echo "Checking for Sierra Module Enumeration "

while [ ! -e "/dev/qcqmi0" ]
do 
   sleep 2
done

echo "Enumeration succeeded , profile & APN name are"

echo $profileName

echo $apnName

if [ ! -e /home/ubuntu/.nddevice/log/sierra ]
then
      echo "Creating log directory sierra"
      su ubuntu -c 'mkdir -m 0755 -p /home/ubuntu/.nddevice/log/sierra'
fi

nmcli radio wifi | grep "enabled" > /dev/null

ret=$?

if [ $ret -eq 0 ]
then
   echo "Turning WIFI OFF"
   sudo nmcli radio wifi off
   sleep 5
fi

a='/home/ubuntu/.nddevice/log/sierra/log_'
b=`date +%s%N | cut -b1-13`
c='.log'
log_path=$a$b$c

echo $log_path

su ubuntu -c "touch \"$log_path\""

sudo /home/ubuntu/.nddevice/$var/service/sierra/connectionmgrarm /home/ubuntu/.nddevice/$var/service/sierra/slqssdk 0 "$profileName" "$apnName" 2>> $log_path &

echo "Started LTE Data session"
