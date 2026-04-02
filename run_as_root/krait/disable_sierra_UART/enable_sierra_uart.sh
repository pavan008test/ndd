#!/usr/bin/env bash
set -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

updating_version=$(grep -A1 upgrade "/home/ubuntu/.nddevice/nddevice.ini" | awk -F "=" '{print $2}' | tail -1 | tr -d " ")
log "Checking for lumia enumeration..."
for (( i=0; i<5; i++ )) ; do
    if [[ -e /dev/qcqmi0 ]]; then
	log "lumia enumerated"
	break
    else
	log "Not enumerated. Sleeping 5 secs and trying again"
	sleep 5
	continue
    fi
done
uart_status=$(lte_gps_sample_app 'AT!MAPUART?' | grep '!MAPUART:' | awk -F " " '{print $2}')
log "Checking for UART enable or disable on Sierra..."
if [ ${uart_status} == "17,16" ]; then
    log "UART on Sierra is already enable, skiping the revert action..."
else
    log "UART on sierra is disabled, taking action to enable it..."
    log "Executing AT commands - lte_gps_sample_app 'AT!MAPUART=17,1'"
    lte_gps_sample_app 'AT!MAPUART=17,1'
    log "Executing AT commands - lte_gps_sample_app 'AT!MAPUART=16,2'"
    lte_gps_sample_app 'AT!MAPUART=16,2'
    uart_status=$(lte_gps_sample_app 'AT!MAPUART?' | grep '!MAPUART:' | awk -F " " '{print $2}')
    log "Checking above UART enable execution status"
    if [[ ${uart_status}  == "17,16" ]]; then
        log "UART is enabled ok. Reset the lumia..."
        if [[ $(systemctl list-units --all -t service --full --no-legend "conn_mgr.service" | cut -f1 -d' ') == "conn_mgr.service" ]] ; then
            echo "conn_mgr_service is exist, stop in process"
            systemctl stop conn_mgr
        fi
        sleep 1
        python3 /home/ubuntu/.nddevice/$updating_version/service/conn_mgr/lumia_reset.py
        sleep 1
        systemctl start conn_mgr
    else
        log "Enable UART action is failed..."
    fi
fi
