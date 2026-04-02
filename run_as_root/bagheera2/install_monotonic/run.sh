#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

set -e

log "================Start of Installing Monotonic wheel file==================="

pip install monotonic-1.6-py2.py3-none-any.whl 
status=$?
if [[ $status == 0 ]]; then
	log "Monotonic wheel file Successfully Installed"
else
	log "Monotonic wheel file not  Installed properly please check...!!!"
fi

log "=================End of Installing Monotonic wheel file================="
