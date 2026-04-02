#!/bin/sh
#Script to continuously monitor the SIM Status, every 5 minutes

while [[ 1 ]]
do

status=$(lte_gps_test 'at!gstatus?' |  awk ' /Mode:/ {print $5$6$7 }')

uptime >> /tmp/conn_force_online
echo " Status  $status" >> /tmp/conn_force_online

if [[ $status != "ONLINE" ]]
then
    sleep 3
    lte_gps_test 'at+cfun=1'
    echo " Moving to ONLINE" >> /tmp/conn_force_online
    sleep 3
    status=$(lte_gps_test 'at!gstatus?' |  awk ' /Mode:/ {print $5$6$7 }')
    echo " Status  $status" >> /tmp/conn_force_online
fi

echo " Check Done. Sleeping..." >> /tmp/conn_force_online

sleep 300

done
