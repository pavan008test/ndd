#!/bin/sh

#script for invoking the internal_buffer service

$(grep ND_INPUT_PATH /home/ubuntu/.bashrc)
username=ubuntu

# Dependency is added in internal_buffer.service for ntdi_icdc.service
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

echo "Startup script finished"

#internal buffer does not require transcoding related commands

sleep 5
sudo /home/ubuntu/.nddevice/latest/service/internal_buffer/internal_buffer

echo END OF SCRIPT



