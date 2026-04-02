#!/bin/bash

LOG_FILE="/var/log/user.log"  # Change as needed
RESET_CMD="/home/ubuntu/.nddevice/latest/service/nd_bt/nd_bt_cleanup.sh"

TIMEOUT_PATTERN="ibs_wake_retrans_timeout: Writing HCI_IBS_WAKE_IND"
IMMEDIATE_PATTERNS=("ibs_bt_device_wakeup:SoC not responding,stop sending wake byte" "ibs_bt_device_wakeup: Failed to wake SOC" "copy_bt_data_to_channel:Failed to assert SoC" "ibs_recv_ibs_cmd: WAKE ACK from SOC, Unexpected TX state")

MSG_REPORTER_BIN="/home/ubuntu/.nddevice/latest/msg_reporter"  # Update path as needed
CRITICAL_MSG_TYPE="critical_info"
HEALTH_MSG_TYPE="health_info"
ERR_CODE=10013
ERR_MSG="Bluetooth reset due to SOC failure"
SERVICE_NAME="nd_bt_watchdog"

THRESHOLD=10
timeout_count=0
RESET_WINDOW=5    # seconds (monotonic time)
RETRY_INTERVAL=1    # seconds to wait before retrying tail

# Function to get monotonic uptime
get_uptime() {
    awk '{print int($1)}' /proc/uptime
}

last_timeout_time=$(get_uptime)

# Infinite outer loop to restart tail if it fails
while true; do
    echo "[bt_watchdog] Starting tail on $LOG_FILE..."

    while read -r line; do
        now=$(get_uptime)

        if (( now - last_timeout_time > RESET_WINDOW )); then
            timeout_count=0
        fi

        if [[ "$line" == *"$TIMEOUT_PATTERN"* ]]; then
            ((timeout_count++))
            last_timeout_time=$now
            echo "[bt_watchdog] Timeout #$timeout_count"
            PYTHONPATH=/home/ubuntu/.nddevice/latest/ $MSG_REPORTER_BIN "$HEALTH_MSG_TYPE" "$SERVICE_NAME" '{"session" : "health_info:bt_module:soc_error", "ts": "'$(($(date +%s%3N)))'", "count": '$timeout_count'}'

            if [[ "$timeout_count" -ge "$THRESHOLD" ]]; then
                echo "[bt_watchdog] Timeout threshold hit. Resetting Bluetooth..."
                $RESET_CMD
                PYTHONPATH=/home/ubuntu/.nddevice/latest/ $MSG_REPORTER_BIN "$CRITICAL_MSG_TYPE" "$SERVICE_NAME" "$ERR_CODE" "$ERR_MSG"
                timeout_count=0
                sleep 3
            fi
        fi

        for pattern in "${IMMEDIATE_PATTERNS[@]}"; do
            if [[ "$line" == *"$pattern"* ]]; then
                echo "[bt_watchdog] Immediate reset triggered by: '$pattern'"
                $RESET_CMD
                PYTHONPATH=/home/ubuntu/.nddevice/latest/ $MSG_REPORTER_BIN "$CRITICAL_MSG_TYPE" "$SERVICE_NAME" "$ERR_CODE" "$ERR_MSG"
                timeout_count=0
                sleep 3
                break
            fi
        done
    done < <(tail -n0 -F "$LOG_FILE" 2>/dev/null)

    echo "[bt_watchdog] tail exited. Retrying in $RETRY_INTERVAL seconds..."
    sleep "$RETRY_INTERVAL"
done
