ND Reboot service was setup for SdCard going into 50Mhz on any reboot other than POR.

Thus, Nd Reboot service would ensure a POR on a reboot triggered from any ND services.

Now since the SdCard is made to always come up in 50Mhz, this is not necessarily required.

To Disable:
-----------
sudo /usr/bin/nd_service.sh -c clean -n nd_shutdown -p /home/ubuntu/.nddevice/latest/service/

Reverting Process: [Make sure that this service exists]
-----------------------------------------------
sudo /usr/bin/nd_service.sh -c install -n nd_shutdown -p /home/ubuntu/.nddevice/latest/service/




NOTE:
-----
Any OTA containing this should not have the nd_shutdown service shipped.

