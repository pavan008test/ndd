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


