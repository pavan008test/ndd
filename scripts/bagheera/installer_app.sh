#!/bin/sh

#script for invoking the installer_app service

export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/home/ubuntu/.nddevice/latest/:/home/ubuntu/.nddevice/gst_1.8.1/
export GST_PLUGIN_SYSTEM_PATH=/home/ubuntu/.nddevice/gst_1.8.1/gstreamer-1.0/
 
/home/ubuntu/.nddevice/latest/service/installer_app/installer_app

echo END OF SCRIPT
