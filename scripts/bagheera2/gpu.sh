#!/bin/bash

while true

echo 1 > /sys/kernel/debug/clock/override.gbus/state

do
        
        echo 998400000 > /sys/kernel/debug/clock/override.gbus/rate
       
       	sleep 1

        echo 537600000 > /sys/kernel/debug/clock/override.gbus/rate
        
        sleep 1
    
        echo 921600000 > /sys/kernel/debug/clock/override.gbus/rate

        sleep 1

        echo 768000000 > /sys/kernel/debug/clock/override.gbus/rate

        sleep 1        

        echo 614400000 > /sys/kernel/debug/clock/override.gbus/rate
        
        sleep 1

        echo 537600000 > /sys/kernel/debug/clock/override.gbus/rate
        
        sleep 1

        echo 460800000 > /sys/kernel/debug/clock/override.gbus/rate
        
        sleep 1

        echo 691200000 > /sys/kernel/debug/clock/override.gbus/rate
        
        sleep 1

        echo 384000000 > /sys/kernel/debug/clock/override.gbus/rate
        
        sleep 1

        echo 153600000 > /sys/kernel/debug/clock/override.gbus/rate
        
        sleep 1

        echo 844800000 > /sys/kernel/debug/clock/override.gbus/rate

        sleep 1
        
        
done

