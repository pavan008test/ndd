#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "=============Copying the libcrypto,libssl and libprotobuf lib=================="
CHECKSUM_proto=$(md5sum libprotobuf.so.14 | awk -F " " '{print $1}')
CHECKSUM_crypto=$(md5sum libcrypto.so.1.1 | awk -F " " '{print $1}')
CHECKSUM_libssl=$(md5sum libssl.so.1.1 | awk -F " " '{print $1}')

status1=$(copy_file -f libprotobuf.so.14 -d /usr/lib/aarch64-linux-gnu/ -b /home/ubuntu/.nddevice/backup -m ${CHECKSUM_proto} -p 644 -o root:root)$?
status2=$(copy_file -f libcrypto.so.1.1  -d /usr/lib/aarch64-linux-gnu/ -b /home/ubuntu/.nddevice/backup -m ${CHECKSUM_crypto} -p 644 -o root:root)$?
status3=$(copy_file -f libssl.so.1.1     -d /usr/lib/aarch64-linux-gnu/ -b /home/ubuntu/.nddevice/backup -m ${CHECKSUM_libssl} -p 644 -o root:root)$?
status4=$(copy_file -f libssl.so.1.1     -d /usr/local/lib/ -b /home/ubuntu/.nddevice/backup -m ${CHECKSUM_libssl} -p 644 -o root:root)$?
status5=$(copy_file -f libcrypto.so.1.1  -d /usr/local/lib/ -b /home/ubuntu/.nddevice/backup -m ${CHECKSUM_crypto} -p 644 -o root:root)$?

#linking to libprotobuf.so.14
status6=$(execute_command_sync ln -sf libprotobuf.so.14 /usr/lib/aarch64-linux-gnu/libprotobuf.so)$?

if [[ $status1 == 0 && $status2 == 0 && $status3 == 0 && $status4 == 0 && $status5 == 0 && $status6 == 0 ]] ; then
        status=0
        check_status $(basename $(pwd)) $status
else
        status=1
        check_status $(basename $(pwd)) $status
fi

log "=============End of Copying libcrypto,libssl and liprotobuf libs===================="
