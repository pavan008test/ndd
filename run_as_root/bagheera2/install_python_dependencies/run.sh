#!/usr/bin/env bash

set -e

source  /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh


log "========= Start of installing python packages =========="

log "installing astrall-1.10.1"
pip install astral-1.10.1-py2.py3-none-any.whl
status1=$?
check_status $(basename $(pwd)) $status1

log "installing timezonefinder-3.0.1"
pip install timezonefinder-3.0.1-py2.py3-none-any.whl
status2=$?
check_status $(basename $(pwd)) $status2

log "installing requests-2.18.1 python package and its dependencies"
log "installing chardet-3.0.4"
pip install chardet-3.0.4-py2.py3-none-any.whl
log "installing certifi-2017.4.17"
pip install certifi-2017.4.17-py2.py3-none-any.whl
log "installing idna-2.5"
pip install idna-2.5-py2.py3-none-any.whl
log "installing urllib3-1.21.1"
pip install urllib3-1.21.1-py2.py3-none-any.whl
log "installing request-2.18.1"
pip install requests-2.18.1-py2.py3-none-any.whl
status3=$?
check_status $(basename $(pwd)) $status3

log "========= End of installing python packages ========="
