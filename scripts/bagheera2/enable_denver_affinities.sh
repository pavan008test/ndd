#!/bin/bash

declare -A etc_services=(
#Camera Services
    ["bagheera"]="2,3"
    ["cam_rec"]="0,3"
    ["nvargus-daemon"]="0,3"
#Analytics Services
    ["analyticsService"]="4,5"
    ["outwardAnalyticsClient"]="4,5"
    ["inwardAnalyticsClient"]="4,5"
    ["unifiedAnalyticsClient"]="4,5"
    ["canAnalyticsClient"]="4,5"
    ["deviceHealthClient"]="1,2"
    ["scheduler_manager"]="1,2"
#ND-Services
    ["circular_buffer"]="2"
    ["ext_cam"]="1,2"
    ["HealthStatsManager"]="2,5"
    ["awsiot"]="1,2,5"
    ["uploader"]="1,2"
    ["diagnostic"]="2"
    ["svc"]="2,5" #giving backup cores for disk cleanup(svc) and connectivity (wifi_mgr,conn_mgr,awsiot,cron) services so that they are not blocked due to any bottleneck in a specific core
    ["conn_mgr"]="1,2,5"
    ["wifi_mgr"]="1,2,5"
    ["nd_bt"]="2,5"
    ["obd"]="2,5"
    ["apm"]="2"
    ["power_monitor"]="2"
    ["nd_sam"]="2"
    ["time_sync"]="2"
    ["audioPlayback"]="2"
    ["gps"]="1,2"
    ["service_mon"]="2"
    ["speed"]="1"
    ["nd_dta"]="2"
    ["installer_app"]="3,5"
#Oneshot Services
    ["nd_app_reboot"]="1,2"
    ["nd_reboot"]="1,2"
    ["nd_shutdown"]="1,2"
    ["nd_suspendresume"]="1,2"
#Cron (keep_alive_manager, wrapper_otacheck, wrapper_cleanupstate)
    ["cron"]="1,2,5"
#Systemd Services and ntdi_bag2
    ["ntdi_bag2"]="2,3"
    ["systemd-journald"]="2,3"
    ["NetworkManager"]="1,2"
    ["udisks2"]="1,2"
    ["rsyslog"]="1,2"
    ["systemd-logind"]="1,2"
    ["accounts-daemon"]="2,3"
    ["avahi-daemon"]="1,2"
    ["systemd-resolved"]="1,2"
    ["haveged"]="1,2"
    ["kerneloops"]="1,2"
    ["bluetooth"]="2,3"
    ["dbus"]="1,2"
    ["systemd-udevd"]="2,3"
    ["polkit"]="1,2"
    ["NetworkManager-dispatcher"]="1,2"
    ["nvmemwarning"]="1,2"
    ["systemd-rfkill"]="1,2"
    ["ssh"]="1,2"
    ["wpa_supplicant"]="1,2"
)

# Check if Denver cores (cpu1 and cpu2) are online, if not enable them
if [[ $(cat /sys/devices/system/cpu/cpu1/online) -ne 1 || $(cat /sys/devices/system/cpu/cpu2/online) -ne 1 ]]; then
    rm -f /etc/nvpmodel/nvpmodel_t186_transient.conf
    cp -rf /home/ubuntu/.nddevice/latest/service/diagnostic/nvpmodel_t186_denver.conf /etc/nvpmodel/nvpmodel_t186_transient.conf
    mv /etc/nvpmodel/nvpmodel_t186_transient.conf /etc/nvpmodel/nvpmodel_t186.conf
    expected_md5sum=35ea251527dee72f0b5443597202ae91
    actual_md5sum=$(md5sum /etc/nvpmodel/nvpmodel_t186.conf | awk '{print $1}')
    if [[ "$actual_md5sum" != "$expected_md5sum" ]]; then
        rm -f /etc/nvpmodel/nvpmodel_t186_transient.conf
        cp -rf /home/ubuntu/.nddevice/latest/service/diagnostic/nvpmodel_t186_backup.conf /etc/nvpmodel/nvpmodel_t186_transient.conf
        mv /etc/nvpmodel/nvpmodel_t186_transient.conf /etc/nvpmodel/nvpmodel_t186.conf
        exit 1
    fi
    nvpmodel -m 3
    sleep 2
fi

if [[ $(cat /sys/devices/system/cpu/cpu1/online) -ne 1 || $(cat /sys/devices/system/cpu/cpu2/online) -ne 1 ]]; then
    echo "Failed to enable Denver cores"
    exit 1
else
    echo "Denver cores enabled successfully"
fi

# Loop through each service and set the CPU affinity
for service in "${!etc_services[@]}"; do
    cpu_affinity="${etc_services[$service]}"
    echo "Setting CPU affinity for $service to $cpu_affinity"

    # Create the override directory if it doesn't exist
    mkdir -p "/etc/systemd/system/${service}.service.d"

    # Create or modify the override configuration file
    echo -e "[Service]\nCPUAffinity=${cpu_affinity}" | tee "/etc/systemd/system/${service}.service.d/override.conf" > /dev/null
done
nvpmodel -q --verbose
echo "Affinity changes made. Reboot to apply"
