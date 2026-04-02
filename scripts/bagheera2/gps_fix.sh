#!/bin/bash

while [ ! -e "/dev/qcqmi0" ]
do 
   sleep 1
done

while [ 1 ]

do 
   echo "-----------------------"

   echo "GPSSTATUS:"

   lte_gps_sample_app 'at!gpsstatus?'

   echo "DEVICE TIME:"

   date

   echo "EPOCH TIME:"

   date +%s

   echo "--------------------------"

   sleep 3

done







