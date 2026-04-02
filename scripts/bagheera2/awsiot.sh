#!/bin/sh

#script for invoking the awsiot service via AwsIotWrapper

$(grep ND_DEVICE_REL_PATH /home/ubuntu/.bashrc)

export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/home/ubuntu/.nddevice/awsiot_lib/

PYTHONPATH=/home/ubuntu/.nddevice/latest/ /home/ubuntu/.nddevice/latest/service/awsiot/AwsIotWrapper $ND_DEVICE_REL_PATH

echo END OF SCRIPT



