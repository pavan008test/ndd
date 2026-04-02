#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling copy funtion from lib
md5sum_nvpmodel_org=$(md5sum nvpmodel_org.conf |awk '{print $1}')
md5sum_nvpmodel_system=$(md5sum /etc/nvpmodel/nvpmodel_t186_p3636.conf |awk '{print $1}')
md5sum_nvpmodel_t186=$(md5sum nvpmodel_t186_p3636.conf |awk '{print $1}')

log "==================Enabling Denver Cores and Assigning CPU affinities to ND Services=================="
log "===========checking md5sum of nvpmodel_org.conf and nvpmodel_t186_p3636.conf file==========="
if [[ "$md5sum_nvpmodel_org" == "$md5sum_nvpmodel_system" ]]; then
log "The nvpmodel_org.conf file matches the device's nvpmodel_t186_p3636.conf file, with the same md5sum $md5sum_nvpmodel_system"
else
log "The nvpmodel_org.conf file does not match the device's nvpmodel_t186_p3636.conf file, as they have different md5sums $md5sum_nvpmodel_system."
fi
log "============nvpmodel_t186_p3636.conf md5sum checks are done========="


echo "output of the command 'nvpmodel -q --verbose':" >> /home/ubuntu/.nddevice/log/bhcopy.log
nvpmodel -q --verbose >> /home/ubuntu/.nddevice/log/bhcopy.log

log "============Copying the latest nvpmodel_t186_p3636.conf file to the /etc/nvpmodel/============"
if [[ "$md5sum_nvpmodel_system" == "$md5sum_nvpmodel_t186" ]]; then
       log "Already updated nvpmodel_t186_p3636.conf is there,So not copying"
       status1=0
else
	log "Taking the backup of the existing nvpmodel_t186_p3636.conf as nvpmodel_t186_p3636_backup.conf"
	cp -rf /etc/nvpmodel/nvpmodel_t186_p3636.conf /etc/nvpmodel/nvpmodel_t186_p3636_backup.conf
	log "Copying the latest nvpmodel_t186_p3636.conf file to /etc/nvpmodel/"
	status1=$(copy_file -f nvpmodel_t186_p3636.conf -d /etc/nvpmodel/ -m $md5sum_nvpmodel_t186 -p 644 -o root:root)$?
fi
log "===========Copying the latest nvpmodel_t186_p3636.conf is done==========="

echo "Running the command 'nvpmodel -m 3':" >> /home/ubuntu/.nddevice/log/bhcopy.log
nvpmodel -m 3
sleep 2

echo "output of the command 'nvpmodel -q --verbose':" >> /home/ubuntu/.nddevice/log/bhcopy.log
nvpmodel -q --verbose >> /home/ubuntu/.nddevice/log/bhcopy.log

echo "Running few commands" >> /home/ubuntu/.nddevice/log/bhcopy.log  
echo "Command: cat /sys/devices/system/cpu/cpu1/online; Output: $(cat /sys/devices/system/cpu/cpu1/online)" >> /home/ubuntu/.nddevice/log/bhcopy.log
echo "Command: cat /sys/devices/system/cpu/cpu2/online; Output: $(cat /sys/devices/system/cpu/cpu2/online)" >> /home/ubuntu/.nddevice/log/bhcopy.log
echo "Command: cat /sys/devices/17000000.gp10b/devfreq/17000000.gp10b/max_freq; Output: $(cat /sys/devices/17000000.gp10b/devfreq/17000000.gp10b/max_freq)" >> /home/ubuntu/.nddevice/log/bhcopy.log

echo "Executing the above commands to check the expected o/p is present or not" >> /home/ubuntu/.nddevice/log/bhcopy.log 
if [[ $(cat /sys/devices/system/cpu/cpu1/online) -ne 1 || $(cat /sys/devices/system/cpu/cpu2/online) -ne 1 || $(cat /sys/devices/17000000.gp10b/devfreq/17000000.gp10b/max_freq) -ne 1134750000 ]]; then   
    status2=1  
else    
    echo "All checks passed successfully" >> /home/ubuntu/.nddevice/log/bhcopy.log    
    # Calling execute_script function from lib    
    log "Executing the  systemd_override_b3.sh"    
    execute_script systemd_override_b3.sh  
    status2=$?  
fi    
 
if [[ $status1 == 0 && $status2 == 0 ]];then
    check_status $(basename $(pwd)) 0
    log "All checks passed successfully"
    else
    check_status $(basename $(pwd)) 1
    log "Something went wrong please check the logs"
    fi
log "===========End of Enabling Denver Cores and Assigning CPU affinities to ND Services==============="
