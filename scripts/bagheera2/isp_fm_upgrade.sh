#To return the Success or Failure status of ISP firmware upgrade
#exit 200 ---> ISP Firmware to be upgraded does not exist 
#exit 201 ---> Dependency 1 /bin/gpio test does not exist
#exit 202 ---> Dependency 2 /lib/modules/misc/gpio_icdc.ko does not exist
#exit 203 ---> Dependency 3 /usr/sbin/flash_eraseall does not exist
#exit 204 ---> Dependency 4 /usr/sbin/flashcp does not exist
#exit 205 ---> ISP Reset failed
#exit 206 ---> ISP Power down failed
#exit 207 ---> SPI Level translator Enable failed
#exit 208 ---> Flash erase failed
#exit 209 ---> Flash write failed
#exit 210 ---> SPI Level translator Disable failed
#exit 211 ---> ISP Power up failed
#exit 212 ---> ISP Reset failed after Flash write 
#exit 213 ---> ISP out of Reset failed  
#exit 0   ---> ISP FIRMWARE UPGRADE SUCCESS
 
echo "ISP firmware to be upgraded is $1"

DEP1=/bin/gpio_test
DEP2=/lib/modules/misc/gpio_icdc.ko
DEP3=/usr/sbin/flash_eraseall
DEP4=/usr/sbin/flashcp

echo "ISP UPGRADE Started"

if ! [ -f "$1" ]
then
	echo "ISP Firmware to be upgraded $1 does not exist" >&2
	echo "Exiting"
	exit 200
fi

if ! [ -f "$DEP1" ]
then
	echo "Dependency 1- $DEP1 does not exist" >&2
	echo "Exiting"
	exit 201

fi

if ! [ -f "$DEP2" ] 
then
	echo "Dependency 2- $DEP2 does not exist" >&2
	echo "Exiting"
	exit 202

fi

if ! [ -f "$DEP3" ] 
then
	echo "Dependency 3- $DEP3 does not exist" >&2
	echo "Exiting"
	exit 203

fi
if ! [ -f "$DEP4" ] 
then
	echo "Dependency 4- $DEP4 does not exist" >&2
	echo "Exiting"
	exit 204

fi

gpio_test -n 148 -s 0
if [ $? -ne 0 ] 
then
	echo "ISP Reset failed"
	echo "Exiting"
	exit 205
fi
sleep 1

gpio_test -n 151 -s 0
if [ $? -ne 0 ]
then
	echo "ISP Power down failed"
	echo "Exiting"
	exit 206
fi

sleep 1

gpio_test -n 174 -s 1
if [ $? -ne 0 ]
then
	echo "SPI Level translator enable failed"
	echo "Exiting"
	exit 207
fi

sleep 1

(flash_eraseall /dev/mtd0 ) & pid=$!
    (sleep $TIMEOUT && kill -HUP $pid ) 2>/dev/null & watcher=$!
	if wait $pid 2>/dev/null;then
		pkill -HUP -P $watcher
		wait $watcher
	else
		echo "Flash erase failed"
		echo "Exiting"
		exit 208

	fi
(flashcp -v $1 /dev/mtd0 ) & pid=$!
    (sleep $TIMEOUT && kill -HUP $pid ) 2>/dev/null & watcher=$!
	if wait $pid 2>/dev/null;then
		pkill -HUP -P $watcher
		wait $watcher
	else
		echo "Flash write failed"
		echo "Exiting"
		exit 209

	fi
gpio_test -n 174 -s 0
if [ $? -ne 0 ]
then
	echo "SPI Level translator disable failed"
	echo "Exiting with failure in resetting ISP mode"
	exit 210
fi
sleep 1
gpio_test -n 151 -s 1
if [ $? -ne 0 ] 
then
	echo "ISP Power up failed"
	echo "Exiting"
	exit 211 
fi
sleep 1

gpio_test -n 148 -s 0
if [ $? -ne 0 ] 
then
	echo "ISP Reset failed"
	echo "Exiting"
	exit 212
fi
sleep 1

gpio_test -n 148 -s 1
if [ $? -ne 0 ] 
then
	echo "ISP out of Reset failed"
	echo "Exiting"
	exit 213
fi
echo "ISP UPGRADE completed"
exit 0
