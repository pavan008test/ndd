#!/usr/bin/env bash

set -e

source  /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "========= Start of installing python packages =========="

log "installing timezonefinder-3.0.1"

pip3 install --no-deps --target /usr/lib64/python3.5/site-packages timezonefinder-3.0.1-py2.py3-none-any.whl
status2=$?
check_status $(basename $(pwd)) $status2

log "END of installing timezonefinder-3.0.1"



log "Start of installing pytz-2022.1"

pip3 install --no-deps --target /usr/lib64/python3.5/site-packages pytz-2022.1-py2.py3-none-any.whl
status1=$?
check_status $(basename $(pwd)) $status1

log "END of installing pytz-2022.1"



log "installing astrall-1.10.1"

pip3 install --no-deps --target /usr/lib64/python3.5/site-packages astral-1.10.1-py2.py3-none-any.whl
status1=$?
check_status $(basename $(pwd)) $status1

log "END of installing astrall-1.10.1"

log "========= END of installing python packages =========="
