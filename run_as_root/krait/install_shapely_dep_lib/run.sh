#!/usr/bin/env bash

set -e

source  /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "========= start  installing Shapely-1.6.4 python package and its dependent libs =========="

log "Copying shapely dependency libs into /usr/lib64 path"
tar -xzf libgeos.tar.gz  -C /usr/lib64/
status1=$(echo $?)

log "Installing Shapely-1.6.4 version"

pip3 install --target /usr/lib64/python3.5/site-packages/ Shapely-1.6.4.post2-py2.py3-none-any.whl
status2=$(echo $?)

if [[ $status1 == 0 && $status2 == 0 ]] ; then
        log "Copying libgeos dependent libs and shapely package installed successfully"
	status=0
	check_status $(basename $(pwd)) $status
else
        log "Something wrong to Copying libgeos dependent libs and shapely package installation. please check !!!!!!"
	status=1
	check_status $(basename $(pwd)) $status
fi

log "========= END of  installing Shapely-1.6.4 python package and its dependent libs =========="
