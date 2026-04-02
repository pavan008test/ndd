#!/bin/sh

# Script for invoking the doop binary with 0/1 argument

ND_DEVICE_FILE_PATH="/home/ubuntu/.nddevice/nddevice.ini"
UPGRADE_VERSION=$(awk -F '=' '/^\[upgrade\]/{found=1} found && $1 ~ /nddevice/ {gsub(/ /, "", $2); print $2; exit}' "$ND_DEVICE_FILE_PATH")

# Export any required library paths
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/home/ubuntu/.nddevice/$UPGRADE_VERSION

# Run doop with the passed argument (0 or 1)
/home/ubuntu/.nddevice/$UPGRADE_VERSION/doop "$1"

echo "END OF doop.sh"

# Always return success regardless of doop's exit status
exit 0