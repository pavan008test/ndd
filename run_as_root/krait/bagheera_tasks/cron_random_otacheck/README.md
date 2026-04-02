#Fix to modify timer for wrapperotacheck crontab from 10min to 1min to randomize ota. BHAGEERA-1641
#Use the below command to modify cron
#crontab -l | sed "\~wrapper_otacheck$~{s~^\*\/10~\*\/1~;}" | crontab -
#To check status of each command in the pipe, use echo "${PIPESTATUS[0]} ${PIPESTATUS[1]}"
