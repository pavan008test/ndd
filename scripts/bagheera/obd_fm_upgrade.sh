#!/usr/bin/env bash
#To return the Success or Failure status of OBD firmware upgrade
#exit -2  ---> ARGUMENTS_FAILED 
#exit -3  ---> BL_CORRUPTED 
#exit -4 --->  FACTORY_IMAGE_CORRUPTED
#exit -5 --->  BINARY_CORRUPTED
#exit -6 --->  INVALID_STATE 
#exit -7 --->  FLASHING_FAILED_RW_ERROR
#exit -8 --->  FLASHING_FAILED_CRC_CHECK
#exit -9 --->  FLASHING_FAILED_INT_TIMEOUT
#exit -10 ---> UNKNOWN_MCS_ERROR
#exit -77 ---> MEMORY_ALLOC_FAILED
#exit 0   ---> OBD FIRMWARE UPGRADE SUCCESS
 
Print_Help()
{
    cat << EOH >&2

  1) Getting help or info about $0 :
  $0 --help   Print this message
  .......................................................................

  $0 --updatePath <updatepath> --stablePath  <stablePath> --partitionId <partitionId> 

EOH
}





function check_if_flash_binary()
{

    echo " OBD firmware to be upgraded is started "
    Exce="/home/ubuntu/.nddevice/ota_temp/run_as_root/obd_fw_update/fw_update_mcs"
   
    if [ -f  $Exec ]
    then
            echo " $Exec  executable  exist"
    else
            echo "$Exec  executable  not exist" >&2
            exit 101
    fi

    if [ -f "$obdUpdatePath" ]
    then
            echo " In this path $obdUpdatePath  Firmware image available"
    else
            echo "In this path $obdUpdatePath  Firmware image not  available" >&2
            exit 102
    fi

    if [ -f "$obdStablePath" ]
    then
            echo " In this path $obdStablePath  Firmware image available"
    else
            echo "In this path $obdStablePath  Firmware image not  available" >&2
            exit 102
    fi
}

############################
# Stop OBD Service  #
#############################
function obd_service_stop() {
        echo "=== obd service stop ===\n"
        echo "Stoping service"
        sleep 3
        sudo nd_service.sh -c stop -n obd

}

############################
# Start OBD Service  #
#############################
function obd_service_start() {
        echo "=== obd service start ===\n"
        echo "Starting service"

        sudo nd_service.sh -c start -n obd

}

function obd_fm_update() {
        echo "=== obd firware update ===\n"
        echo  $Exce  $partitionId $checksumValue  $obdUpdatePath  $fwVer  $retryCount
        sudo $Exce  $partitionId $checksumValue  $obdUpdatePath  $fwVer  $retryCount
        exitcode=$?
        echo "value of return $exitcode"
        if [ $exitcode -eq 0 ]
        then
                echo "OBD firmware upgrade is sucessful"
                exit 0
        fi
        if [ $exitcode -ne 0 ]
        then
                echo "OBD firmware upgrade is failed with failure $exitcode"
                flash_stable_version
        fi
}

function flash_stable_version(){

	if [ $partitionId -eq 36 ];then
         
        fwVer=$(awk -F "=" '/sapp2_version/ {print $2}' $obd_config)
        retryCount=$(awk -F "=" '/sapp2_retry/ {print $2}' $obd_config)
        checksumValue=$(awk -F "=" '/sapp2_checksum/ {print $2}' $obd_config) 
	fi

        if [ $partitionId -eq 35 ];then

        fwVer=$(awk -F "=" '/sapp1_version/ {print $2}' $obd_config)
        retryCount=$(awk -F "=" '/sapp1_retry/ {print $2}' $obd_config)
        checksumValue=$(awk -F "=" '/sapp1_checksum/ {print $2}' $obd_config)
        fi

        echo  $Exce  $partitionId $checksumValue  $obdStablePath  $fwVer  $retryCount
        sudo $Exce  $partitionId $checksumValue  $obdStablePath  $fwVer  $retryCount
        stableExitcode=$?
        if [ $stableExitcode -eq 0 ]; then
                echo "Flashing stable obd firmware upgrade sucessful"
                exit 0
        else
	        echo "Flashing stable obd firmware upgrade failed with exit code $stableExitcode"
                exit 105
        fi
}




partitionId="none"
checksumValue="none"
absoultePath="none"
fwVer="none"
retryCount="none"
obdStablePath="none"
obdUpdatePath="none"
#scan the arguments
while true
do
    case "$1" in
     -partitionId | --partitionId )
	partitionId="$2"
	shift 2
	;;
     -checksum | --checksum )
	checksumValue="$2"
	shift 2
	;;
     -stablePath | --stablePath )
	obdStablePath="$2"
	shift 2
	;;
     -updatePath | --updatePath )
        obdUpdatePath="$2"
        shift 2
        ;;
     -fwVer | --fwVer)
	fwVer="$2"
	shift 2
	;; 

     -retryCount | --retryCount)
	retryCount="$2"
	shift 2
	;; 

      *)
	break;;
	
   esac
done

obd_config="/home/ubuntu/.nddevice/obd_info.txt"
#check if no partitionId then flag the error and error out
if [ "$partitionId" == "none" ]; then
	echo "No partitionId provided ...."
        Print_Help
        exit;
fi

#check if no path then flag the error and error out
if [ "$obdUpdatePath" == "none" ]; then
        echo "No path value provide so exiting...."
        Print_Help
        exit;
fi 

#check if no path then flag the error and error out
if [ "$obdStablePath" == "none" ]; then
        echo "No path value provide so exiting ...."
        Print_Help
        exit;

fi

if [ "$partitionId" == "36" ]; then
       
        fwVer=$(awk -F "=" '/uapp2_version/ {print $2}' $obd_config)
        retryCount=$(awk -F "=" '/uapp2_retry/ {print $2}' $obd_config)
        checksumValue=$(awk -F "=" '/uapp2_checksum/ {print $2}' $obd_config)
fi

if [ "$partitionId" == "35" ]; then

        fwVer=$(awk -F "=" '/uapp1_version/ {print $2}' $obd_config)
        retryCount=$(awk -F "=" '/uapp1_retry/ {print $2}' $obd_config)
        checksumValue=$(awk -F "=" '/uapp1_checksum/ {print $2}' $obd_config)
fi




#main Function
        check_if_flash_binary
        obd_service_stop
        obd_fm_update
        obd_service_start
