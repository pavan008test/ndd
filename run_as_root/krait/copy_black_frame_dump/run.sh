#!/usr/bin/env bash
source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

black_frame_inward_LS_md5sum_krait1=$(md5sum krait1/black_frame_inward_LS.avc  | awk '{print $1}')
black_frame_outward_LS_md5sum_krait1=$(md5sum krait1/black_frame_outward_LS.avc | awk '{print $1}')
black_frame_inward_LS_md5sum_krait2=$(md5sum krait2/black_frame_inward_LS.avc | awk '{print $1}')
black_frame_outward_LS_md5sum_krait2=$(md5sum krait2/black_frame_outward_LS.avc | awk '{print $1}')

log "=============Copying the black frame files=================="

devicetype=$(grep -i "devicetype" /home/ubuntu/config/deviceconfig.ini | awk -F'=' '{print $2}' | tr -d ' ')
echo "$devicetype"

if [[ "$devicetype" == "krait" ]]; then 
    copy_file -f krait1/black_frame_inward_LS.avc -d /home/ubuntu/.nddevice/ -m "$black_frame_inward_LS_md5sum_krait1" -o root:root -p 775
    status1=$?
    copy_file -f krait1/black_frame_outward_LS.avc -d /home/ubuntu/.nddevice/ -m "$black_frame_outward_LS_md5sum_krait1" -o root:root -p 775
    status2=$?
else
    copy_file -f krait2/black_frame_inward_LS.avc -d /home/ubuntu/.nddevice/ -m "$black_frame_inward_LS_md5sum_krait2" -o root:root -p 775
    status1=$?
    copy_file -f krait2/black_frame_outward_LS.avc -d /home/ubuntu/.nddevice/ -m "$black_frame_outward_LS_md5sum_krait2" -o root:root -p 775
    status2=$?
fi

log "===============End Copying Black Frames=================="

if [[ $status1 == 0 && $status2 == 0 ]]; then
    log "Copy black frames executed successfully"
    check_status $(basename $(pwd)) 0
else
    log "Failed to execute copy black frames. Please check!"
    check_status $(basename $(pwd)) 1
fi

log "=============End of script===================="
