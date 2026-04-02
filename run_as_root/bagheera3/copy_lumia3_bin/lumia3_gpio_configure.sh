#!/bin/bash

# Export Usb-Uart bridge
echo 212 > /sys/class/gpio/export       #gps reset gpio
echo 213 > /sys/class/gpio/export
echo 214 > /sys/class/gpio/export
echo 215 > /sys/class/gpio/export

# Set gps reset gpio as output
gpio_test -n 212 -o 0
gpio_test -n 213 -o 1
gpio_test -n 214 -o 0
gpio_test -n 215 -o 1

