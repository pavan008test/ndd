#!/bin/bash

DB_PATH=""
SOURCE_DB_PATH=""
DEST_DB_PATH=""
DB_TABLE="GENPROP"
LOG_FILE="/home/ubuntu/.nddevice/log/obd_property_copy.log"

# Function to log messages
log_message() {
    echo "$(date '+%Y-%m-%d %H:%M:%S') - $1" >> "$LOG_FILE"
}

get_device_type() {
    config_file="/home/ubuntu/config/deviceconfig.ini"
    devicetype=$(grep -i "devicetype" "$config_file" | awk -F'=' '{print $2}' | tr -d ' ')
    echo "$devicetype"

    if [ "$devicetype" == "bagheera3" ] || [ "$devicetype" == "bagheera2" ]; then 
        DB_PATH="/home/ubuntu/.nddevice"
    else
        DB_PATH="/data/nd_files/db"
    fi
    SOURCE_DB_PATH="$DB_PATH/gen_property.db"
    DEST_DB_PATH="$DB_PATH/obd_property.db"
}
set_property_DB() {
    local property="vbus_db_copied"
    local value="$1"
    local existing_value
    existing_value=$(sqlite3 "$SOURCE_DB_PATH" "SELECT DATA FROM $DB_TABLE WHERE PROPERTY='$property';")
    if [[ -z "$existing_value" ]]; then
        # If the property doesn't exist, insert it
        sqlite3 "$SOURCE_DB_PATH" "INSERT INTO $DB_TABLE (PROPERTY, DATA) VALUES ('$property', '$value');"
        log_message "Inserted new property '$property' with value '$value'."
    else
        # If it exists, update the existing value
        sqlite3 "$SOURCE_DB_PATH" "UPDATE $DB_TABLE SET DATA='$value' WHERE PROPERTY='$property';"
        log_message "Updated property '$property' to new value '$value'."
    fi
}
get_property_DB() {
    local property="vbus_db_copied"
    local value
    value=$(sqlite3 "$SOURCE_DB_PATH" "SELECT DATA FROM $DB_TABLE WHERE PROPERTY='$property';")
    if [[ -z "$value" ]]; then
        echo "not found"
        return 1
    else
        echo "$value"
        return 0
    fi
}
check_file_exists() {
    if [[ -f $1 ]]; then
        echo "available"
        return 0
    else
        echo "not-available"
        return 1
    fi
}
copy_vbus_db() {
    cp "$SOURCE_DB_PATH" "$DEST_DB_PATH"
    if [[ $? -ne 0 ]]; then
        log_message "Failed to copy obd_property db"
        return 1
    fi
    sqlite3 "$DEST_DB_PATH" "ALTER TABLE GENPROP RENAME TO obd_property;"
    if [[ $? -ne 0 ]]; then
        log_message "Failed to rename table in obd_property db"
        return 1
    fi

    log_message "obd_property db copied and renamed successfully"
    return 0
}

handle_vbus_db_copy() {
    if [[ $(check_file_exists "$DEST_DB_PATH") == "available" ]]; then
        log_message "obd_property db already copied"
        return 0
    fi
    local vbus_db_copied
    vbus_db_copied=$(get_property_DB)
    log_message "vbus_db_copied value is $vbus_db_copied"
    if [[ $vbus_db_copied == "true" ]]; then
        log_message "obd_property db already copied"
        return 0
    else
        if copy_vbus_db; then
            log_message "obd_property db copied successfully"
            if set_property_DB "true"; then
                log_message "obd_property db copied flag set successfully"
                return 0
            else
                log_message "Failed to set db copied flag"
                return 1
            fi
        else
            log_message "Failed to copy obd_property db"
            return 1
        fi
    fi
}
# Main execution
{
    get_device_type
    log_message "Starting database operations..."
    handle_vbus_db_copy
} 2>&1 | tee "$LOG_FILE"

