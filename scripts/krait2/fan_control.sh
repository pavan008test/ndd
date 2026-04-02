#!/bin/sh

#script for invoking the fan service

#Script to kill vvdn_fan_control binary
if [ -n "$(pgrep fan_control)" ]; then
    pkill -9 fan_control;
fi

/home/ubuntu/.nddevice/latest/service/fan_control/fan_control

echo END OF SCRIPT



