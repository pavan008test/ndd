#!/usr/bin/env bash
source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

awsiot_lib_path="/home/ubuntu/.nddevice/awsiot_lib/"

libssl_md5sum=$(md5sum bagheera/libssl.so.1.1 |awk '{print $1}')
libcry_md5sum=$(md5sum bagheera/libcrypto.so.1.1 |awk '{print $1}')
openssl_md5sum=$(md5sum bagheera/openssl |awk '{print $1}')

log "=============Copying the openssl, libcrypto and libssl=================="

copy_file -f bagheera/libssl.so.1.1 -d /usr/local/lib/ -b /home/ubuntu/.nddevice/backup -m $libssl_md5sum -o root:root -p 775
status1=$(echo $?)
copy_file -f bagheera/libcrypto.so.1.1 -d /usr/local/lib/ -b /home/ubuntu/.nddevice/backup -m $libcry_md5sum -o root:root -p 775
status2=$(echo $?)
copy_file -f bagheera/libcrypto.so.1.1 -d /usr/lib/aarch64-linux-gnu/ -b /home/ubuntu/.nddevice/backup -m $libcry_md5sum -o root:root -p 775 
status3=$(echo $?)
copy_file -f bagheera/libssl.so.1.1 -d /usr/lib/aarch64-linux-gnu/ -b /home/ubuntu/.nddevice/backup -m $libssl_md5sum -o root:root -p 775
status4=$(echo $?)
copy_file -f bagheera/openssl -d /usr/bin/ -b /home/ubuntu/.nddevice/backup -m $openssl_md5sum -o root:root -p 775
status5=$(echo $?)

log "===============start linking libssl.so and libcrypto.so libraries to ======================="

ln -sf libssl.so.1.1 /usr/local/lib/libssl.so
ln -sf libcrypto.so.1.1 /usr/local/lib/libcrypto.so
ln -sf libssl.so.1.1 /usr/lib/aarch64-linux-gnu/libssl.so
ln -sf libcrypto.so.1.1 /usr/lib/aarch64-linux-gnu/libcrypto.so
status6=$(echo $?)

log "===============end linking libssl.so and libcrypto.so libraries to ======================="

log "=============End of Copying openssl, libcrypto and libssl===================="

log "=============Copying the awsiot sdk libs=================="

log "Checking the libs directory in $current_ota_pkg_dir OTA package"
if [[ ! -d  ${awsiot_lib_path} ]] ; then
	log "${awsiot_lib_path} is not present, creating ${awsiot_lib_path} now"
	mkdir -p "${awsiot_lib_path}"
fi
log "Copying awsiot sdk libs to ${awsiot_lib_path}..."
cp -vfr --preserve bagheera/awsiot_lib/* ${awsiot_lib_path}
sync

status7=$(echo $?)

log "=============End of Copying awsiot sdk libs===================="

if [[ $status1 == 0 && $status2 == 0 && $status3 == 0 && $status4 == 0 && $status5 == 0 && $status6 == 0 && $status7 == 0 ]] ; then
	status=0
	check_status $(basename $(pwd)) $status
else
	status=1
	check_status $(basename $(pwd)) $status
fi
