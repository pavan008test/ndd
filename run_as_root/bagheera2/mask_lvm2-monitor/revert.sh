
#!/usr/bin/env bash 

set -e 

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "===== Start of revert masking lvm2-monitor ====="
current_ota=$(cat /home/ubuntu/.nddevice/nddevice.ini | grep -a1 version | grep nddevice | awk -F "=" '{print $2}' | tr -d ' ' | cut -d "." -f 1-3)
ota_list=(0.5.3 0.5.4)
for version in ${ota_list[@]}
do
    if [[ $current_ota == $version ]]
    then
	log "This revert task is not required for this current OTA $current_ota"
      exit 0
    fi
done

log "UnMasking the lvm2-monitor service"
sudo systemctl unmask lvm2-monitor.service
sleep 2
log "Get the status of lvm2-monitor service"
sudo systemctl status lvm2-monitor.service >> /home/ubuntu/.nddevice/log/bhcopy.log

log "===== End of revert masking lvm2-monitor ====="

