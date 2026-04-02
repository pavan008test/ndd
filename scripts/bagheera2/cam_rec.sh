#!/bin/sh

export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/home/ubuntu/.nddevice/latest/

sudo sh /home/ubuntu/.nddevice/latest/service/cam_rec/cleanup_inward_shm.sh &

/home/ubuntu/.nddevice/latest/service/cam_rec/cam_rec

echo END OF SCRIPT
