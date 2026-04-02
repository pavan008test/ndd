#!/bin/bash

# Disable Denver Cores
if [[ $(cat /sys/devices/system/cpu/cpu1/online) -ne 0 || $(cat /sys/devices/system/cpu/cpu2/online) -ne 0 ]]; then
    rm -f /etc/nvpmodel/nvpmodel_t186_transient.conf
    cp -rf /home/ubuntu/.nddevice/latest/service/diagnostic/nvpmodel_t186_backup.conf /etc/nvpmodel/nvpmodel_t186_transient.conf
    mv /etc/nvpmodel/nvpmodel_t186_transient.conf /etc/nvpmodel/nvpmodel_t186.conf
    expected_md5sum=c96efb2adb3df6d368d137789e179607
    actual_md5sum=$(md5sum /etc/nvpmodel/nvpmodel_t186.conf | awk '{print $1}')
    if [[ "$actual_md5sum" != "$expected_md5sum" ]]; then
        rm -f /etc/nvpmodel/nvpmodel_t186_transient.conf
        cp -rf /home/ubuntu/.nddevice/latest/service/diagnostic/nvpmodel_t186_denver.conf /etc/nvpmodel/nvpmodel_t186_transient.conf
        mv /etc/nvpmodel/nvpmodel_t186_transient.conf /etc/nvpmodel/nvpmodel_t186.conf
        exit 1
    fi
    nvpmodel -m 3
    sleep 2
fi


if [[ $(cat /sys/devices/system/cpu/cpu1/online) -ne 0 || $(cat /sys/devices/system/cpu/cpu2/online) -ne 0 ]]; then
    echo "Failed to disable Denver cores" 
else
    echo "Denver cores disabled successfully"
fi

# Revert CPU affinities for all processes
find /etc/systemd/system/ -type d -name "*.service.d" | xargs rm -rvf
nvpmodel -q --verbose
echo "Reboot to apply the affinity removal change"
