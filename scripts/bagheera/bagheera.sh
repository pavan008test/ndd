#!/bin/sh

#script for invoking the bagheera service

$(grep ND_INPUT_PATH /home/ubuntu/.bashrc)

export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/home/ubuntu/.nddevice/latest/:/home/ubuntu/.nddevice/gst_1.8.1/
export GST_PLUGIN_SYSTEM_PATH=/home/ubuntu/.nddevice/gst_1.8.1/gstreamer-1.0/

# These changes are to reduce the kernel logging

echo "file drivers/media/i2c/ov9732.c line 683 -p" > /sys/kernel/debug/dynamic_debug/control
echo "file drivers/media/i2c/ov9732.c line 700 -p" > /sys/kernel/debug/dynamic_debug/control
echo "file drivers/media/i2c/ov9732.c line 721 -p" > /sys/kernel/debug/dynamic_debug/control
echo "file drivers/media/i2c/ov9732.c line 821 -p" > /sys/kernel/debug/dynamic_debug/control
echo "file drivers/media/i2c/ov9732.c line 834 -p" > /sys/kernel/debug/dynamic_debug/control

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

#mkdir folder for internal buffer
INTERNAL_BUFFER_FOLDER=/home/iriscli/internal_buff
echo "Creating folder for internal buffer at ${INTERNAL_BUFFER_FOLDER}"
mkdir -p ${INTERNAL_BUFFER_FOLDER}
chmod 777 ${INTERNAL_BUFFER_FOLDER}
chown ubuntu:ubuntu ${INTERNAL_BUFFER_FOLDER}

ND_FILES_PATH="/home/iriscli/files"
echo "Startup script finished"

echo "inserting gps_pps_interrupt.ko module"
sudo insmod /home/ubuntu/.nddevice/kernel_modules/gps_pps_interrupt.ko
sudo insmod /home/ubuntu/.nddevice/kernel_modules/imu_interrupt.ko

# ND_FILES_PATH is the path where recording happens (/home/iriscli/ND_INPUT/files)
# ND_INPUT_PATH is the path where scheduler looks for files to run analytics (/home/iriscli/ND_INPUT)
/home/ubuntu/.nddevice/latest/service/bagheera/bagheera $ND_FILES_PATH $ND_INPUT_PATH

echo END OF SCRIPT
