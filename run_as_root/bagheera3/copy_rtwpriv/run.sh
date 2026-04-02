#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "============ Start of copying rtwpriv ==========="

md5sum_rtwpriv=$(md5sum rtwpriv | awk '{print $1}')
md5sum_rtwpriv_system=$(md5sum /bin/vendor/rtwpriv | awk '{print $1}')

log "Copying /bin/vendor/rtwpriv"
if [[ -f /bin/vendor/rtwpriv && "$md5sum_rtwpriv" == "$md5sum_rtwpriv_system" ]]; then
    log "md5sum of rtwpriv is as expected. Skipping copy."
    status=0
else
    log "rtwpriv is missing or md5 mismatch. Copying..."
    copy_file -f rtwpriv -d /bin/vendor/  -m "$md5sum_rtwpriv" -p 775 -o root:root
    status=$?
    log "Copied rtwpriv  to bin folder"
fi

check_status $(basename $(pwd)) $status


log "============ End of copying rtwpriv to /bin/vendor/ ==========="
