#!/bin/sh

#script for invoking the scheduler_manager service
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:/home/ubuntu/bin
export ND_DEVICE_REL_PATH=/home/ubuntu/.nddevice
export ND_CONFIG_PATH=$ND_DEVICE_REL_PATH/latest/nd_config.ini
export ND_INPUT_PATH=/home/iriscli/ND_INPUT
export ND_OUTPUT_PATH=/home/iriscli/ND_OUTPUT

/home/ubuntu/.nddevice/latest/service/scheduler_manager/scheduler_manager

echo END OF SCRIPT



