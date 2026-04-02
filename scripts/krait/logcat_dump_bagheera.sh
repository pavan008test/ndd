#!/bin/bash

date +%s
name="logcat_"
date_str=`date +%s`
full_name="$name$date_str.log"
echo $full_name

logcat -t 10000 > /home/ubuntu/.nddevice/log/ndcentral/$full_name

logcat_pid=$!

wait $logcat_pid
echo "Logcat process completed."

echo END_OF_SCRIPT
