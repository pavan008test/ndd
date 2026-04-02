#!/usr/bin/env bash

set -e

source  /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "=============Start of Reverting ntdi_bag3_startup, libsys, ltc3350_pwr_loop ================="


if [[ -f /home/ubuntu/.nddevice/backup/ntdi_bag3_startup ]]; then

	log "Reverting ntdi_bag3_startup to older one as 13.0.16"
	cp -f /home/ubuntu/.nddevice/backup/ntdi_bag3_startup /etc/init.d/
	status1=$?
else
	log "No ntdi_bag3_startup file in backup"
	status1=0

fi

if [[ -f /home/ubuntu/.nddevice/backup/libsys.so ]]; then

	log "Reverting libsys.so to older one as 13.0.16"
	cp -f /home/ubuntu/.nddevice/backup/libsys.so /lib/
	status2=$?
else
	log "No libsys.so file in backup"
	status2=0

fi

if [[ -f /home/ubuntu/.nddevice/backup/ltc3350_pwr_loop ]]; then

	log "Reverting ltc3350_pwr_loop to older one as 13.0.16"
	cp -f /home/ubuntu/.nddevice/backup/ltc3350_pwr_loop /bin/vendor/
	status3=$?
else
	log "No ltc3350_pwr_loop file in backup"
	status3=0

fi

log "Verifying the status of above copies in thie task"
if [[ $status1 != 0 ]]; then log "status1 failed !!!!" ; check_status $(basename $(pwd)) 1 ; fi
if [[ $status2 != 0 ]]; then log "status2 failed !!!!" ; check_status $(basename $(pwd)) 1 ; fi
if [[ $status3 != 0 ]]; then log "status3 failed !!!!" ; check_status $(basename $(pwd)) 1 ; fi

check_status $(basename $(pwd)) 0

log "===============End of Reverting ntdi_bag3_startup==================="
