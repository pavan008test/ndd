#!/bin/sh

NDDEVICE_PATH=/home/ubuntu/.nddevice/

touch /dev/shm/speed.info
touch $NDDEVICE_PATH/current_speed.info;
touch $NDDEVICE_PATH/previous_speed.info;

if [ -f $NDDEVICE_PATH/current_speed.info ];
then
    cat $NDDEVICE_PATH/current_speed.info > $NDDEVICE_PATH/previous_speed.info
fi

while [ 1 ]
do
    sleep 20;
    cat /dev/shm/speed.info > $NDDEVICE_PATH/current_speed.info
done
