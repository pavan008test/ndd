#!/bin/bash

locations=("/var/lib/systemd/coredump/" "/sys/fs/pstore/" "/media/data/ntdi_bag2_logs/misc_logs/" "/media/data/ntdi_bag2_logs/sys_logs/")
rm_locations=("/media/data/ntdi_bag2_logs/misc_logs/" "/media/data/ntdi_bag2_logs/sys_logs/" "/var/log/ntdi_bag2_logs/sys_logs/" "/var/log/ntdi_bag2_logs/misc_logs/")
sleep 20
epoch_time=$(date +%s%3N)
for location in ${locations[@]}; do
        folder_name=$(basename $location)
        if [[ -n "$(ls $location)" ]]
        then 
            echo "Create a tar with ${folder_name} folder"
            sudo tar -czf /home/ubuntu/.nddevice/log/service_mon/${folder_name}.tar.gz $location
            sudo mv /home/ubuntu/.nddevice/log/service_mon/${folder_name}.tar.gz /home/ubuntu/.nddevice/log/service_mon/${folder_name}_${epoch_time}.tar.gz.log
            sudo chown ubuntu:ubuntu /home/ubuntu/.nddevice/log/service_mon/${folder_name}_${epoch_time}.tar.gz.log
        else 
            echo "Files are not present in ${folder_name} folder"
        fi
done

for location in ${rm_locations[@]}; do
        if [[ -n "$(ls $location)" ]]
        then 
            echo "Removing files from ${location}"
            rm -rf ${location}/*
        else 
            echo "Files are not present in ${location}"
        fi
done
