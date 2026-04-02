#!/bin/sh

#script for invoking the ext_cam service

rm -r /data/nd_files/data_frames_*
rm -f /home/ubuntu/RGB_Check_*
sync

/home/ubuntu/.nddevice/latest/service/ext_cam/ext_cam
ret_result=$?
echo "END OF SCRIPT ext_cam.sh with result "$ret_result 
exit $ret_result



