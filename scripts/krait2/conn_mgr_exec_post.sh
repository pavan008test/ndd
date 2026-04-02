#!/bin/sh

echo "Resetting sierra modem"
gpio-app -n 134 -s 0 > /dev/null

sleep 5
gpio-app -n 134 -s 1 > /dev/null


