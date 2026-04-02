#!/bin/sh

mkdir -p /dev/shm/nd_files_c/
mkdir -p /dev/shm/nd_files_nc/
touch /dev/shm/nd_files_nc/standby_uptime
echo 0 > /dev/shm/nd_files_nc/standby_uptime
touch /dev/shm/nd_files_c/keepaliveresponse.txt
chmod 666 /dev/shm/nd_files_c/keepaliveresponse.txt
