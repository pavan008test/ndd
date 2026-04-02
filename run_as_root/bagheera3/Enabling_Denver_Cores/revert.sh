#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "########Start of copying original nvpmodel_t186_p3636_backup.conf as nvpmodel_t186_p3636.conf  ######"
if [[ -f /etc/nvpmodel/nvpmodel_t186_p3636_backup.conf ]] ;then
        log "Copying the nvpmodel_t186_p3636_backup.conf as nvpmodel_t186_p3636.conf"
	cp -f /etc/nvpmodel/nvpmodel_t186_p3636_backup.conf /etc/nvpmodel/nvpmodel_t186_p3636.conf
else
	log "nvpmodel_t186_p3636_backup.conf is not present"
fi

echo "Running the command 'nvpmodel -m 3':" >> /home/ubuntu/.nddevice/log/bhcopy.log
nvpmodel -m 3
sleep 2

echo "output of the command 'nvpmodel -q –verbose':" >> /home/ubuntu/.nddevice/log/bhcopy.log
nvpmodel -q --verbose >> /home/ubuntu/.nddevice/log/bhcopy.log

echo "Running few commands" >> /home/ubuntu/.nddevice/log/bhcopy.log
echo "Command: cat /sys/devices/system/cpu/cpu1/online; Output: $(cat /sys/devices/system/cpu/cpu1/online)" >> /home/ubuntu/.nddevice/log/bhcopy.log
echo "Command: cat /sys/devices/system/cpu/cpu2/online; Output: $(cat /sys/devices/system/cpu/cpu2/online)" >> /home/ubuntu/.nddevice/log/bhcopy.log
echo "Command: cat /sys/devices/17000000.gp10b/devfreq/17000000.gp10b/max_freq; Output: $(cat /sys/devices/17000000.gp10b/devfreq/17000000.gp10b/max_freq)" >> /home/ubuntu/.nddevice/log/bhcopy.log

echo "Executing the above commands to check the expected o/p is present or not" >> /home/ubuntu/.nddevice/log/bhcopy.log
if [[ $(cat /sys/devices/system/cpu/cpu1/online) -ne 0 || $(cat /sys/devices/system/cpu/cpu2/online) -ne 0 || $(cat /sys/devices/17000000.gp10b/devfreq/17000000.gp10b/max_freq) -ne 1134750000 ]]; then       status1=1  
else    
    status1=0   
    echo "All checks passed successfully" >> /home/ubuntu/.nddevice/log/bhcopy.log    
fi    


echo "Removing the *.service.d" >> /home/ubuntu/.nddevice/log/bhcopy.log
find /etc/systemd/system/ -type d -name "*.service.d" | xargs rm -rvf >> /home/ubuntu/.nddevice/log/bhcopy.log

if [[ $status1 == 0 ]];then
    log "Successfully executed"
    check_status $(basename $(pwd)) 0
    else
    log "Something goes wrong,please check...!"	    
    check_status $(basename $(pwd)) 1
fi
