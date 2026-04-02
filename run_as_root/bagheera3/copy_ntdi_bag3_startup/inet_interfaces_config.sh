#!/bin/bash

declare -a interfaces
interfaces=(
"eth0"
"rndis0"
"usb0"
"l4tbr0"
)

sleep 30
run_state=1
while [ $run_state -eq 1 ]
do
	fail_state=0
	for i in "${interfaces[@]}"
	do
		ifconfig $i down
		if [ $? -ne 0 ]; then
			echo "ifconfig $i down is Failed"
			fail_state=1
			continue
		else
			fail_state=0
		fi
		ip a s $i | grep state | grep UP
		if [ $? -eq 0 ]; then
			echo "ifconfig $i down is Failed"
			fail_state=1
		else
			fail_state=0
			echo "ifconfig $i down is Successful"
		fi
	done
	if [ $fail_state -eq 1 ];then
		run_state=1
	else
		run_state=0
	fi
done
