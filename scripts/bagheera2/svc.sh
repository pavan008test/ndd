#!/bin/sh

#script for invoking the svc service
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/home/ubuntu/.nddevice/latest/

# BGR2-175 -Disable rootfs to recovery_roots partition switching
#setting crash_count to 0 on every reboot prevents( crash_count on reaching 10, the device shuts down)
#switching to recovery_rootfs and subsequent shutdown which makes the device unavailable."

sudo fw_setenv crash_count 0

sudo /home/ubuntu/.nddevice/latest/service/svc/svc

echo END OF SCRIPT
