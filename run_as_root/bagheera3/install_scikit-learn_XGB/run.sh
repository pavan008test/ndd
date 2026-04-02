#!/usr/bin/env bash

source  /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "==========Removing the pandas folder======="
rm -rf /usr/lib/python3/dist-packages/pandas
rm -rf /usr/lib/python3/dist-packages/pandas-0.22.0.egg-info
log "===pandas folder has been removed successfully==="

log "=============Extracting tar file============="
tar -xzf install_XGB_scikit-learn.tar.gz
status1=$?
if [[ $status1 == 0 ]] ; then log "Extracted successfully" ; else log "Extraction  not successful. Exiting..."; exit 1 ; fi;
chmod +x *

log "===============Start of Installing scikit-learn 1.1.2 with its dependencies and XGB 1.6.1 whl files============"

log "Installing the threadpoolctl-3.1.0-py3-none-any.whl"
sudo pip3 -v install threadpoolctl-3.1.0-py3-none-any.whl

log "Installing the joblib-1.2.0-py3-none-any.whl"
sudo pip3 -v install joblib-1.2.0-py3-none-any.whl

log "Installing the scikit_learn-1.1.2-cp39-cp39-manylinux_2_17_aarch64.manylinux2014_aarch64.whl"
sudo pip3 -v install scikit_learn-1.1.2-cp39-cp39-manylinux_2_17_aarch64.manylinux2014_aarch64.whl
status2=$?
check_status $(basename $(pwd)) $status2

log "Installing the xgboost-1.6.1-py3-none-manylinux2014_aarch64.whl"
sudo pip3 -v install xgboost-1.6.1-py3-none-manylinux2014_aarch64.whl
status3=$?
check_status $(basename $(pwd)) $status3

log "==============End of installing scikit-learn 1.1.2 with its dependencies and XGB 1.6.1 whl files===================="
