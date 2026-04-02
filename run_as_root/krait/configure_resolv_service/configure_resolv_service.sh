#! /usr/bin/bash 

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

set -e

log "********* Starting resolv service configuration *******"
## Create a System User for systemd-resolved
log "Creating  a systemd-resolve nologin user for DNS resolver"
if [[ $(grep -iq "systemd-resolve" /etc/passwd)$? == 1 ]]; then
useradd -c "systemd DNS Resolver,,," --system -d / -M --shell /bin/nologin systemd-resolve
log "systemd-resolve user is created"
else
log "systemd-resolve user is already created"
fi

## Updated the binaries and service and misc files
log "copying org.freedesktop.resolve1.busname to /lib/systemd/system/"
cp -f org.freedesktop.resolve1.busname /lib/systemd/system/

log "copying systemd-resolved.service to /lib/systemd/system/"
cp -f systemd-resolved.service /lib/systemd/system/

log "copying org.freedesktop.resolve1.conf to /usr/share/dbus-1/system.d/"
cp -f org.freedesktop.resolve1.conf /usr/share/dbus-1/system.d/

log "copying resolved.conf to /etc/systemd/"
cp -f resolved.conf /etc/systemd/

log "copying systemd-resolved to /lib/systemd/"
cp -f systemd-resolved /lib/systemd/

log "copying  systemd-resolve to /usr/bin/"
cp -f systemd-resolve /usr/bin/ 

log "copying eth0.network to /etc/systemd/network/"
cp -f eth0.network /etc/systemd/network/

#log "copying wlan0.network to /etc/systemd/network/"
#cp -f wlan0.network /etc/systemd/network/

log "copying 60-ipv6 hook file to dhcp config folder"
cp -f 60-ipv6 /usr/libexec/dhcpcd-hooks/

## KRT2-94: Copying the config file to Control Recursive DNS Resolution retries
log "copying named.conf.options file to /etc/bind/"
cp -f named.conf.options /etc/bind/

## Disable /etc/resolv.conf update from dhcpcd
log "Adding nohook resolv.conf to dhcpcd.conf"
if [[ $(grep -iq "resolv" /usr/etc/dhcpcd.conf)$? == 1 ]]; then
echo "nohook resolv.conf" >> /usr/etc/dhcpcd.conf
log "nohook resolv is added to dhcpcd.conf"
else
log "Already added"
fi

## Removing old resolv.conf as is was created by dhcp
log "Removing resolv.conf and creating a soft link to file present in /run/"
rm -f /etc/resolv.conf
ln -fs /run/systemd/resolve/resolv.conf /etc/resolv.conf

log "Removing resolv-conf.systemd and creating a soft link to file present in /run/"
rm -f /etc/resolv-conf.systemd
ln -fs /run/systemd/resolve/resolv.conf /etc/resolv-conf.systemd

## Removing the /run/systemd/resolve folder as it was already created with root permission 
## and when systemd-resolved service is up it could not create that folder with its own permissions
log "Removing resolve folder /run/"       
rm -rf /run/systemd/resolve/

log "Checking and adding resolve to nsswitch.conf"
if [[ $(grep -iq "resolve" /etc/nsswitch.conf)$? == 1 ]] ; then
sed -i 's/hosts:          files dns/hosts:          files dns resolve/g' /etc/nsswitch.conf
else
log "Already added"
fi

# Update the systemd.conf to remove resolve file and folder entry
log "Checking and removing the resolve folder and file creation entry from systemd.conf"
if [[ $(grep -iq "resolve" /usr/lib/tmpfiles.d/systemd.conf)$? == 0 ]] ; then
sed -i '/resolve/d' /usr/lib/tmpfiles.d/systemd.conf
else
log "Already removed"
fi

# Else below error is seen--- /lib/systemd/systemd-resolved 
#Could not create runtime directory: File exists

log "Restaring the systemd-networkd service"
systemctl restart systemd-networkd

log "Enable the new service - systemd-resolved"
systemctl enable systemd-resolved

log "Start service - systemd-resolved"
systemctl restart systemd-resolved

log "********* End of resolv service configuration *******"

