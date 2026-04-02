#!/bin/bash

HOTSPOT="DriveriHostspot"
CONNECTIONS_DIR="/etc/NetworkManager/system-connections"

echo "Bringing down connection: $HOTSPOT"
nmcli con down "$HOTSPOT" 2>/dev/null || echo "$HOTSPOT may not be active."

echo "Removing all connections except those with id: ${HOTSPOT}*"

for f in "$CONNECTIONS_DIR"/*; do
    # Extract connection id from file (ini-style)
    CON_ID=$(grep '^id=' "$f" | cut -d'=' -f2)

    if [[ "$CON_ID" == ${HOTSPOT}* ]]; then
        echo "Keeping: $f (id=$CON_ID)"
    else
        echo "Removing: $f (id=$CON_ID)"
        sudo rm -f "$f"
    fi
done
echo "Reloading NetworkManager configuration"  
sudo nmcli connection reload