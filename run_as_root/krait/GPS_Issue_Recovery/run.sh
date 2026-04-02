#!/usr/bin/env bash

set -e

source  /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh


md5sum_blacklist_conf_system=$(md5sum /etc/modprobe.d/blacklist.conf | awk '{print $1}')
md5sum_blacklist_conf=$(md5sum blacklist.conf | awk '{print $1}')

log "========= Start of copying blacklist.conf file into /etc/modprobe.d/ path =========="

if [[ $md5sum_blacklist_conf_system == $md5sum_blacklist_conf ]]; then

	log "Already file is present  there so not copying"
	status=0
else
	log "Copying blacklist.conf in /etc/modprobe.d/ path "
	status=$(copy_file -f blacklist.conf -d /etc/modprobe.d/ -b /home/ubuntu/.nddevice/backup/ -m $md5sum_blacklist_conf -p 644)$?

fi

check_status $(basename $(pwd)) $status

log "========= End of  copying blacklist.conf file into /etc/modprobe.d/ path ========="
