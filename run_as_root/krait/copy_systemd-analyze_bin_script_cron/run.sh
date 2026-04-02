#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

systemd_analyse_sh_md5sum=$(md5sum systemd_analyse.sh | awk '{print $1}')
systemd_analyze_bin_md5sum=$(md5sum systemd-analyze | awk '{print $1}')

log "=============Start of Copying systemd_analyse.sh and systemd-analyze files=================="
copy_file -f systemd_analyse.sh -d /home/ubuntu/.nddevice/ -b /home/ubuntu/.nddevice/backup -m $systemd_analyse_sh_md5sum -o root:root -p 775
status1=$(echo $?)
copy_file -f systemd-analyze -d /bin/ -b /home/ubuntu/.nddevice/backup -m $systemd_analyze_bin_md5sum -o root:root -p 775 
status2=$(echo $?)

if [[ $status1 == 0 && $status2 ]] ; then
	status=0
	check_status $(basename $(pwd)) $status
else
	status=1
	check_status $(basename $(pwd)) $status
fi

log "=============End of Copying systemd_analyse.sh and systemd-analyze files===================="
