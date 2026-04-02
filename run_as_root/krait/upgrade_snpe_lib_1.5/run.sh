#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#upgrading the required snpe lib @ /nd_lib
log "======= Start of upgrade_snpe_lib task execution ======="

#Checking snpe libraries are present or not for 1.42 version in /nd_lib/snpe/

        if [[ ! -d "/nd_lib/snpe/arm-oe-linux-gcc6.4hf_142" ]] ; then
                log "arm-oe-linux-gcc6.4hf is present for 1.42 snpe version. Renaming arm-oe-linux-gcc6.4hf folder to arm-oe-linux-gcc6.4hf_142"
		mv /nd_lib/snpe/arm-oe-linux-gcc6.4hf /nd_lib/snpe/arm-oe-linux-gcc6.4hf_142
		log "Updating the snpe version to 1.5*"
		#Untaring snpe libs and dependencies files tar directly into /nd_lib/snpe/
		tar -xzf  snpe-1.52_libs.tar.gz -C /nd_lib/snpe/
		status=$?
        else
		log "arm-oe-linux-gcc6.4hf_142 folder is present in /nd_lib/snpe folder. checking for /nd_lib/snpe/arm-oe-linux-gcc6.4hf folder"
		if [[ -d "/nd_lib/snpe/arm-oe-linux-gcc6.4hf" ]] ; then
                log "============= snpe is already updated to 1.5* version. skipping the action to upgrade snpe version ==================="
		check_status $(basename $(pwd)) 0
		exit 0
		else
                log "/nd_lib/snpe/arm-oe-linux-gcc6.4hf is not present , extracting 1.5* version snpe libraries into /nd_lib/snpe/ path"
		tar -xzf  snpe-1.52_libs.tar.gz -C /nd_lib/snpe/
		status=$?
		fi
        fi
if [[ $status == 0 ]]; then
        log "SNPE libs upgraded Successfully..!"
else
        log "SNPE libs upgration is failed .Please Check...!"
fi
check_status $(basename $(pwd)) $status

log "============= End of upgrade_snpe_lib task execution ==================="
