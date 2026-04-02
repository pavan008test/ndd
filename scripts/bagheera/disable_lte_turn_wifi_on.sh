#!/bin/sh

echo "Killing LTE service"

PID=`ps -e | grep connectionmgr | grep -v grep | awk '{print $1}'`

echo $PID

while [ ! -e "/dev/qcqmi0" ]
do
   sleep 2
done

if [ -e /proc/$PID ]; then
   sudo kill -3 $PID
else
   echo "connectionmgr is already killed ..."
fi

sleep 2

nmcli radio wifi | grep "disabled" > /dev/null

ret=$?

if [ $ret -eq 0 ]
then
   echo "Turning WIFI On"

   sudo nmcli radio wifi on

   sleep 6
fi

