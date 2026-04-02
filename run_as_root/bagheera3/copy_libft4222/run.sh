#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "============ Start of copying libsys.so ==========="

md5sum_libft4=$(md5sum libft4222.so.1.4.4.232 | awk '{print $1}')
md5sum_libft4_system=$(md5sum /usr/lib/libft4222.so.1.4.4.232 | awk '{print $1}')
md5sum_expander_init=$(md5sum expander_init | awk '{print $1}')
BACKUP_DIR="/home/ubuntu/backup/run_as_root/expander_init/"


log "start of Copying the libft4222.so.1.4.4.232"
if [[ -f /usr/lib/libft4222.so.1.4.4.232 && $md5sum_libft4 == $md5sum_libft4_system ]]; then
    log "Already Updated libft4222.so.1.4.4.232 is there, So not copying"
    status1=0
else
    log "Copying the latest libft4222.so.1.4.4.232"
    copy_file -f libft4222.so.1.4.4.232 -d /usr/lib/ -m $md5sum_libft4 -p 755 -o root:root
    status1=$?
    
fi

log "Linking the libft4222.so"
if [[ $(readlink /usr/lib/libft4222.so) == "libft4222.so.1.4.4.232" ]]; then
log "Linking is already present to libft4222.so.1.4.4.232. So skipping"
status2=0
else
log "Linking the libft4222.so to the libft4222.so.1.4.4.232"
status2=$(execute_command ln -sf libft4222.so.1.4.4.232 /usr/lib/libft4222.so)$?
fi
log "Done with linking the libft4222.so to libft4222.so.1.4.4.232"

log "start of Copying the expander_init "
if [[ -f /bin/vendor/expander_init && $md5sum_expander_init == $(md5sum /bin/vendor/expander_init | awk '{print $1}') ]]; then
    log "Already Updated expander_init is there, So not copying"
    status3=0
else
    log "Copying the latest expander_init"
    copy_file -f expander_init -d /bin/vendor/ -b $BACKUP_DIR -m $md5sum_expander_init -p 755 -o root:root
    status3=$?
fi

if [[ $status1 -eq 0 && $status2 -eq 0 && $status3 -eq 0 ]]; then
    log "All files copied and linked successfully"
    status=0
else
    log "Summary:"
    log "  libft4222.so.1.4.4.232 copy status: $status1"
    log "  libft4222.so linking status: $status2"
    log "  expander_init copy status: $status3"
    status=1
fi

check_status $(basename $(pwd)) $status

log "============End of copying the libft4222.so.1.4.4.232=========="
