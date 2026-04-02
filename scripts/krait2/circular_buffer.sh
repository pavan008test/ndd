#!/bin/sh

#script for invoking the circular_buffer service

ND_INPUT_PATH="/home/iriscli/ND_INPUT"
#$(grep ND_INPUT_PATH /home/ubuntu/.bashrc)
log_dir=/home/ubuntu/.nddevice/log/circ_buff

d=$(date +%s)
# append 3 zeros to convert second to milli seconds
logfilename=log_$d"000.log"
logfilename=$log_dir/$logfilename
echo ================Logs from circular_buffer.sh - $(date --date @$d)==========================>$logfilename


sleep 5
/home/ubuntu/.nddevice/latest/service/circular_buffer/circular_buffer


echo END OF SCRIPT
