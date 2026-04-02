#!/usr/bin/env bash
source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

arm32_libboost_system_md5sum=$(md5sum arm32/libboost_system.so.1.64.0  |awk '{print $1}')
arm32_libboost_filesystem_md5sum=$(md5sum arm32/libboost_filesystem.so.1.64.0 |awk '{print $1}')
arm64_libboost_system_md5sum=$(md5sum arm64/libboost_system.so.1.64.0 |awk '{print $1}')
arm64_libboost_filesystem_md5sum=$(md5sum arm64/libboost_filesystem.so.1.64.0 |awk '{print $1}')

log "=============Copying the libboost_filesystem and libboost_system lib=================="

copy_file -f arm32/libboost_system.so.1.64.0 -d /usr/lib/ -b /home/ubuntu/.nddevice/backup -m $arm32_libboost_system_md5sum -o root:root -p 775
status1=$(echo $?)
copy_file -f arm32/libboost_filesystem.so.1.64.0 -d /usr/lib/ -b /home/ubuntu/.nddevice/backup -m $arm32_libboost_filesystem_md5sum -o root:root -p 775 
status2=$(echo $?)
copy_file -f arm64/libboost_system.so.1.64.0 -d /usr/lib64/ -b /home/ubuntu/.nddevice/backup -m $arm64_libboost_system_md5sum -o root:root -p 775
status3=$(echo $?)
copy_file -f arm64/libboost_filesystem.so.1.64.0 -d /usr/lib64/ -b /home/ubuntu/.nddevice/backup -m $arm64_libboost_filesystem_md5sum -o root:root -p 775
status4=$(echo $?)

log "===============start linking libboost_filesystem and libboost_system libraries ======================="

ln -sf libboost_system.so.1.64.0  /usr/lib/libboost_system.so
ln -sf libboost_filesystem.so.1.64.0 /usr/lib/libboost_filesystem.so
ln -sf libboost_system.so.1.64.0 /usr/lib64/libboost_system.so
ln -sf libboost_filesystem.so.1.64.0 /usr/lib64/libboost_filesystem.so
status5=$(echo $?)

log "===============end linking libboost_filesystem and libboost_system libraries ======================="

if [[ $status1 == 0 && $status2 == 0 && $status3 == 0 && $status4 == 0 && $status5 == 0 ]] ; then
	status=0
	check_status $(basename $(pwd)) $status
else
	status=1
	check_status $(basename $(pwd)) $status
fi

log "=============End of Copying and linking of libboost_filesystem and libboost_system lib===================="





