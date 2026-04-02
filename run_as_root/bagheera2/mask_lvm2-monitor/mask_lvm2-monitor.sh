
#!/usr/bin/env bash 

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "Masking the lvm2-monitor service"
sudo systemctl mask lvm2-monitor.service
[[ $? == 0 ]] || exit 1
sleep 2
log "Get the status of lvm2-monitor service"
sudo systemctl status lvm2-monitor.service >> /home/ubuntu/.nddevice/log/bhcopy.log
exit 0

