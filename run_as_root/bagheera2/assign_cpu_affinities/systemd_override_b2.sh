#!/bin/bash

declare -A etc_services=(
# Camera, analytics and clients
    ["bagheera"]="0,3,4,5"
    ["cam_rec"]="0,3,4,5"
    ["nvargus-daemon"]="0,3,4,5"
    ["analyticsService"]="0,3,4,5"
    ["outwardAnalyticsClient"]="0,3,4,5"
    ["inwardAnalyticsClient"]="0,3,4,5"
    ["unifiedAnalyticsClient"]="0,3,4,5"
    ["canAnalyticsClient"]="0,3,4,5"
    ["deviceHealthClient"]="4,5"
    ["installer_app"]="0,3,4,5"
# ND-services
    ["speed"]="4"
    ["circular_buffer"]="5"
    ["HealthStatsManager"]="5"
    ["awsiot"]="5"
    ["conn_mgr"]="5"
    ["wifi_mgr"]="5"
    ["nd_bt"]="5"
    ["obd"]="5"
    ["apm"]="5"
    ["power_monitor"]="5"
    ["service_mon"]="5"
    ["nd_sam"]="5"
    ["time_sync"]="5"
    ["audioPlayback"]="5"
    ["nd_dta"]="5"
    ["uploader"]="4,5"
    ["diagnostic"]="4,5"
    ["svc"]="4,5"
    ["scheduler_manager"]="4,5"
    ["ext_cam"]="4,5"
    ["gps"]="4,5"
# Oneshot ND-services
    ["nd_shutdown"]="4,5"
    ["nd_suspendresume"]="4,5"
# Cronjobs (keep_alive_manager, wrapper_otacheck, wrapper_cleanupstate)
    ["cron"]="4,5"
# Systemd-services + ntdi_bag2
    ["ntdi_bag2"]="4,5"
    ["systemd-journald"]="4,5"
    ["NetworkManager"]="4,5"
    ["udisks2"]="4,5"
    ["rsyslog"]="4,5"
    ["systemd-logind"]="4,5"
    ["accounts-daemon"]="4,5"
    ["avahi-daemon"]="4,5"
    ["systemd-resolved"]="4,5"
    ["haveged"]="4,5"
    ["kerneloops"]="4,5"
    ["bluetooth"]="4,5"
    ["dbus"]="4,5"
    ["systemd-udevd"]="4,5"
    ["polkit"]="4,5"
    ["NetworkManager-dispatcher"]="4,5"
    ["nvmemwarning"]="4,5"
    ["systemd-rfkill"]="4,5"
    ["ssh"]="4,5"
    ["wpa_supplicant"]="4,5"
)

# Loop through each service and set the CPU affinity
for service in "${!etc_services[@]}"; do
    cpu_affinity="${etc_services[$service]}"
    echo "Setting CPU affinity for $service to $cpu_affinity"

    # Create the override directory if it doesn't exist
    mkdir -p "/etc/systemd/system/${service}.service.d"

    # Create or modify the override configuration file
    echo -e "[Service]\nCPUAffinity=${cpu_affinity}" | tee "/etc/systemd/system/${service}.service.d/override.conf" > /dev/null
done
