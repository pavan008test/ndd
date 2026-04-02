extracting the dm_logging.zip file to the /home/ubuntu/

This tool is placed in the device at /home/ubuntu/dm_logging and can be triggered through KA commands.
After updating to special package to capture the DM logs, below KA commands need to run.
This create a log files in conn_mgr folder with name modem_swi_${epoch_time}.log which will be uploaded. This generated log file is not direclty readle format. After successful execution of KA commands,
device reboots after ~12 mins.

KA (Device Action) commands :
echo EKM2800123Netra | sudo -S cp -f /home/ubuntu/dm_logging/dmcapture_trigger.service /etc/systemd/system
echo EKM2800123Netra | sudo -S systemctl start dmcapture_trigger.service
