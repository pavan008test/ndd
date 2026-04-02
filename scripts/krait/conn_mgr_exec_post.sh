#!/bin/sh

echo "Resetting sierra modem"
gpio-app -n 26 -s 1 > /dev/null
sleep 5
gpio-app -n 26 -s 0 > /dev/null


