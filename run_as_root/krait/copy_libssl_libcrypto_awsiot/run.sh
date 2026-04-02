#!/usr/bin/env bash
source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

awsiot_lib_path="/home/ubuntu/.nddevice/awsiot_lib/"

arm32_libssl_md5sum=$(md5sum krait/arm32/libssl.so.1.1 |awk '{print $1}')
arm32_libcry_md5sum=$(md5sum krait/arm32/libcrypto.so.1.1 |awk '{print $1}')
arm64_libssl_md5sum=$(md5sum krait/arm64/libssl.so.1.1 |awk '{print $1}')
arm64_libcry_md5sum=$(md5sum krait/arm64/libcrypto.so.1.1 |awk '{print $1}')
arm64_openssl_md5sum=$(md5sum krait/arm64/openssl |awk '{print $1}')

log "=============Copying the openssl, libcrypto and libssl=================="

copy_file -f krait/arm32/libssl.so.1.1 -d /nd_lib/ -b /home/ubuntu/.nddevice/backup -m $arm32_libssl_md5sum -o root:root -p 775
status1=$(echo $?)
copy_file -f krait/arm32/libcrypto.so.1.1 -d /nd_lib/ -b /home/ubuntu/.nddevice/backup -m $arm32_libcry_md5sum -o root:root -p 775
status2=$(echo $?)
copy_file -f krait/arm32/libssl.so.1.1 -d /usr/lib/ -b /home/ubuntu/.nddevice/backup -m $arm32_libssl_md5sum -o root:root -p 775
status3=$(echo $?)
copy_file -f krait/arm32/libcrypto.so.1.1 -d /usr/lib/ -b /home/ubuntu/.nddevice/backup -m $arm32_libcry_md5sum -o root:root -p 775 
status4=$(echo $?)
copy_file -f krait/arm64/libssl.so.1.1 -d /usr/lib64/ -b /home/ubuntu/.nddevice/backup -m $arm64_libssl_md5sum -o root:root -p 775
status5=$(echo $?)
copy_file -f krait/arm64/libcrypto.so.1.1 -d /usr/lib64/ -b /home/ubuntu/.nddevice/backup -m $arm64_libcry_md5sum -o root:root -p 775
status6=$(echo $?)
copy_file -f krait/arm64/openssl -d /usr/bin/ -b /home/ubuntu/.nddevice/backup -m $arm64_openssl_md5sum -o root:root -p 775
status7=$(echo $?)

log "===============start linking libssl.so and libcrypto.so libraries to ======================="

ln -sf libssl.so.1.1 /nd_lib/libssl.so
ln -sf libcrypto.so.1.1 /nd_lib/libcrypto.so
ln -sf libssl.so.1.1 /usr/lib/libssl.so
ln -sf libcrypto.so.1.1 /usr/lib/libcrypto.so
ln -sf libssl.so.1.1 /usr/lib64/libssl.so
ln -sf libcrypto.so.1.1 /usr/lib64/libcrypto.so
status8=$(echo $?)

log "===============end linking libssl.so and libcrypto.so libraries to ======================="

log "=============End of Copying openssl, libcrypto and libssl===================="

log "=============Copying the awsiot sdk libs=================="

log "Checking the libs directory in $current_ota_pkg_dir OTA package"
if [[ ! -d  ${awsiot_lib_path} ]] ; then
	log "${awsiot_lib_path} is not present, creating ${awsiot_lib_path} now"
	mkdir -p "${awsiot_lib_path}"
fi
log "Copying awsiot sdk libs to ${awsiot_lib_path}..."
cp -vfr --preserve krait/awsiot_lib/* ${awsiot_lib_path}
sync

status9=$(echo $?)

log "=============End of Copying awsiot sdk libs===================="

if [[ $status1 == 0 && $status2 == 0 && $status3 == 0 && $status4 == 0 && $status5 == 0 && $status6 == 0 && $status7 == 0 && $status8 == 0 && $status9 == 0 ]] ; then
	status=0
	check_status $(basename $(pwd)) $status
else
	status=1
	check_status $(basename $(pwd)) $status
fi
