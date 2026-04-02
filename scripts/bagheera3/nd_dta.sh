#!/bin/sh

#script for invoking the nd_dta service

NDDEVICE_PATH=/home/ubuntu/.nddevice/

grep -R 'server = prod' /home/ubuntu/.nddevice/latest/cloudconfig.ini
rc=$?
echo "grep exit code: $rc"
if [ $rc -ne 0 ]; then
  echo "staging"
  sudo systemctl start ssh
fi

$NDDEVICE_PATH/latest/service/nd_dta/nd_dta
echo END OF SCRIPT
