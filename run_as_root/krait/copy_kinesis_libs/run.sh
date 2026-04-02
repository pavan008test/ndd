#!/usr/bin/env bash


source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

md5sum_libcproducer=$(md5sum libcproducer.so |awk '{print $1}')
md5sum_libcrypto=$(md5sum libcrypto.so.1.1 |awk '{print $1}')
md5sum_libcurl=$(md5sum libcurl.so |awk '{print $1}')
md5sum_libssl=$(md5sum libssl.so.1.1 |awk '{print $1}')
md5sum_cacert=$(md5sum cacert.pem |awk '{print $1}')

#Calling execute_script function from lib
log "=============Copying the kinesis libs=================="
copy_file -f libcproducer.so -d /nd_lib/ -b /home/ubuntu/.nddevice/backup -m ${md5sum_libcproducer}
status1=$(echo $?)
copy_file -f libcrypto.so.1.1 -d /nd_lib/ -b /home/ubuntu/.nddevice/backup -m ${md5sum_libcrypto}
status2=$(echo $?)
copy_file -f libcurl.so -d /nd_lib/ -b /home/ubuntu/.nddevice/backup -m ${md5sum_libcurl}
status3=$(echo $?)
copy_file -f libssl.so.1.1 -d /nd_lib/ -b /home/ubuntu/.nddevice/backup -m ${md5sum_libssl}
status4=$(echo $?)
copy_file -f cacert.pem -d /data/nd_files/certificate -b /home/ubuntu/.nddevice/backup -m ${md5sum_cacert}
status5=$(echo $?)

if [[ $status1 == 0 && $status2 == 0 && $status3 == 0 && $status4 == 0 && $status5 == 0 ]] ; then
	status=0
	check_status $(basename $(pwd)) $status
else
	status=1
	check_status $(basename $(pwd)) $status
fi

log "=============End of Copying the kinesis libs===================="
