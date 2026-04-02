#!/bin/sh

$(grep ND_INPUT_PATH /home/ubuntu/.bashrc)

# This script expects two arguments
# Arg-1. service name
# Arg-2. to wait for ntdi_icdc to complete or 30 secs which ever is earlier This is also default option if nothing is specified
# If you wish not to wait for ntdi_icdc service you can give 'dontwait' option

wait_option=$2

if [ "$wait_option" != "dontwait" ]; then
    echo "making wait as default option for" $1
    wait_option="wait"
fi

echo "wait_option :"$wait_option " for service "$1

wait_count=0
#if [[ "$VAR1" == "$VAR2" ]]; then
if [ "$wait_option" = "wait" ]; then
    echo "waiting for ntdi service to complete or 30 secs timeout before starting " $1

    while [ ! -f /dev/shm/ntdi_icdc.done ]; 
    do
        wait_count=$((wait_count+1))
        if [ $wait_count -gt 30 ]; then
            echo "reached timeout while waiting for /dev/shm/ntdi_icdc.done"
            echo "starting service after timeout" $1
            break;
        fi
        sleep 1;
        echo "wait_count "$wait_count " for service "$1
    done
fi

if [ -f "/home/ubuntu/.nddevice/latest/service/$1/$1.sh" ]; then
    echo "/home/ubuntu/.nddevice/latest/service/"$1"/"$1".sh"
    sudo /home/ubuntu/.nddevice/latest/service/$1/$1.sh
	ret_wrapper=$?
else
    echo "/home/ubuntu/.nddevice/bootstrap/service/"$1"/"$1".sh"
    sudo /home/ubuntu/.nddevice/bootstrap/service/$1/$1.sh
	ret_wrapper=$?
fi

echo "END OF wrapper_service.sh SCRIPT for service "$1" with result "$ret_wrapper
exit $ret_wrapper 

# look into NMEI client for a simpler solution
