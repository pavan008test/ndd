#!/usr/bin/env bash

set -e
source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

#Script to fix the cron service

log "==============Start of delete_db_entries================="

log "Taking back up of original file DB to backup path"
backup /home/ubuntu/.nddevice/circular_buffer.db /home/ubuntu/.nddevice/backup/

log "Removing the .zip entries in circular_buffer.db and recording into the /home/ubuntu/.nddevice/backup/cb_zip_removed.txt "
set  -x

sudo sqlite3 /home/ubuntu/.nddevice/circular_buffer.db <<EOF
.output /home/ubuntu/.nddevice/backup/cb_zip_total.txt
.mode csv
SELECT COUNT(*) FROM VIDFILES;
.output /home/ubuntu/.nddevice/backup/cb_zip_removed.txt
.mode csv
SELECT * FROM VIDFILES WHERE TYPE == 7 AND STATUS == 2;
DELETE FROM VIDFILES WHERE TYPE == 7 AND STATUS == 2;
.output /home/ubuntu/.nddevice/backup/cb_zip_total2.txt
.mode csv
SELECT COUNT(*) FROM VIDFILES;
EOF

total_count=$(cat /home/ubuntu/.nddevice/backup/cb_zip_total.txt)
total_count2=$(cat /home/ubuntu/.nddevice/backup/cb_zip_total2.txt)
removed_count=$(cat /home/ubuntu/.nddevice/backup/cb_zip_removed.txt | wc -l )
log "Total number of rows in circular_buffer db : $total_count"
log "Count of removed rows from circular_buffer db : $removed_count"
log "Total number of rows after remove of *.zip : $total_count2"
log "==============End of delete_db_entries================="

exit 0
