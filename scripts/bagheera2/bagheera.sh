#!/bin/sh

#script for invoking the bagheera service

#this sleep is to compensate the ExecStartPre removal in service file
sleep 2

$(grep ND_INPUT_PATH /home/ubuntu/.bashrc)

rm -f /tmp/kinesis_streaming_out
rm -f /tmp/kinesis_streaming_in

export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/home/ubuntu/.nddevice/latest/

sudo sh /home/ubuntu/.nddevice/latest/service/bagheera/cleanup_outward_shm.sh &
# These changes are to reduce the kernel logging

#echo "file drivers/media/i2c/ov9732.c line 683 -p" > /sys/kernel/debug/dynamic_debug/control
#echo "file drivers/media/i2c/ov9732.c line 700 -p" > /sys/kernel/debug/dynamic_debug/control
#echo "file drivers/media/i2c/ov9732.c line 721 -p" > /sys/kernel/debug/dynamic_debug/control
#echo "file drivers/media/i2c/ov9732.c line 821 -p" > /sys/kernel/debug/dynamic_debug/control
#echo "file drivers/media/i2c/ov9732.c line 834 -p" > /sys/kernel/debug/dynamic_debug/control

# Dependency is added in bagheera.service for ntdi_icdc.service
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

ND_FILES_PATH="/home/iriscli/files"
echo "Startup script finished"

echo "Clear older gstreamer cache register"
ls -t /root/.cache/gstreamer-1.0/* | tail -n +2 | xargs rm --

# ND_FILES_PATH is the path where recording happens (/home/iriscli/files)
# ND_INPUT_PATH is the path where scheduler looks for files to run analytics (/home/iriscli/ND_INPUT)
PATH=$PATH:/bin/vendor/gpio_test
/home/ubuntu/.nddevice/latest/service/bagheera/bagheera $ND_FILES_PATH $ND_INPUT_PATH

echo END OF SCRIPT
