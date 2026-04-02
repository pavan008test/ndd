#To return the Success or Failure status of ISP firmware upgrade
#exit 200 ---> ISP Firmware to be upgraded does not exist
#exit 201 ---> Dependency 1 /bin/vendor/gpio_test does not exist
#exit 202 ---> Dependency 2 /lib/modules/misc/gpio_ntdi.ko does not exist
#exit 203 ---> Dependency 3 /usr/sbin/flash_eraseall does not exist
#exit 204 ---> Dependency 4 /usr/sbin/flashcp does not exist
#exit 205 ---> ISP Reset failed
#exit 206 ---> SPI Level translator Enable failed
#exit 207 ---> Gpio value of ISP reset - Not set to 0
#exit 208 ---> Gpio value of SPI level translator - Not set to 1
#exit 209 ---> Registers value write in SPI enable - Failed
#exit 210 ---> Flash erase failed
#exit 211 ---> Flash write failed
#exit 212 ---> SPI Level translator Disable failed
#exit 213 ---> ISP Reset failed after Flash write
#exit 214 ---> ISP out of Reset failed
#exit 215 ---> Gpio value of ISP reset - Not set to 1
#exit 216 ---> Gpio value of SPI level translator - Not set to 0
#exit 217 ---> Registers value write in SPI disable - Failed
#exit 0   ---> ISP FIRMWARE UPGRADE SUCCESS

send_error_code() {
    gpio_test -n 461 -i
    exit $1
}

echo "ISP firmware to be upgraded is $1"

DEP1=/bin/vendor/gpio_test
DEP2=/lib/modules/4.9.140-tegra/kernel/drivers/bag2_vendor/gpio/gpio_ntdi.ko
DEP3=/usr/sbin/flash_eraseall
DEP4=/usr/sbin/flashcp

echo "ISP UPGRADE Started"

if ! [ -f "$1" ]
then
	echo "ISP Firmware to be upgraded $1 does not exist" >&2
	echo "Exiting"
    send_error_code 200
fi

if ! [ -f "$DEP1" ]
then
	echo "Dependency 1- $DEP1 does not exist" >&2
	echo "Exiting"
    send_error_code 201

fi

if ! [ -f "$DEP2" ]
then
	echo "Dependency 2- $DEP2 does not exist" >&2
	echo "Exiting"
    send_error_code 202

fi

if ! [ -f "$DEP3" ]
then
	echo "Dependency 3- $DEP3 does not exist" >&2
	echo "Exiting"
    send_error_code 203

fi
if ! [ -f "$DEP4" ]
then
	echo "Dependency 4- $DEP4 does not exist" >&2
	echo "Exiting"
    send_error_code 204

fi


gpio_test -n 461 -o 0
if [ $? -ne 0 ]
then
	echo "ISP Reset failed"
	echo "Exiting"
    send_error_code 205
fi
sleep 1

gpio_test -n 259 -s 1
if [ $? -ne 0 ]
then
	echo "SPI Level translator enable failed"
	echo "Exiting"
    send_error_code 206
fi

/image_upgrade/isp/spi4_enable.sh
sleep 1

gpio_test -n 461 -g | grep "0"
if [ $? -ne 0 ]
then
	echo "gpio value of ISP reset pin - Not set as 0"
    send_error_code 207
fi

gpio_test -n 259 -g | grep "1"
if [ $? -ne 0 ]
then
        echo "gpio value of SPI level translator - Not set as 1"
        send_error_code 208
fi

echo "SPI enable Register value Check"
read_reg_0=$(md_test 0x0c302050 4 | cut -d ' ' -f2)
read_reg_1=$(md_test 0x0c302058 4 | cut -d ' ' -f2)
read_reg_2=$(md_test 0x0c302060 4 | cut -d ' ' -f2)
read_reg_3=$(md_test 0x0c302068 4 | cut -d ' ' -f2)
written_reg_0_2=00000400
written_reg_1=00000454
written_reg_3=00000408

echo "read_reg_0: $read_reg_0"
echo "read_reg_1: $read_reg_1"
echo "read_reg_2: $read_reg_2"
echo "read_reg_3: $read_reg_3"

if [[ $read_reg_0 = $written_reg_0_2 && $read_reg_1 = $written_reg_1 && $read_reg_2 = $written_reg_0_2 && $read_reg_3 = $written_reg_3 ]]
then
	echo "Registers are written successfully in SPI enable script"
else
	echo "Register write in SPI enable - Failed"
    send_error_code 209
fi

(flash_eraseall /dev/mtd0 ) & pid=$!
    (sleep $TIMEOUT && kill -HUP $pid ) 2>/dev/null & watcher=$!
	if wait $pid 2>/dev/null;then
		pkill -HUP -P $watcher
		wait $watcher
	else
		echo "Flash erase failed"
		/image_upgrade/isp/spi4_disable.sh
		echo "Exiting"
        send_error_code 210

	fi
(flashcp -v $1 /dev/mtd0 ) & pid=$!
    (sleep $TIMEOUT && kill -HUP $pid ) 2>/dev/null & watcher=$!
	if wait $pid 2>/dev/null;then
		pkill -HUP -P $watcher
		wait $watcher
	else
		echo "Flash write failed"
		/image_upgrade/isp/spi4_disable.sh
		echo "Exiting"
        send_error_code 211

	fi
gpio_test -n 259 -s 0
if [ $? -ne 0 ]
then
	echo "SPI Level translator disable failed"
	echo "Exiting with failure in resetting ISP mode"
	/image_upgrade/isp/spi4_disable.sh
    send_error_code 212
fi
sleep 1

gpio_test -n 461 -s 0
if [ $? -ne 0 ]
then
	echo "ISP Reset failed"
	/image_upgrade/isp/spi4_disable.sh
	echo "Exiting"
    send_error_code 213
fi
sleep 1

gpio_test -n 461 -s 1
if [ $? -ne 0 ]
then
	echo "ISP out of Reset failed"
	/image_upgrade/isp/spi4_disable.sh
	echo "Exiting"
    send_error_code 214
fi

gpio_test -n 461 -g | grep "1"
if [ $? -ne 0 ]
then
        echo "gpio value of ISP reset pin - Not set as 1"
        send_error_code 215
fi

gpio_test -n 259 -g | grep "0"
if [ $? -ne 0 ]
then
        echo "gpio value of SPI level translator - Not set as 0"
        send_error_code 216
fi

/image_upgrade/isp/spi4_disable.sh

echo "SPI Disable Register value Check"

read_reg_0=$(md_test 0x0c302050 4 | cut -d ' ' -f2)
read_reg_1=$(md_test 0x0c302058 4 | cut -d ' ' -f2)
read_reg_2=$(md_test 0x0c302060 4 | cut -d ' ' -f2)
read_reg_3=$(md_test 0x0c302068 4 | cut -d ' ' -f2)
written_reg_0_1_2_3=00000010

echo "read_reg_0: $read_reg_0"
echo "read_reg_1: $read_reg_1"
echo "read_reg_2: $read_reg_2"
echo "read_reg_3: $read_reg_3"

if [[ $read_reg_0 = $written_reg_0_1_2_3 && $read_reg_1 = $written_reg_0_1_2_3 && $read_reg_2 = $written_reg_0_1_2_3 && $read_reg_3 = $written_reg_0_1_2_3 ]]
then
        echo "Registers are written successfully in SPI disable script"
else
        echo "Register write in SPI disable - Failed"
        send_error_code 217
fi

echo "ISP UPGRADE completed"
send_error_code 0
