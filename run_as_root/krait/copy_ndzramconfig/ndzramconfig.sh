#! /bin/bash

#select lzo compression algorithm
echo lzo > /sys/block/zram0/comp_algorithm

# set disk size to 256MB
echo 256M > /sys/block/zram0/disksize

# active zram0
mkswap /dev/zram0
swapon /dev/zram0

for i in $(seq 1 ); do
    if [ ! -e /sys/block/zram$i ]; then
        cat /sys/class/zram-control/hot_add
        echo lzo > /sys/block/zram$i/comp_algorithm
        echo 256M > /sys/block/zram$i/disksize
        mkswap /dev/zram$i
        swapon /dev/zram$i
    else
        echo "zram$i already exists"
    fi
done

# swapiness parameter set to 10
#echo 10 > /proc/sys/vm/swappiness

#Trigger Memory Defragmentation as early as possible
echo 100 > /proc/sys/vm/extfrag_threshold

#ZRAM Latencies are in the order of 30 time compared to RAM 
echo 100  >  /proc/sys/vm/swappiness

#Load only a single pages instead of multiple pages when swapping
echo 0   > /proc/sys/vm/page-cluster

echo "Executed ndzramconfig.sh script" > /dev/kmsg
