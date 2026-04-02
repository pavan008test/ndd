#!/bin/sh

#script for invoking the power_monitor service

$(grep ND_INPUT_PATH /home/ubuntu/.bashrc)

sudo /home/ubuntu/.nddevice/latest/service/power_monitor/power_monitor

echo END OF SCRIPT



