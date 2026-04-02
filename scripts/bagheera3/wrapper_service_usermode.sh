#!/bin/sh

$(grep ND_INPUT_PATH /home/ubuntu/.bashrc)

if [ -f "/home/ubuntu/.nddevice/latest/service/$1/$1.sh" ]; then
	/home/ubuntu/.nddevice/latest/service/$1/$1.sh  
	echo "/home/ubuntu/.nddevice/latest/service/$1/$1.sh"
else
	/home/ubuntu/.nddevice/bootstrap/service/$1/$1.sh  
	echo "/home/ubuntu/.nddevice/bootstrap/service/$1/$1.sh"
fi

echo END OF SCRIPT

# look into NMEI client for a simpler slution


