#!/bin/sh
#Script to continuously monitor the SIM Status, every 5 minutes

while [ 1 ]
do

status=$(lte_gps_test 'at!gstatus?' |  awk ' /Mode:/ {print $5}')

if [ $status != "ONLINE" ]
then
    epoch_time=$(date +%s%3N)
    uptime >> /home/ubuntu/.nddevice/log/conn_mgr/conn_force_online_${epoch_time}.log
    echo " Status  $status" >> /home/ubuntu/.nddevice/log/conn_mgr/conn_force_online_${epoch_time}.log
    sleep 3
    lte_gps_test 'at+cfun=1'
    echo " Moving to ONLINE" >> /home/ubuntu/.nddevice/log/conn_mgr/conn_force_online_${epoch_time}.log
    sleep 3
    status=$(lte_gps_test 'at!gstatus?' |  awk ' /Mode:/ {print $5$6$7 }')
    echo " Status  $status" >> /home/ubuntu/.nddevice/log/conn_mgr/conn_force_online_${epoch_time}.log
    echo " Check Done. Sleeping..." >> /home/ubuntu/.nddevice/log/conn_mgr/conn_force_online_${epoch_time}.log
fi

sleep 300

done
