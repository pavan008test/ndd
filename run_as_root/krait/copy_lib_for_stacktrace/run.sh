#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

libdw_md5sum=$(md5sum libdw-0.170.so | awk '{print $1}')
libelf_md5sum=$(md5sum libelf.so  | awk '{print $1}')

log "=============Copying the libdw-0.170 and libelf lib=================="
copy_file -f libdw-0.170.so -d /usr/lib/ -b /home/ubuntu/.nddevice/backup -m $libdw_md5sum -o root:root -p 775
status1=$(echo $?)
copy_file -f libelf.so -d /usr/lib/ -b /home/ubuntu/.nddevice/backup -m $libelf_md5sum -o root:root -p 775 
status2=$(echo $?)

log "===============start linking libdw-0.170 and libelf ======================="

ln -sf /usr/lib/libdw-0.170.so /usr/lib/libdw.so.1
ln -sf /usr/lib/libdw.so.1 /usr/lib/libdw.so
ln -sf /usr/lib/libelf.so /usr/lib/libelf.so.1
status3=$(echo $?)

log "===============end of linking linking libdw-0.170 and libelf ======================="

if [[ $status1 == 0 && $status2 == 0 && $status3 == 0 ]] ; then
	status=0
	check_status $(basename $(pwd)) $status
else
	status=1
	check_status $(basename $(pwd)) $status
fi

log "=============End of Copying and linking the libdw-0.170 and libelf lib===================="
