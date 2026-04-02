#!/bin/sh

#script for invoking the apm service

FILE="/sys/kernel/krait/ignition"
MODULE="gpio_ignition"

if test -f "$FILE"; then
    echo "$MODULE is inserted"
else
    modprobe gpio-ignition
fi

sudo /home/ubuntu/.nddevice/latest/service/apm/apm

echo END OF SCRIPT


