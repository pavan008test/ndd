#!/bin/bash

# Script Flashes Application firmware to AON in bootloader mode.
# If running application version mismatches with the latest one, script puts Aon in bootloader mode.

AON_APP_VERSION_FILE="/home/ubuntu/aon_app_version_check.txt"
TEMP_FILE="/home/ubuntu/aon_flashcheck.txt"
FLASH_PATH="/image_upgrade/aon/"
FLASH_SCRIPT="$FLASH_PATH/aon_image_upgrade.sh"

INPUT_1_STR="AON is in App mode"
success=0
aon_retry_max_count=2
mode=""

usage()
{
        echo ""
        echo ""
        echo " Usage:"
        echo "      $0  /PATH/TO/bag3_aon.bin"
        echo "      Ex:"
        echo "      $0 /image_upgrade/aon/bag3_aon.bin"
        echo ""
}

get_data_from_cli_mgr()
{
cli_mgr <<EOF
aon $1 $2
exit
EOF
}


put_aon_in_boot_mode() {
    get_data_from_cli_mgr set_aon_boot_mode 1
}

get_aon_boot_mode() {
	get_data_from_cli_mgr get_aon_boot_mode >> $TEMP_FILE 2>&1
	grep -q "$INPUT_1_STR" "$TEMP_FILE"
	status=$?

	if [ $status -eq $success ]
	then
		mode="Aon_App_Mode"
	else
		mode="Aon_Boot_Mode"
	fi

	rm $TEMP_FILE
}

flash_latest_app() {
    cd $FLASH_PATH
    $FLASH_SCRIPT $AON_BIN -t
}

verify_present_app_version() {
	running_app_ver=$(ls $AON_BIN_FILE | head -1 | grep -oP '(?<=app_ver).*(?=.efm8)')
	get_data_from_cli_mgr get_app_version >> $AON_APP_VERSION_FILE 2>&1
	latest_app_ver=$(cat $AON_APP_VERSION_FILE | grep -oP '(?<=AON Application version:0x).*(?=)')
	# Remove the file, as it's not required anymore
	rm $AON_APP_VERSION_FILE
	if [ "$latest_app_ver" = "$running_app_ver" ]
	then
		echo "Continue booting.."
		echo "AON flash success.." > /dev/shm/aon_log.txt
		echo "AON flash version : $latest_app_ver" >> /dev/shm/aon_log.txt
	else
		# Putting device in boot_mode
		echo "Put in boot mode"
		put_aon_in_boot_mode
	fi
}

verify_input_arguments() {

        AON_LINK_FILE=$1

        if [ -z $AON_LINK_FILE ];then
                echo "Error: Aon app link file is empty"
                echo "Error: Aon app link file is empty" > /dev/shm/aon_log.txt
                usage
                exit $fail
        fi
	
	type=$(ls -l $AON_LINK_FILE | cut -c 1)
        if [ ! "$type" = "l" ];then
                echo "Error: $AON_LINK_FILE is not a link file"
                echo "Error: $AON_LINK_FILE is not a link file" > /dev/shm/aon_log.txt
		usage
                exit $fail
        fi

        if [ ! -e $AON_LINK_FILE ];then
                echo "Error: $AON_LINK_FILE link file is broken, please provide proper link file"
                echo "Error: $AON_LINK_FILE link file is broken, please provide proper link file" > /dev/shm/aon_log.txt
                ls -l "$AON_LINK_FILE"
                usage
                exit $fail
        fi
}

verify_input_arguments $1
AON_BIN_FILE=$(realpath $AON_LINK_FILE)
AON_BIN=$(basename $AON_BIN_FILE)


# 1. Check Device Boot mode
get_aon_boot_mode
case $mode in
    "Aon_App_Mode")
	echo "Aon is in App mode"
	verify_present_app_version
	;;
    "Aon_Boot_Mode")
	echo "Aon is in boot mode"
	# Flash the latest app
	aon_retry_count=`cat /etc/init.d/aon_retry_count.txt`
	if [[ "$aon_retry_count" -ge "$aon_retry_max_count" ]]
	then
                echo "AON Flash Fail..." > /dev/shm/aon_log.txt
		echo "AON is in bootloader mode..." >> /dev/shm/aon_log.txt
	else
                aon_retry_count=$[ $aon_retry_count + 1 ]
                echo $aon_retry_count > /etc/init.d/aon_retry_count.txt
		sleep 3
		sync
		sync
		sync
		flash_latest_app
	fi
	;;
esac
