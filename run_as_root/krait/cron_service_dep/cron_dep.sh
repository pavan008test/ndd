#!/bin/bash

for i in `seq 1 5`; 
do
    if grep "[[:space:]]rw[[:space:],]" /proc/mounts | grep -qw "/data" && grep "[[:space:]]rw[[:space:],]" /proc/mounts | grep -qw "/";then
        break
    fi
    sleep 4
done
