#!/bin/bash
sleep 60
systemd-analyze plot > /data/nd_files/log/obd/bootchart_$(date +%s).svg.log
sleep 1
systemd-analyze critical-chain wifi_mgr.service > /data/nd_files/log/obd/critical_wmgr_$(date +%s).log
