#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Calling function from lib to replace/upgrade the nd_service.sh @ /usr/bin/
status=$(copy_file -f wifi_modules.sh -d /etc -m 41d27a30625f22f488b6afe17d8bb593 -p 755 -o root:root)$?
check_status $(basename $(pwd)) $status

