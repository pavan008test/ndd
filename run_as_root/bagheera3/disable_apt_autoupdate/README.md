Auto updates are triggered as part of APT settings in following places

APT::Periodic::Update-Package-Lists:
------------------------------------
/etc/apt/apt.conf.d/20auto-upgrades
/etc/apt/apt.conf.d/10periodic

APT::Periodic::Unattended-Upgrade:
----------------------------------
/etc/apt/apt.conf.d/20auto-upgrades



sed -i "/APT::Periodic::Update-Package-Lists/c\APT::Periodic::Update-Package-Lists \"0\";" /etc/apt/apt.conf.d/20auto-upgrades
sed -i "/APT::Periodic::Unattended-Upgrade/c\APT::Periodic::Unattended-Upgrade \"0\";" /etc/apt/apt.conf.d/20auto-upgrades
sed -i "/APT::Periodic::Update-Package-Lists/c\APT::Periodic::Update-Package-Lists \"0\";" /etc/apt/apt.conf.d/10periodic



NOTE:
-----

/etc/apt/sources.list

Must have the following lines commented out
# deb http://ports.ubuntu.com/ubuntu-ports/ xenial-security universe
# deb-src http://ports.ubuntu.com/ubuntu-ports/ xenial-security universe
# deb http://ports.ubuntu.com/ubuntu-ports/ xenial-security multiverse
# deb-src http://ports.ubuntu.com/ubuntu-ports/ xenial-security multiverse
# deb-src http://ports.ubuntu.com/ubuntu-ports/ xenial universe
# deb-src http://ports.ubuntu.com/ubuntu-ports/ xenial-updates universe

-----------------------------------------------------------------------------------------
This task was used in B1 to disable apt-get autoupdates. Now same is reused for B3 to disable the apt updates and also snapd service disble and remove.

Now script might changes as per B3 device. BGR3-642
-----------------------------
Disable snapd service
-----------------------------
sudo systemctl disable snapd

sudo systemctl disable snapd.socket

sudo apt purge snapd

sudo apt purge appstream
----------------------------------
Disable apt-get updates
-------------------------------------
cat /etc/apt/apt.conf.d/10periodic

APT::Periodic::Update-Package-Lists "0";
APT::Periodic::Download-Upgradeable-Packages "0";
APT::Periodic::AutocleanInterval "0";
APT::Periodic::Unattended-Upgrade "0";

Remove apt-get permissions
------------------------------------
chmod 0 /usr/bin/apt-get

----------------------------------------------

Need to implement in the coming release to validate the service removal
if [[ `sudo systemctl is-active snapd` == inactive ]] ; then log "snapd service is not active" ; else log "snapd service is active !!!!" ; fi 
if [[ `sudo systemctl list-unit-files snapd.service | grep listed | cut -d " " -f1` == 0 ]] ; then log "snapd unit service file not found" ; else log "snapd unit service file found !!!!" ; fi

if [[ `sudo systemctl is-active snapd.socket` == inactive ]] ; then log "snapd service is not active" ; else log "snapd service is active !!!!" ; fi
if [[ `sudo systemctl list-unit-files snapd.socket.service | grep listed | cut -d " " -f1` == 0 ]] ; then log "snapd unit service file not found" ; else log "snapd unit service file found !!!!" ; fi

