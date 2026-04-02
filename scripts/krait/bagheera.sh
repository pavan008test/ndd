#!/bin/bash

#script for invoking the bagheera service

$(grep ND_INPUT_PATH /home/ubuntu/.bashrc)

sudo sh /home/ubuntu/.nddevice/latest/service/bagheera/cleanup_outward_shm.sh &
sudo sh /home/ubuntu/.nddevice/latest/service/bagheera/cleanup_inward_shm.sh &
rm -f /tmp/kinesis_streaming_out
rm -f /tmp/kinesis_streaming_in

#reset outward camera
#/usr/bin/gpio-app -n 8 -s 0
#sleep 1
#/usr/bin/gpio-app -n 8 -s 1
#sleep 1

# Dependency is added in bagheera.service for ntdi_icdc.service
# which executes startup.sh. That is how we are sure that if pid_startup
# is blank, startup.sh has been finished.

pid_startup=$(ps -e | grep -w startup.sh | awk '{print $1}')

if [ -n "$pid_startup" ]; then

	echo "pid of startup.sh is $pid_startup"
	while [ -e /proc/$pid_startup ]
	do
		echo "waiting for startup.sh to finish"
        sleep 1
	done
fi

ND_FILES_PATH="/home/iriscli/files"
echo "Startup script finished"

# ND_FILES_PATH is the path where recording happens (/home/iriscli/ND_INPUT/files)
# ND_INPUT_PATH is the path where scheduler looks for files to run analytics (/home/iriscli/ND_INPUT)
/home/ubuntu/.nddevice/latest/service/bagheera/bagheera $ND_FILES_PATH $ND_INPUT_PATH

echo END OF SCRIPT
