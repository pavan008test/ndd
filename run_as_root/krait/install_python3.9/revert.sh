#!/usr/bin/env bash


source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

new_version=$(grep -A1 upgrade "/home/ubuntu/.nddevice/nddevice.ini" | awk -F "=" '{print $2}' | tail -1 | tr -d " ")
#Calling execute_script function from lib
log "============= Starting revert of install_python3.9 =================="

log "checking the upgrade version and latest version"
new_version=$(grep -A1 upgrade "/home/ubuntu/.nddevice/nddevice.ini" | awk -F "=" '{print $2}' | tail -1 | tr -d " ")
latest=$(readlink /home/ubuntu/.nddevice/latest)
major_version=$(readlink /home/ubuntu/.nddevice/latest | cut -d '.' -f1-3)

if [[ $new_version != $major_version* ]]; then

log "============= Starting revert of install_python3.9 from reboot script =================="
log "Checking the local folder in the /usr/ path and removeing if exists"
rm -rf /usr/local/
status=$(echo $?)
check_status revert_$(basename $(pwd)) $status

log "Removing the tar files in ota_temp foler"
rm -rf /home/ubuntu/.nddevice/ota_temp/run_as_root/install_python3.9/*.tar.gz
rm -rf /home/ubuntu/.nddevice/ota_temp/run_as_root/scipy_copy_python3.9/scipy*
status1=$(echo $?)
check_status revert_$(basename $(pwd)) $status1

log "updating the python3 alternatives to python3.5 "
update-alternatives --remove python3 /usr/local/bin/python3.9
update-alternatives --install /usr/bin/python3 python3 /usr/bin/python3.5 2
status3=$(echo $?)
check_status revert_$(basename $(pwd)) $status3

elif [[ $new_version == $major_version* ]]; then
  log "latest linking is similar as new version : $new_version. So need of revert of py3.9"

else
  log "Something went wrong in last update.upgrade_version from nddevice.ini not matching with latest folder name !!!!!!!!!!"
  exit 1
fi


log "=============End of revert adb secure==================="

