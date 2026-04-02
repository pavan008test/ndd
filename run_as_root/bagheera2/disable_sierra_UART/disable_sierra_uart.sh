#!/usr/bin/env bash
set -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#updating_version=$(grep -A1 upgrade "/home/ubuntu/.nddevice/nddevice.ini" | awk -F "=" '{print $2}' | tail -1 | tr -d " ")
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
uart_status=$(sudo lte_gps_sample_app 'AT!MAPUART?' | grep '!MAPUART:' | awk -F " " '{print $2}' | tr -d '\r')
log "Checking for UART enable or disable on Sierra..."

if [ ${uart_status} == "0,0" ]; then 
    log "UART is already disabled, skiping the action to reset UART."
else
    log "UART is enabled, taking action to disable it..."
    log "Executing AT commands - lte_gps_sample_app 'AT!MAPUART=0,1'"
    sudo lte_gps_sample_app 'AT!MAPUART=0,1'
    log "Executing AT commands - lte_gps_sample_app 'AT!MAPUART=0,2'"
    sudo lte_gps_sample_app 'AT!MAPUART=0,2'
    uart_status=$(sudo lte_gps_sample_app 'AT!MAPUART?' | grep '!MAPUART:' | awk -F " " '{print $2}' | tr -d '\r')
    log "Checking above UART disable execution status"
    if [[ ${uart_status} == "0,0" ]]; then
    	log "UART is disabled ok. Reset the lumia..."
    	sudo systemctl stop conn_mgr
        sleep 2
	log "Resetting lumia sierra modem"
        /bin/vendor/gpio_test -n 235 -o 0 > /dev/null
        sleep 5
        /bin/vendor/gpio_test -n 235 -o 1 > /dev/null
        sudo systemctl start conn_mgr
    else
        log "Disable UART action is failed..."
    fi
fi
