#!/bin/sh

#script for invoking the circular_buffer service

$(grep ND_INPUT_PATH /home/ubuntu/.bashrc)
log_dir=/home/ubuntu/.nddevice/log/circ_buff
username=ubuntu

# Dependency is added in circular_buffer.service for ntdi_icdc.service
# which executes startup.sh. That is how we are sure that if pid_startup
# is blank, startup.sh has been finished.

pid_startup=$(ps -e | grep -w ntdi_icdc_startup | awk '{print $1}')

if [ -n "$pid_startup" ]; then

	echo "pid of startup.sh is $pid_startup"
	while [ -e /proc/$pid_startup ]
	do
		echo "waiting for startup.sh to finish"
        sleep 1
	done
fi

echo "Startup script finished"

# Check for availability of h265parse in gstreamer registry.
# This plugin is needed for transcoding

d=$(date +%s)
# append 3 zeros to convert second to milli seconds
logfilename=log_$d"000.log"
logfilename=$log_dir/$logfilename
echo ================Logs from circular_buffer.sh - $(date --date @$d)==========================>$logfilename
h265parse_present=$(grep h265parse /root/.cache/gstreamer-1.0/registry.aarch64.bin | grep matches | wc -l) 2>>$logfilename
if [ $h265parse_present -eq 0 ]
then
    echo "h265parse not present. Removing root's gstreamer registry" >> $logfilename
    # Not using -rf options intentionally to see if registry was present or not
    sudo rm /root/.cache/gstreamer-1.0/registry.aarch64.bin 2>>$logfilename
else
    echo "h265parse is present in gstreamer registry" >> $logfilename
fi
sudo chown $username:$username $logfilename

sleep 5
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/home/ubuntu/.nddevice/latest/

sudo /home/ubuntu/.nddevice/latest/service/circular_buffer/circular_buffer

echo END OF SCRIPT



