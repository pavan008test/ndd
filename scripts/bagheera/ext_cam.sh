#!/bin/sh

#script for invoking the ext_cam service

sudo /home/ubuntu/.nddevice/latest/service/ext_cam/ext_cam
ret_result=$?
echo "END OF SCRIPT ext_cam.sh with result "$ret_result 
exit $ret_result
