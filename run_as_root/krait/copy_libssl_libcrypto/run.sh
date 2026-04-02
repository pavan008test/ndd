#!/usr/bin/env bash
source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

arm32_libssl_md5sum=$(md5sum arm32/libssl.so.1.1 |awk '{print $1}')
arm32_libcry_md5sum=$(md5sum arm32/libcrypto.so.1.1 |awk '{print $1}')
arm64_libssl_md5sum=$(md5sum arm64/libssl.so.1.1 |awk '{print $1}')
arm64_libcry_md5sum=$(md5sum arm64/libcrypto.so.1.1 |awk '{print $1}')

log "=============Copying the libcrypto and libssl lib=================="
copy_file -f arm32/libssl.so.1.1 -d /nd_lib/ -b /home/ubuntu/.nddevice/backup -m $arm32_libssl_md5sum -o root:root -p 775
status1=$(echo $?)
copy_file -f arm32/libcrypto.so.1.1 -d /nd_lib/ -b /home/ubuntu/.nddevice/backup -m $arm32_libcry_md5sum -o root:root -p 775
status2=$(echo $?)
copy_file -f arm32/libssl.so.1.1 -d /usr/lib/ -b /home/ubuntu/.nddevice/backup -m $arm32_libssl_md5sum -o root:root -p 775
status3=$(echo $?)
copy_file -f arm32/libcrypto.so.1.1 -d /usr/lib/ -b /home/ubuntu/.nddevice/backup -m $arm32_libcry_md5sum -o root:root -p 775 
status4=$(echo $?)
copy_file -f arm64/libssl.so.1.1 -d /usr/lib64/ -b /home/ubuntu/.nddevice/backup -m $arm64_libssl_md5sum -o root:root -p 775
status5=$(echo $?)
copy_file -f arm64/libcrypto.so.1.1 -d /usr/lib64/ -b /home/ubuntu/.nddevice/backup -m $arm64_libcry_md5sum -o root:root -p 775
status6=$(echo $?)

log"===============start linking libssl.so and libcrypto.so libraries to ======================="

ln -sf libssl.so.1.1 /nd_lib/libssl.so
ln -sf libcrypto.so.1.1 /nd_lib/libcrypto.so
ln -sf libssl.so.1.1 /usr/lib/libssl.so
ln -sf libcrypto.so.1.1 /usr/lib/libcrypto.so
ln -sf libssl.so.1.1 /usr/lib64/libssl.so
ln -sf libcrypto.so.1.1 /usr/lib64/libcrypto.so
status7=$(echo $?)

log"===============end linking libssl.so and libcrypto.so libraries to ======================="

if [[ $status1 == 0 && $status2 == 0 && $status3 == 0 && $status4 == 0 && $status5 == 0 && $status6 == 0 && $status7 == 0 ]] ; then
	status=0
	check_status $(basename $(pwd)) $status
else
	status=1
	check_status $(basename $(pwd)) $status
fi

log "=============End of Copying libcrypto and libssl lib===================="





