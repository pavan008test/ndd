#!/bin/bash
set -euo pipefail

name="$1"

case "$name" in
  var-log)
    src="/var/log"
    dest="/media/data/overlay_var_log/upper"
    ;;
  nddevice-log)
    src="/home/ubuntu/.nddevice/log"
    dest="/media/data/overlay_nddevice_log/upper"
    ;;
  var-backups)
    src="/var/backups"
    dest="/media/data/overlay_var_backups/upper"
    ;;
  var-tmp)
    src="/var/tmp"
    dest="/media/data/overlay_var_tmp/upper"
    ;;
  *)
    logger "sync-early: unknown instance '$name'"
    exit 1
    ;;
esac

logger "sync-early[$name]: rsync $src -> $dest"
rsync -a --inplace --no-compress "$src"/ "$dest"/

logger "sync-early[$name]: deleting file contents of $src (preserving folder structure)"
find "$src" -type f -delete

logger "sync-early[$name]: done"
