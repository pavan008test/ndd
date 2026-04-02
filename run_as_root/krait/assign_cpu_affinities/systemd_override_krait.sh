#!/bin/bash

# Define the services and their CPU affinities
declare -A services=(
#Camera services
    ["qmmf-server"]="7"
    ["installer_app"]="7"
    ["bagheera"]="6,7"
#Analytics services
    ["analytics"]="6,7"
    ["outwardAnalyticsClient"]="6"
    ["inwardAnalyticsClient"]="6"
    ["unifiedAnalyticsClient"]="6"
    ["deviceHealthClient"]="2-4"
    ["canAnalyticsClient"]="2-4"
    ["scheduler_manager"]="2-4"
#ND-services
    ["circular_buffer"]="1,2,5"
    ["diagnostic"]="1,2,5"
    ["ext_cam"]="1,2,5"
    ["time_sync"]="4-5"
    ["obd"]="4-5"
    ["HealthStatsManager"]="3-5"
    ["free_cache"]="4-5"
    ["nd_bt"]="0,5"
    ["wifi_mgr"]="1,4,5" #wpa supplicant is managed by wifi_mgr
    ["conn_mgr"]="1,4,5" #giving atleast 3 cores for connectivity(wifi_mgr,conn_mgr,awsiot,cron) and disk cleanup(svc) services to avoid blocking due to any bottleneck in a specific core
    ["audioPlayback"]="4-5"
    ["fan_control"]="4-5"
    ["nd_sam"]="4-5"
    ["apm"]="4-5"
    ["power_monitor"]="4-5"
    ["svc"]="3-5"
    ["awsiot"]="1,2,5"
    ["uploader"]="3-5"
    ["service_mon"]="4-5"
    ["speed"]="4-5"
    ["nd_dta"]="4-5"
    ["gps"]="4-5"
# Cron (keep_alive_manager, wrapper_otacheck, wrapper_cleanupstate)
    ["crond"]="2-4"
# Systemd services
    ["systemd-journald"]="1,4"
    ["rsyslog"]="1,4"
    ["systemd-logind"]="1,4"
    ["msmirqbalance"]="1,4"
    ["logd"]="1,4"
    ["leprop"]="1,4"
    ["cdsprpcd"]="6,7"
    ["user@"]="1,4"
    ["adsprpcd"]="6,7"
    ["systemd-networkd"]="1,4"
    ["dbus"]="1,4"
    ["systemd-udevd"]="1,4"
    ["qtid"]="1,4"
    ["systemd-resolved"]="1,4"
    ["tftp_server"]="1,4"
    ["rmt_storage"]="1,4"
    ["QCMAP_ConnectionManagerd"]="1,4"
    ["start_cnss_daemon"]="1,4"
    ["dnsmasq"]="1,4"
    ["location_hal_daemon"]="1,4"
    ["qmmf-webserver"]="1,4"
    ["loc_launcher"]="1,4"
    ["adbd"]="1,4"
    ["thermal-engine"]="1,4"
    ["qti_system_daemon"]="1,4"
    ["reboot-daemon"]="1,4"
    ["qseecomd"]="1,4"
    ["servicemanager"]="1,4"
    ["misc_daemon"]="1,4"
    ["pdmapper"]="1,4"
    ["adsprpcd_rootpd"]="6,7"
    ["adsprpcd_audiopd"]="6,7"
    ["dcam"]="1,4" #For D-210
    ["clik"]="1,4" #For D-215
    ["init_sys_mss"]="1,4"

)

# Loop through each service and set the CPU affinity
for service in "${!services[@]}"; do
    cpu_affinity="${services[$service]}"
    echo "Setting CPU affinity for $service to $cpu_affinity"

    # Create the override directory if it doesn't exist
    mkdir -p "/etc/systemd/system/${service}.service.d"

    # Create or modify the override configuration file
    echo -e "[Service]\nCPUAffinity=${cpu_affinity}" | tee "/etc/systemd/system/${service}.service.d/override.conf" > /dev/null
done

