1.Updating the ntdi_bag2_script with the timedatectl set-ntp false(disable ntp) and copying through the rar_task copy_ntdi_bag2_startup
md5sum : d309d8101ab2e7c88546e906036d2da0  ntdi_bag2_startup

Note:  In some cases if timesync is enabled RTC time jump is happening so disabling the ntp
Taken the ntdi_bag2_script from device with ota (3.6.3.rc.4) and updated the disable ntp in the script
Jira-id: https://netradyne.atlassian.net/browse/BGR3-816
