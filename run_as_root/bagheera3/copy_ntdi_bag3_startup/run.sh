#!/usr/bin/env bash

#set -e

source  /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh


CURRENT_OS_VERSION=$(grep "nd_os" /etc/nd_os_ver.ini | cut -d'=' -f2)

if [[ $CURRENT_OS_VERSION != 13.0.19 ]] && [[ $CURRENT_OS_VERSION != 13.0.1A ]] ; then
    echo "This script is intended for OS version 13.0.19 or 13.0.1A,So Exiting..."    
    exit 0
fi

md5sum_ntdi_system=$(md5sum /etc/init.d/ntdi_bag3_startup |awk '{print $1}')
md5sum_ntdi=$(md5sum ntdi_bag3_startup |awk '{print $1}')


log "========= Start of copying ntdi_bag3_startup script into /etc/init.d/ =========="
if [[ $md5sum_ntdi_system == $md5sum_ntdi ]]; then

       log "Already Updated ntdi_bag3_startup is there so not copying"
       status_1=0
else
        log "Copying the latest ntdi_bag3_starup script to /etc/init.d/"
	status_1=$(copy_file -f  ntdi_bag3_startup  -d /etc/init.d -b /home/ubuntu/.nddevice/backup -m $md5sum_ntdi -p 755 -o root:root)$?

fi
log "========= End of copying ntdi_bag3_startup script into /etc/init.d/ ========="

log "========= Start of copying ltc3350_pwr_loop bin into /bin/vendor/ =========="

md5sum_ltc3350_pwr_loop_system=$(md5sum /bin/vendor/ltc3350_pwr_loop | awk '{print $1}')
md5sum_ltc3350_pwr_loop=$(md5sum ltc3350_pwr_loop | awk '{print $1}')

if [[ $md5sum_ltc3350_pwr_loop_system == $md5sum_ltc3350_pwr_loop ]]; then

	log "Already Updated ltc3350_pwr_loop is there so not copying"
	status_2=0
else
	log "Copying ltc3350_pwr_loop to /bin/vendor"
	status_2=$(copy_file -f ltc3350_pwr_loop -d /bin/vendor -b /home/ubuntu/.nddevice/backup -m $md5sum_ltc3350_pwr_loop -p 755 -o root:root)$?

fi

log "========= End of copying ltc3350_pwr_loop bin into /bin/vendor/  ========="

# md5sum_interfaces_system=$(md5sum /etc/init.d/inet_interfaces_config.sh | awk '{print $1}')
# md5sum_interfaces=$(md5sum inet_interfaces_config.sh | awk '{print $1}')

# log "========= Start of copying inet_interfaces_config.sh script into /etc/init.d/ =========="

# if [[ $md5sum_interfaces_system == $md5sum_interfaces ]]; then

# 	log "Already Updated inet_interfaces_config.sh is there so not copying"
# 	status_2=0
# else
# 	log "Copying inet_interfaces_config.sh to /etc/init.d"
# 	status_2=$(copy_file -f   inet_interfaces_config.sh -d /etc/init.d -b /home/ubuntu/.nddevice/ -m $md5sum_interfaces -p 755 -o root:root)$?
# 	touch /dev/shm/ntdi_task_status_file

# fi

# log "========= End of copying inet_interfaces_config.sh script into /etc/init.d/ ========="


md5sum_libsys_system=$(md5sum /lib/libsys.so | awk '{print $1}')
md5sum_libsys=$(md5sum  libsys.so | awk '{print $1}')

log "========= Start of copying libsys.so  into /lib/ =========="

if [[ $md5sum_libsys_system == $md5sum_libsys ]]; then

	log "Already Updated libsys.so is there so not copying"
	status_3=0
else
	log "Copying libsys.so to /lib/ "
	status_3=$(copy_file -f   libsys.so -d /lib/ -b /home/ubuntu/.nddevice/backup -m $md5sum_libsys -p 755 -o root:root)$?
	#touch /dev/shm/ntdi_task_status_file

fi

log "========= End of copying libsys.so script into /lib/ ========="


# md5sum_wlanbind_system=$(md5sum /bin/vendor/wifi_device_bind.sh | awk '{print $1}')
# md5sum_wlanbind=$(md5sum wifi_device_bind.sh | awk '{print $1}')

# log "========= Start of copying wifi_device_bind.sh  into /bin/vendor/ =========="

# if [[ $md5sum_wlanbind_system == $md5sum_wlanbind ]]; then

# 	log "Already Updated wifi_device_bind.sh is there so not copying"
# 	status_4=0
# else
# 	log "Copying wifi_device_bind.sh to  /bin/vendor/"
# 	status_4=$(copy_file -f wifi_device_bind.sh -d /lib/ -b /home/ubuntu/.nddevice/ -m $md5sum_wlanbind -p 755 -o root:root)$?
# 	touch /dev/shm/ntdi_task_status_file

# fi

# log "========= End of copying wifi_device_bind.sh script into /bin/vendor/ ========="

log "Verifying the status of above copies in thie task"
if [[ $status_1 != 0 ]]; then log "status_1 failed !!!!" ; check_status $(basename $(pwd)) 1 ; fi
if [[ $status_2 != 0 ]]; then log "status_2 failed !!!!" ; check_status $(basename $(pwd)) 1 ; fi
if [[ $status_3 != 0 ]]; then log "status_3 failed !!!!" ; check_status $(basename $(pwd)) 1 ; fi

check_status $(basename $(pwd)) 0
