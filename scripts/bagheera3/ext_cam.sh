#!/bin/sh

#script for invoking the ext_cam service

#this sleep is to compensate the ExecStartPre removal in service file
rm -r /home/iriscli/data_frames_*
rm -f /home/ubuntu/RGB_Check_*
sync
sleep 2

sudo /home/ubuntu/.nddevice/latest/service/ext_cam/ext_cam
ret_result=$?
echo "END OF SCRIPT ext_cam.sh with result "$ret_result 
exit $ret_result
