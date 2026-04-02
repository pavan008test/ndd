#Load the gpio module
echo "Starting VVDN CLIK script" > /dev/kmsg
CONN_MGR_PATH=/home/ubuntu/.nddevice/latest/service/conn_mgr

python3 $CONN_MGR_PATH/lumia_reset.py
if [ $? -ne 0 ]
then
echo "ERROR!!! lumia reset failed !!!!! " > /dev/kmsg
else
echo "lumia reset SUCCESS!!!!! " > /dev/kmsg
fi

GPIOAPP=/usr/bin/gpio-app
PSTORE=/mnt/pstore

sync
ln -s /run/systemd/journal/syslog /dev/log
sync

echo "LED STOP" > /dev/kmsg

echo "none" > /sys/class/leds/clik_left_red/trigger

echo 1 > /sys/class/leds/clik_left_green/brightness

echo 33 > /sys/class/leds/fan_control/pwm_us

echo 33 > /sys/class/leds/ir_led/pwm_us

mount -o remount,rw /

if ! [ -d "$PSTORE" ]
then
    mkdir /mnt/pstore
fi
mount -t pstore pstore /mnt/pstore

fan_control &

lpm_wakeup &

modprobe GobiSerial

modprobe GobiNet

mkdir -p /data/fru_check/tmp

fru_check
if [ $? -ne 0 ]; then
    echo "fru_failed" > /dev/kmsg
fi

echo "Configuring ISP-SPI GPIO PINS ..." > /dev/kmsg

if ! [ -f "$GPIOAPP" ]
then
    echo "Dependency 2 $GPIOAPP does not exist" > /dev/kmsg
fi

gpio-app -n 109 -s 0
if [ $? -ne 0  ]
then
    echo "Enable pin  of spi flash - NOT set as 0" > /dev/kmsg
fi

if [ -f /usr/bin/sierra_powerdown -a -f /usr/bin/sierra_powerup.sh ]
then
    sierra_powerup.sh
    sleep 2
    sierra_powerdown &
fi

echo "Executed VVDN CLIK script" > /dev/kmsg
