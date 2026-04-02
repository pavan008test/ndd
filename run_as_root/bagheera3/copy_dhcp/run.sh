#!/usr/bin/env bash 

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "============== Starting copy_dhcp ===================="

BACKUP_DIR="/home/ubuntu/backup/run_as_root/dhcp/"

copy_if_needed() {
    local src_file="$1"
    local dest_path="$2"
    local perm="$3"
    local owner="${4:-root:root}"

    if [[ ! -f "$dest_path/$src_file" ]]; then
        log "Source file $src_file not found at $dest_path, proceeding with copying."
        local md5_src=$(md5sum "$src_file" | awk '{print $1}')
        copy_file -f "$src_file" -d "$dest_path" -p "$perm" -o "$owner" -m "$md5_src"
        copy_status=$?
        return $copy_status
    else
        local md5_src
        local md5_dest
        md5_src=$(md5sum "$src_file" | awk '{print $1}')
        md5_dest=$(md5sum "$dest_path/$src_file" | awk '{print $1}')
        if [[ "$md5_src" != "$md5_dest" ]]; then
            log "MD5 checksums differ - Expected: $md5_src, Present: $md5_dest. Copying $src_file to $dest_path"
            copy_file -f "$src_file" -d "$dest_path" -p "$perm" -o "$owner" -b "$BACKUP_DIR" -m "$md5_src"
            copy_status=$?
            log "status of copying $src_file to $dest_path: $copy_status"
            return $copy_status
        else
            log "$src_file already up to date, skipping copy."
            return 0
        fi
    fi
}

copy_if_needed "dhcpd.conf" "/etc/dhcp/" "644"
status_dhcpd_conf=$?
copy_if_needed "dhcp_setup.sh" "/bin/vendor/" "755"
status_dhcp_setup=$?
copy_if_needed "start-dhcpd@.service" "/etc/systemd/system/" "644"
status_dhcpd_service=$?

if [[ $status_dhcpd_service -eq 0 ]] && [[ $status_dhcpd_conf -eq 0 ]] && [[ $status_dhcp_setup -eq 0 ]]; then
    status=0
    log " Completed copy_dhcp successfully "
else
    log "Status of dhcpd.conf copy operation: $status_dhcpd_conf"
    log "Status of dhcp_setup.sh copy operation: $status_dhcp_setup"
    log "Status of start-dhcpd@.service copy operation: $status_dhcpd_service"
    status=1
fi

check_status $(basename $(pwd)) $status

log "============== End of copy_dhcp ===================="
