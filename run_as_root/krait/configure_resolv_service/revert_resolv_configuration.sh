#! /usr/bin/bash 

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "********* Starting of reverting resolv service configuration *******"
#Update the dhcpcd                    
#remove the line "nohook resolv.conf"          
log "Remove nohook resovl.conf from dhcpcd.conf"
sed -i '/resolv/d' /usr/etc/dhcpcd.conf

#Remove the lines added from /etc/bind/named.conf.options
log "Remove config line for control recursive DNS"
sed -i -e /recursion/d -e /additional-from-auth/d -e /additional-from-cache/d /etc/bind/named.conf.options

#Disable the Service
log "Stop systemd-resolved service"
systemctl stop systemd-resolved
log "Disble systemd-resolved service"
systemctl disable systemd-resolved

#Delete the User
log "Removing systemd-resolve user"
userdel systemd-resolve 

#Updated the binaries and service and misc files
log "Removing the all unit supporting files for systemd-resolved service"
rm -f /lib/systemd/system/org.freedesktop.resolve1.busname
rm -f  /lib/systemd/system/systemd-resolved.service
rm -f /usr/share/dbus-1/system.d/org.freedesktop.resolve1.conf
rm -f /etc/systemd/resolved.conf
rm -f /lib/systemd/systemd-resolved
rm -f /usr/bin/systemd-resolve
rm -f /etc/systemd/network/eth0.network
#rm -f /etc/systemd/network/wlan0.network
rm -f /usr/libexec/dhcpcd-hooks/60-ipv6

#now dhcpcd will update the resolv.conf
log "Do empty resolv.conf file"
rm /etc/resolv.conf
touch /etc/resolv.conf 

# Update the systemd.conf
log "Adding entry for resolve folder and file in systemd.conf"
echo "d /run/systemd/resolve 0755 root root -" >> /usr/lib/tmpfiles.d/systemd.conf
echo "f /run/systemd/resolve/resolv.conf 0644 root root" >> /usr/lib/tmpfiles.d/systemd.conf

log "Restarting the systemd-networkd service"
systemctl restart systemd-networkd

log "********* End of reverting resolv service configuration *******"

