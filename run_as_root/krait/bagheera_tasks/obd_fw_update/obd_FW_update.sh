#!/bin/sh

set -e

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh
BASE_PATH=/home/ubuntu/.nddevice/ota_temp/run_as_root

log "============Start of obd_FW_update script================="
log "Removing if any old status file is present"
rm -f /home/ubuntu/.nddevice/obd/obd_upgrade_status.txt
log "Creating the status files app1 and app2"
touch /home/ubuntu/.nddevice/ota_temp/app1.txt
touch /home/ubuntu/.nddevice/ota_temp/app2.txt

log "Executing the obd_fm_upgrade script from bin folder for APP1"
$BASE_PATH/obd_fw_update/obd_fm_upgrade.sh --updatePath $BASE_PATH/obd_fw_update/obd/update/App1.s19 --stablePath $BASE_PATH/obd_fw_update/obd/stable/App1.s19 --partitionId 35

val=$?
log "Recording the run status of APP1 script to app1.txt"
echo "status $val" > /home/ubuntu/.nddevice/ota_temp/app1.txt

log "Executing the obd_fm_upgrade script from bin folder for APP2"
$BASE_PATH/obd_fw_update/obd_fm_upgrade.sh --updatePath $BASE_PATH/obd_fw_update/obd/update/App2.s19 --stablePath $BASE_PATH/obd_fw_update/obd/stable/App2.s19 --partitionId 36

val=$?
log "Recording the run status of APP2 script to app2.txt"
echo "status $val" > /home/ubuntu/.nddevice/ota_temp/app2.txt

log "============End of obd_FW_update script================="



