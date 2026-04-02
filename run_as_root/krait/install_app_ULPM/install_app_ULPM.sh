#!/bin/bash -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

IP_ADDRESS="192.168.2.2"
CHECKSUM=$(md5sum ulpm.wp750x.update | awk '{print $1}')
function install_app_ulpm (){
    if [[ $(ifconfig usb0 | grep "192.168.2.") ]] ; then
        log " ========= Cleaning 0 bytes files from Lumia log folder ========="
        ssh -oStrictHostKeyChecking=no root@${IP_ADDRESS} 'find /home/root/log/ulpm/ -type f -size 0c | xargs rm -f'
        log "========== Copy update file to Lumia ========="
        scp -o StrictHostKeyChecking=no ulpm.wp750x.update root@${IP_ADDRESS}:/home/root
        if [[ $(ssh -t -o StrictHostKeyChecking=no root@${IP_ADDRESS} '/usr/bin/md5sum /home/root/ulpm.wp750x.update' | awk '{print $1}')  == "${CHECKSUM}" ]]; then
            log "========== Install app ULPM on Lumia ========="
            log "First removing the existing ULPM app"
            ssh -t -o StrictHostKeyChecking=no root@${IP_ADDRESS} '/legato/systems/current/bin/update --remove ulpm'
            log "Installing app ....."
            ssh -t -o StrictHostKeyChecking=no root@${IP_ADDRESS} '/legato/systems/current/bin/update /home/root/ulpm.wp750x.update'
            [[ $? == 0 ]] || ( log "Updating app failed !! exiting" ; exit 1 )
            log " ========= Marking the system as good after update. This is to avoid possible system rollback ========="
            ssh -t -o StrictHostKeyChecking=no root@${IP_ADDRESS} '/legato/systems/current/bin/update -g'
            # add a condition which can define the update is successfully done before "update_done = yes"
            UPDATE_DONE="yes"
        elif [[ ${retry} == "1" ]] ; then
            log " ========= Md5sum is not matching. Update file would not been copied !! exiting ========="
            exit 1
        else
            log " ========= Copying update file to Lumia again as the checksum is not matched with existing file ========="
            scp -o StrictHostKeyChecking=no ulpm.wp750x.update root@${IP_ADDRESS}:/home/root
        fi
    elif [[ ${retry} == "1" ]] ; then
        log " =========  Unable to find USB after 3rd retry also !! exiting ========="
        exit 1
    else
        log " =========  Restart dhcpcd as interface is not found. ========="
        pkill -KILL dhcpcd
        dhcpcd usb0
	sleep 20
    fi
}

retry=4
until [ $retry -le 0 ] ; do
  if [[ ${UPDATE_DONE}  == "yes" ]] ; then
    log " ========= Install app ULPM on Lumia is Done, successfully ========="
    exit 0
  else
    install_app_ulpm
    retry=$[$retry-1]
    sleep 2
  fi
done
