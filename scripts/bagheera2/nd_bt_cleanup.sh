#!/bin/bash

/home/ubuntu/.nddevice/latest/service/nd_bt/nd_bt_cli -c

pkill -USR1 nd_bt_man

sleep 2

exit 0
