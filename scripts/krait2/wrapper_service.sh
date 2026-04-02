#!/bin/bash
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/home/ubuntu/.nddevice/latest/:/nd_lib/

ND_INPUT_PATH=/home/iriscli/ND_INPUT

if [ -f "/home/ubuntu/.nddevice/latest/service/$1/$1.sh" ]; then
	echo "/home/ubuntu/.nddevice/latest/service/$1/$1.sh"
	/home/ubuntu/.nddevice/latest/service/$1/$1.sh
	ret_wrapper=$?
else
	echo "/home/ubuntu/.nddevice/bootstrap/service/$1/$1.sh"
	/home/ubuntu/.nddevice/bootstrap/service/$1/$1.sh
	ret_wrapper=$?
fi

echo "END OF wrapper_service.sh SCRIPT for service "$1" with result "$ret_wrapper
exit $ret_wrapper

# look into NMEI client for a simpler slution


