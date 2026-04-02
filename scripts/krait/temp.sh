#!/bin/bash

while true

do
        cat /sys/class/thermal/thermal_zone0/type
        cat /sys/class/thermal/thermal_zone0/temp

        cat /sys/class/thermal/thermal_zone1/type
        cat /sys/class/thermal/thermal_zone1/temp

        cat /sys/class/thermal/thermal_zone2/type
        cat /sys/class/thermal/thermal_zone2/temp

        cat /sys/class/thermal/thermal_zone3/type
        cat /sys/class/thermal/thermal_zone3/temp

        cat /sys/class/thermal/thermal_zone4/type
        cat /sys/class/thermal/thermal_zone4/temp

        cat /sys/class/thermal/thermal_zone5/type
        cat /sys/class/thermal/thermal_zone5/temp

        cat /sys/class/thermal/thermal_zone6/type
        cat /sys/class/thermal/thermal_zone6/temp

        cat /sys/class/thermal/thermal_zone7/type
        cat /sys/class/thermal/thermal_zone7/temp
       
       	sleep 2
done

