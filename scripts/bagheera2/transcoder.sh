#!/bin/sh

#script for invoking the transcoder service


#create dir for transcode 
TC_FILES_PATH="/home/iriscli/saveMP4/transcode"
rm -rf $TC_FILES_PATH
echo "Removing files from $TC_FILES_PATH/ folder"
mkdir -p $TC_FILES_PATH/
#Clean files in recording folder before starting bagheera service

echo "mapping $TC_FILES_PATH/ to tmpfs"
mount -t tmpfs -o size=100m tmpfs $TC_FILES_PATH/
echo $?

echo "Removing files from $TC_FILES_PATH/ folder after mapping to ramdisk"
rm -rf $TC_FILES_PATH/*

sleep 5
sudo LD_LIBRARY_PATH=/usr/lib/aarch64-linux-gnu/ GST_PLUGIN_PATH=/usr/lib/aarch64-linux-gnu/gstreamer-1.0/ /home/ubuntu/.nddevice/latest/service/transcoder/transcoder

echo "unmapping $TC_FILES_PATH/ from tmpfs"
umount $TC_FILES_PATH/
echo $?

echo END OF SCRIPT



