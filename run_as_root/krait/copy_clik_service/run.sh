#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

# Securing ADB with clik_service.sh
log "======= Start of copying clik_service.sh task execution ======="

FILE_NAME="clik_service.sh"
DEST_DIR="/usr/bin/"
DEST_PATH="${DEST_DIR}/${FILE_NAME}"

# Get checksums
SOURCE_CHECKSUM=$(md5sum "$FILE_NAME" | awk '{print $1}')
DEST_CHECKSUM=""
[[ -f "$DEST_PATH" ]] && DEST_CHECKSUM=$(md5sum "$DEST_PATH" | awk '{print $1}')

log "Copying $FILE_NAME to $DEST_DIR"

if [[ ! -f "$DEST_PATH" || "$DEST_CHECKSUM" != "$SOURCE_CHECKSUM" ]]; then
    log "Latest $FILE_NAME not present at $DEST_DIR or checksum mismatch. Copying..."
    copy_file -f "$FILE_NAME" -d "$DEST_DIR" -b /home/ubuntu/.nddevice/backup -m "$SOURCE_CHECKSUM" -p 755 -o root:root
else
    log "$FILE_NAME already present at $DEST_DIR with correct checksum. Skipping copy..."
fi

log "============= End of copying clik_service.sh task execution ==================="

