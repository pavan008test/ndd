#!/usr/bin/env bash

SERVICE_PATH="/home/ubuntu/.nddevice/latest/service"
echo "checking the ${SERVICE_PATH}/conn_mgr_0.5.4 presence"
if [[ -d ${SERVICE_PATH}/conn_mgr_0.5.4 ]] ;then
	echo "${SERVICE_PATH}/conn_mgr_0.5.4 is present, stoping conn_mgr.service to revert it..."
	sudo systemctl stop conn_mgr.service
	mv ${SERVICE_PATH}/conn_mgr ${SERVICE_PATH}/conn_mgr_0.5.5
	echo "Moving 0.5.4 conn_mgr as active connection_mgr"
	mv ${SERVICE_PATH}/conn_mgr_0.5.4 ${SERVICE_PATH}/conn_mgr
	echo "Checking md5sum of 0.5.4 conn_mgr"
	if [[ $(md5sum ${SERVICE_PATH}/conn_mgr/connection_mgr | awk -F " " '{print $1}') == 494d41112aa603ae7c062e845aaa7250 ]] ; then
		echo "md5sum : $(md5sum ${SERVICE_PATH}/conn_mgr/connection_mgr) is matched, reverted successfully"
		echo "Starting conn_mgr.service to after revert"
		sudo systemctl start conn_mgr.service
 		[[ $? == 0 ]] && echo "conn_mgr.service is started ok" || echo "failing to start conn_mgr.service"
	else
		echo "[Error!] md5sum mismatched for 0.5.4 conn_mgr, revert failed"
	fi
else
	echo "${SERVICE_PATH}/conn_mgr_0.5.4 is not present to revert."
fi

