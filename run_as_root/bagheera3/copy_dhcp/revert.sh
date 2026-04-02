#!/usr/bin/env bash 

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "============== Starting revert_copy_dhcp ===================="

BACKUP_DIR="/home/ubuntu/backup/dhcp/"

copy_if_needed() {
    local src_file="$1"
    local dest_path="$2"
    local perm="$3"
    local owner="${4:-root:root}"

    if [[ ! -f "$BACKUP_DIR/$src_file" ]]; then
        log "No backup found for $src_file in $BACKUP_DIR, skipping $src_file"
    else
        local md5_src
        local md5_dest
        md5_src=$(md5sum "$BACKUP_DIR/$src_file" | awk '{print $1}')
        md5_dest=$(md5sum "$dest_path/$src_file" | awk '{print $1}')
        if [[ "$md5_src" != "$md5_dest" ]]; then
            log "The files are different: $src_file ($md5_src) is not the same in destination ($md5_dest), copying $src_file"
            cp "$BACKUP_DIR/$src_file" "$dest_path/$src_file"
            md5sum_after_copying=$(md5sum "$dest_path/$src_file" | awk '{print $1}')
            if [[ "$md5_src" != "$md5sum_after_copying" ]]; then
                log "Error: After copying, the MD5 checksum of $src_file in destination ($md5sum_after_copying) does not match the source ($md5_src)."
                return 1
            else
                log "Successfully restored $src_file to $dest_path"
            fi
        else
            log "$src_file already up to date, skipping copy."
        fi
    fi
}

copy_if_needed "dhcpd.conf" "/etc/dhcp/" "644"
status_dhcpd_conf=$?
copy_if_needed "dhcp_setup.sh" "/bin/vendor/" "755"
status_dhcp_setup=$?
copy_if_needed "start-dhcpd@.service" "/etc/systemd/system/" "644"
status_dhcpd_service=$?

if [[ $status_dhcpd_service -eq 0 && $status_dhcpd_conf -eq 0 && $status_dhcp_setup -eq 0 ]]; then
    status=0
    log " Completed copy_dhcp successfully "
else
    log "Status of dhcpd.conf copy operation: $status_dhcpd_conf"
    log "Status of dhcp_setup.sh copy operation: $status_dhcp_setup"
    log "Status of start-dhcpd@.service copy operation: $status_dhcpd_service"
    status=1
fi

check_status $(basename $(pwd)) $status

log "============== Starting revert_copy_dhcp ===================="

