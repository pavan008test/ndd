#!/bin/sh

#script for invoking the speed service

NDDEVICE_PATH=/home/ubuntu/.nddevice/

$NDDEVICE_PATH/latest/service/speed/speed.record.sh &

$NDDEVICE_PATH/latest/service/speed/speed

echo END OF SCRIPT



