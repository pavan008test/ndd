#!/bin/bash

function log {
echo -n "`date +%Y-%m-%d` `date +"%T,%3N"` - __run_as_root@reboot__ - INFO - ">> /home/ubuntu/.nddevice/log/bhcopy.log 2>&1
echo $1 >> /home/ubuntu/.nddevice/log/bhcopy.log 2>&1
}

function check_status {
if [[ $1 == 0 ]]; then 
log "last command is success"
else
log "last command failed"
fi
}

function self_destruct {
systemctl disable py_reboot_recovery.service
rm -f /etc/systemd/system/py_reboot_recovery.service
rm -f /home/ubuntu/.nddevice/py3.5_reboot_recovery.sh
}

systemctl stop run_as_root ; systemctl disable run_as_root

new_version=$(grep -A1 upgrade "/home/ubuntu/.nddevice/nddevice.ini" | awk -F "=" '{print $2}' | tail -1 | tr -d " ")
latest=$(readlink /home/ubuntu/.nddevice/latest)
major_version=$(readlink /home/ubuntu/.nddevice/latest | cut -d '.' -f1-3)

if [[ $new_version != $major_version* ]]; then 

log "============= Starting revert of install_python3.9 from reboot script =================="
log "Checking the local folder in the /usr/ path and removeing if exists"
rm -rf /usr/local/
status=$(echo $?)
check_status $status

log "Removing the tar files in ota_temp foler"
rm -rf /home/ubuntu/.nddevice/ota_temp/run_as_root/install_python3.9/*.tar.gz
rm -rf /home/ubuntu/.nddevice/ota_temp/run_as_root/scipy_copy_python3.9/scipy*
status1=$(echo $?)
check_status $status1

log "updating the python3 alternatives to python3.5 "
update-alternatives --remove python3 /usr/local/bin/python3.9
update-alternatives --install /usr/bin/python3 python3 /usr/bin/python3.5 2
status3=$(echo $?)
check_status $status3

log "=============End of revert of install_python3.9 from reboot script==================="

self_destruct
exit 0

elif [[ $new_version == $major_version* ]]; then 
  log "latest linking is similar as new version : $new_version. So need of revert of py3.9"
  self_destruct
  exit 0
else 
  log "Something went wrong in last update. Not able to get proper upgrade_version from nddevice.ini and also not matching with latest folder name !!!!!!!!!!"
  rm -rf /home/ubuntu/.nddevice/ota_temp/run_as_root/install_python3.9/*.tar.gz
  rm -rf /home/ubuntu/.nddevice/ota_temp/run_as_root/scipy_copy_python3.9/scipy*
  self_destruct
  exit 1 
fi

