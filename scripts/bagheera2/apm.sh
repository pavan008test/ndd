#!/bin/sh

#script for invoking the apm service

FILE="/sys/kernel/bagheera2/ignition"
MODULE="gpio_ignition"

if test -f "$FILE"; then
    echo "$MODULE is inserted"
else
     modpobe gpio-ignition
fi

$(grep ND_INPUT_PATH /home/ubuntu/.bashrc)

sudo /home/ubuntu/.nddevice/latest/service/apm/apm

echo END OF SCRIPT

