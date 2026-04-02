#!/bin/bash

# Interface passed as first argument
IFACE="$1"

# Configuration
IPADDR="172.17.0.2/24"
GATEWAY="172.17.0.1"
LOGFILE="/tmp/ip_setup.log"
DHCPDL="/var/lib/dhcp/dhcpd.leases"
DHCPDC="/etc/dhcp/dhcpd.conf"
DHCP_SERVER_START_DELAY=5
IP_BINARY="/sbin/ip"
NM_CONF_DIR="/etc/NetworkManager/conf.d"
NM_CONF_FILE="$NM_CONF_DIR/10-unmanaged-interfaces.conf"

ATTEMPT=0
MAX_ATTEMPTS=2

echo "-------------------------" >> "$LOGFILE"
echo "$(date): Starting IP setup for $IFACE" >> "$LOGFILE"

# Validate interface argument
if [ -z "$IFACE" ]; then
    echo "Usage: $0 <interface>" >> "$LOGFILE"
    exit 1
fi

if [ ! -f "$IP_BINARY" ]; then
    echo "$(date): ERROR - $IP_BINARY is absent. Exiting." >> "$LOGFILE"
    exit 1
fi

# Wait for interface to be available
for i in {1..10}; do
    if "$IP_BINARY" link show "$IFACE" > /dev/null 2>&1; then
        echo "$(date): Interface $IFACE detected" >> "$LOGFILE"
        break
    fi
    echo "$(date): Waiting for $IFACE to be detected..." >> "$LOGFILE"
    sleep 1
done

# Final check if interface was not found
if ! "$IP_BINARY" link show "$IFACE" > /dev/null 2>&1; then
    echo "$(date): ERROR - Interface $IFACE not found after waiting. Exiting." >> "$LOGFILE"
    exit 1
fi

# Mark as unmanaged in NetworkManager
# NetworkManager assigns some default IP to the interface, since we require this to be
# the server and exist in assigned subnet, we need to make sure NM is not managing this interface
if command -v nmcli >/dev/null 2>&1; then
    echo "$(date): Marking $IFACE as unmanaged in NetworkManager" >> "$LOGFILE"
    nmcli dev set "$IFACE" managed no >> "$LOGFILE" 2>&1
    STATUS=$?
    if [ "$STATUS" -ne 0 ];then
        echo "$(date): ERROR - Failed to disable NetworkManager at managing Interface $IFACE. Exiting." >> "$LOGFILE"
        exit 1
    fi
    sleep 1

    # Persist unmanaged state across Network Manager Restarts using a keyfile in conf.d
    if [ -d "$NM_CONF_DIR" ]; then
        # Create or update the unmanaged-devices entry for this interface (idempotent)
        echo "$(date): Ensuring persistent NM unmanaged for $IFACE via $NM_CONF_FILE" >> "$LOGFILE"
        {
            echo "[keyfile]"
            echo "unmanaged-devices=interface-name:$IFACE"
        } > "$NM_CONF_FILE" 2>>"$LOGFILE"
        STATUS=$?
        if [ "$STATUS" -ne 0 ]; then
            echo "$(date): ERROR - Failed to write $NM_CONF_FILE" >> "$LOGFILE"
        else
            chmod 644 "$NM_CONF_FILE" 2>>"$LOGFILE"
        fi
    else
        echo "$(date): WARNING - $NM_CONF_DIR not found; persistent unmanaged state not configured." >> "$LOGFILE"
    fi
fi

# Assign IP address with 2 retries if not already assigned
while true; do
    ATTEMPT=$((ATTEMPT + 1))
    if [ "$ATTEMPT" -gt "$MAX_ATTEMPTS" ]; then
        echo "$(date): ERROR - Failed to assign IP $IPADDR to $IFACE after max attempts" >> "$LOGFILE"
        exit 1
    fi
    # Bring interface down before changing IP address
    echo "$(date): Bringing interface $IFACE down before IP update" >> "$LOGFILE"
    "$IP_BINARY" link set dev "$IFACE" down >> "$LOGFILE" 2>&1
    STATUS=$?
    if [ "$STATUS" -ne 0 ];then
        echo "$(date): ERROR - Interface $IFACE not going down. Retrying..." >> "$LOGFILE"
        continue
    fi
    sleep 0.5

    echo "$(date): Attempt $ATTEMPT - Assigning IP $IPADDR to $IFACE" >> "$LOGFILE"
    "$IP_BINARY" addr replace "$IPADDR" dev "$IFACE" >> "$LOGFILE" 2>&1
    STATUS=$?
    if [ "$STATUS" -ne 0 ];then
        echo "$(date): ERROR - Assigning IP failed to Interface $IFACE. Retrying..." >> "$LOGFILE"
        continue
    fi
    sleep 1

    # Bring the interface up
    echo "$(date): Bringing interface $IFACE up" >> "$LOGFILE"
    "$IP_BINARY" link set dev "$IFACE" up >> "$LOGFILE" 2>&1
    STATUS=$?
    if [ "$STATUS" -ne 0 ];then
        echo "$(date): ERROR - Interface $IFACE not coming up. Retrying..." >> "$LOGFILE"
        continue
    fi
    # Give time for the interface to settle
    sleep 1

    # Check if IP is assigned after current attempt
    if "$IP_BINARY" addr show dev "$IFACE" | grep -q "${IPADDR%/*}"; then
        echo "$(date): Successfully assigned IP $IPADDR to $IFACE" >> "$LOGFILE"
        break
    fi
done

# Check if route already exists
if "$IP_BINARY" route | grep -q "default via $GATEWAY"; then
    echo "$(date): Default route via $GATEWAY already exists" >> "$LOGFILE"
else
    # Add default route
    echo "$(date): Adding default route via $GATEWAY" >> "$LOGFILE"
    "$IP_BINARY" route add default via "$GATEWAY" dev "$IFACE" >> "$LOGFILE" 2>&1
fi

# Show final interface state
"$IP_BINARY" addr show "$IFACE" >> "$LOGFILE"
STATUS=$?
if [ "$STATUS" -ne 0 ];then
     echo "$(date): ERROR - Failed at querying state of the Interface $IFACE. Exiting." >> "$LOGFILE"
     exit 1
fi

# Create file for DHCP lease
if [ ! -f "$DHCPDL" ]; then
	touch "$DHCPDL"
fi
chmod 644 "$DHCPDL" >> "$LOGFILE"

# Validate DHCP configuration file
if [ ! -f "$DHCPDC" ]; then
    echo "$(date): ERROR - DHCP configuration file $DHCPDC does not exist." >> "$LOGFILE"
    exit 3
fi
if [ ! -s "$DHCPDC" ]; then
    echo "$(date): ERROR - DHCP configuration file $DHCPDC is empty." >> "$LOGFILE"
    exit 3
fi
# Test DHCP configuration file syntax
if ! dhcpd -t -cf "$DHCPDC" >> "$LOGFILE" 2>&1; then
    echo "$(date): ERROR - DHCP configuration file $DHCPDC failed syntax check." >> "$LOGFILE"
    exit 3
fi

sleep "$DHCP_SERVER_START_DELAY"
dhcpd -cf "$DHCPDC" "$IFACE" >> "$LOGFILE"
STATUS=$?
if [ "$STATUS" -ne 0 ];then
	echo "$(date): Failed to start the DHCP Server with error: $STATUS" >> "$LOGFILE"
	exit 2
fi

echo "$(date): Script completed successfully." >> "$LOGFILE"
echo "-------------------------" >> "$LOGFILE"
