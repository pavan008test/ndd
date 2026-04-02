#!/bin/bash

#ERROR CASE
#To return the success or failure status of Sierra firmware upgrade result
#exit 100 ---> Sierra firmware to be upgraded does not exist
#exit 101 ---> Binary 1 /usr/bin/fwdldarm does not exist
#exit 102 ---> Binary 2 /bin/slqssdk does not exist
#exit 103 ---> Binary 3 /bin/gpio_test does not exist
#exit 104 ---> Sierra Firmware to be upgraded does not consist of .spk file
#exit 105 ---> Exiting with failure in setting gpio
#exit 106 ---> Exiting with failure in resetting gpio
#exit 107 ---> Sierra Modem is not connected before download

#exit 110 ---> Sierra Modem is not connected after download
#exit 111 ---> Max attempts for flashing failed
#exit 112 ---> Sierra Modem reset failed after Download
#exit 113 ---> Sierra Modem reset failed before Download
#exit 114 ---> Firmware is corrupted
#exit 115 ---> Update AT command failed
#exit 116 ---> Modem status after flashing is incorrect
#exit 117 ---> AT cmd port unavailable
#exit 0   ---> Sierra Firmware Upgrade Success

TIMEOUT=240 #Seconds timeout for flashing attempt
RETRY=5 #No. of flash retry attempts

###########################################
# Stop SDK process  #
###########################################

function stop_sdk_process()
{
     echo "Stopping SDK process .."

     PID=`ps -e | grep slqssdk | grep -v grep | awk '{print $1}'`

     if ([ !  -z  $PID  ] && [ -e /proc/$PID ]); then
         sudo kill -9 $PID
         sleep 5
     else
         echo "slqssdk is already killed ..."
     fi

}


###########################################
# Wait Modem reset enumeration #
###########################################

function wait_modem_reset_enumeration()
{
     echo "Waiting for modem to shutdown ..."

     count4=0
     while ([ -e "/dev/qcqmi0" ] && [ $count4 -lt 25 ])
     do
         sleep 2
         let count4=count4+1
     done

     if [[ "$count4" -ge 25 ]]
     then
        echo "Modem shutdown failed"
        exit 111
     else
        echo "Modem shutdown completed"
     fi

     stop_sdk_process

     count5=0
     echo "Waiting for modem enumeration ...."

     while ([ ! -e "/dev/qcqmi0" ] && [ $count5 -lt 25 ])
     do
        sleep 2
        let count5=count5+1
     done

     if [[ "$count5" -ge 25 ]]
     then
        echo "Modem enumeration failed"
        exit 111
     else
        echo "Modem enumeration completed"
     fi

     a=0
     while [ $a -lt 10 ]
     do
       sleep 1
       lsusb | grep "Sierra" > /dev/null
       if [ $? -eq 0 ]
       then
          echo "Sierra Modem is connected"
          break
       fi
          let a=a+1
     done

     if [[ "$a" -ge 10 ]]
     then
        echo "Sierra Modem is not connected"
        exit 110
     fi
}


###########################################
# Check if LTE flash binary are avaibale #
###########################################

function check_if_flash_binary()
{

     echo "Sierra firmware to be upgraded is $folder"

     FILE1=/usr/bin/fwdldarm
     FILE2=/bin/slqssdk
     FILE3=/bin/gpio_test

     echo "===Checking flash binary==="

     if [ -d "$folder" ]
     then
        echo "Sierra firmware to be upgraded $folder exist"
     else
        echo "Sierra firmware to be upgraded $folder does not exist" >&2
        exit 100
     fi


     if [ -f "$FILE1" ]
     then
        echo "File 1 $FILE1 exist"
     else
        echo "File 1 $FILE1 does not exist" >&2
        exit 101
     fi

     if [ -f "$FILE2" ]
     then
        echo "File 2 $FILE2 exist"
     else
        echo "File 2 $FILE2 does not exist" >&2
        exit 102
     fi

     if [ -f "$FILE3" ]
     then
        echo "File 3 $FILE3 exist"
     else
        echo "File 3 $FILE3 does not exist" >&2
        exit 103
     fi

     count=$(ls "$folder"/*.spk | wc -l)
     if [ "$count" -ne 0 ]
     then
        echo "Sierra firmware to be upgraded $folder consist of .spk file"
     else
        echo "Sierra firmware to be upgraded $folder does not consist of .spk file"
        exit 104
     fi
}

##############################
# Check if LTE update is ok to proper #
##############################

function check_if_safe_toupdate()
{
     echo "=== Power Cycling Modem ==="

     gpio_test -n 1019 -s 1 > /dev/null & pid=$!
     sleep 1
     ([ -e /proc/$pid ] && sleep 2 && sudo kill -9 $pid) > /dev/null
     if wait $pid 2>/dev/null;then
          echo "gpio_test -n 1019 -s 1 success"
     else
          echo "Failed in setting LTE_GPS WP7504";
          echo "Exiting with failure in setting gpio"
          exit 105
     fi

     sleep 8

     gpio_test -n 1019 -s 0 > /dev/null & pid=$!
     sleep 1
     ([ -e /proc/$pid ] && sleep 2 && sudo kill -9 $pid) > /dev/null
     if wait $pid 2>/dev/null;then
          echo "gpio_test -n 1019 -s 0 success"
     else
          echo "Failed in Resetting the LTE_GPS WP7504";
          echo "Exiting with Failure in resetting the gpio"
          exit 106
     fi

     echo "Waiting for modem reset before flash "

     wait_modem_reset_enumeration
}


############################
# Stop Connection Service  #
#############################
function connection_service_stop()
{
    echo "=== conn service stop ==="
    echo "Stop connection service"
    sudo nd_service.sh -c stop -n conn_mgr
    sleep 2
}


############################
# Start Connection Service  #
#############################
function connection_service_start()
{
    echo "=== conn service start ==="
    echo "Start connection service"
    sudo nd_service.sh -c start -n conn_mgr
    sleep 2
}


############################
# Stop LTE Manager  #
#############################
function connection_manager_stop()
{
     echo "=== connection manager stop ==="

     echo "Killing LTE service"

     PID=`ps -e | grep connectionmgr | grep -v grep | awk '{print $1}'`

     if ([ !  -z  $PID  ] && [ -e /proc/$PID ]); then
          sudo kill -3 $PID
          sleep 5
     else
          echo "connectionmgr is already killed ..."
     fi

     if ([ !  -z  $PID  ] && [ -e /proc/$PID ]); then
          echo "Forcibly killing connection mgr .."
          sudo kill -9 $PID
          sleep 1
     else
          echo "connectionmgr exited gracefully .."
     fi

     connection_service_stop

     stop_sdk_process

     echo "Turning WIFI Off ..."

     nmcli radio wifi off > /dev/null & pid=$!

     ([ -e /proc/$pid ] && sleep 6 && sudo kill -9 $pid)
}



############################
# Start LTE Manager  #
#############################
function connection_manager_start()
{
   echo "=== connection manager start ==="

   argument=$1

   echo "Path argument: " "$argument"

   [ -f "/home/ubuntu/.nddevice/session_lte" ] && sudo /home/ubuntu/bin/disable_wifi_turn_on_lte.sh "$argument"

   [ ! -f "/home/ubuntu/.nddevice/session_lte" ] && sudo /home/ubuntu/bin/disable_lte_turn_wifi_on.sh "$argument"

   [ ! -f "/home/ubuntu/.nddevice/session_lte" ] && connection_service_start
}

############################
# Stop Bagheera Service  #
#############################
function bageera_service_stop()
{
    echo "=== bagheera service stop ==="
    echo "Stopping service"
    sudo nd_service.sh -c stop -n bagheera
    sleep 5
}

############################
# LTE Firmware Update  #
#############################
function lte_fm_update()
{
     echo "=== lte firware update ==="

     loop=0
     while [ $loop -lt $RETRY ]
     do
        echo "Retry Count - $loop"

        echo "LTE_GPS: Download Starts"

        (fwdldarm -s "$FILE2" -d 9x15 -p "$folder" ) & pid=$!

        (sleep $TIMEOUT && [ -e /proc/$pid ] && sudo kill -9 $pid) 2> /dev/null & watcher=$!

        if wait $pid 2>/dev/null;then
             echo "LTE_GPS Download is done and reset modem"
             break;
        else
             echo "FAILED in Downloading LTE_GPS $pid"
        fi

        sudo kill -9 $watcher > /dev/null
        wait $watcher

        echo "LTE_GPS: Resetting WP7504"

        check_if_safe_toupdate

        echo "Incrementing..."

        let loop=loop+1
     done

     if [ "$loop" -ge "$RETRY" ]
     then
         echo "Firmware flash max attempts failed ..."
         exit 113
     fi

     echo "Waiting for modem reset after flash"

     wait_modem_reset_enumeration
}



###############################
# Check AT cmd port #
###############################
function check_at_cmd_port()
{
      echo "===== check at cmd port ====="

      count8=0
      while ([ ! -e "/dev/ttyUSB2" ] && [ $count8 -lt 25 ])
      do
          sleep 2
          let count8=count8+1
      done

      if [[ "$count8" -ge 25 ]]
      then
          echo "AT cmd port unavailable"
          exit 117
      else
          echo "AT cmd port detection success"
      fi
}



###############################
# Verify LTE Firmware #
###############################
function verify_lte_firmware()
{
      echo "=== verify lte firmware ==="

      echo "=== Get firmware version ===="
      count6=0

      while [ $count6 -lt $RETRY ]
      do
          echo "Retry Count - $count6"

          check_at_cmd_port

          lte_gps_test 'AT!PRIID?'  > log.txt & pid=$!
          sleep 1

          ([ -e /proc/$pid ] && sleep 5 && sudo kill -9 $pid ) > /dev/null
          grep "OK" log.txt > /dev/null

          ret=$?
          if [ $ret -eq 0 ]
             then
                 echo "Get AT cmd firmware version success $pid"
                 tmp=(`ls "$folder"`)
                 version="${tmp::-4}"

                 echo  "Firmware version is :" "$version"
                 grep "$version" log.txt > /dev/null
                 ret=$?

                 if [ $ret -eq 0 ]
                 then
                     echo "Firmware version matched"
                     break;
                 else
                     echo "Firmware version mismatch : $version"
                 fi

           else
                 echo "FAILED in getting firmware version"
           fi

           let count6=count6+1

      done

      rm -f log.txt

      if [ "$count6" -ge "$RETRY" ]
      then
           echo "Modem firmware seems corrupted"
           exit 114
      fi

      echo "=== Get Modem status ===="
      count7=0

      while [ $count7 -lt $RETRY ]
      do
           echo "Retry Count - $count7"

           check_at_cmd_port

           lte_gps_test 'AT!GSTATUS?' > log.txt & pid=$!
           sleep 1

           ([ -e /proc/$pid ] && sleep 5 && sudo kill -9 $pid ) > /dev/null
           grep "OK" log.txt > /dev/null
           ret=$?

           if [ $ret -eq 0 ]
           then
               echo "Get GSTATUS success $pid"
               grep "ONLINE" log.txt > /dev/null

               ret=$?
               if [ $ret -eq 0 ]
               then
                    echo "Modem status is ONLINE"
                    break;
               else
                    echo "Modem is not ONLINE "
                    grep "RESETTING" log.txt > /dev/null
                    ret=$?

                    if [ $ret -eq 0 ]
                    then
                       echo "Modem is resetting , wait ...."
                       wait_modem_reset_enumeration
                    else
                       echo "Modem status is incorrect"
                    fi

               fi

           else
               echo "FAILED in getting Modem STATUS"
           fi

           let count7=count7+1
      done

      rm -f log.txt

      if [ "$count7" -ge "$RETRY" ]
      then
           echo "Modem firmware seems corrupted"
           exit 116
      fi

      echo "==== Verify LTE firmware success ===="
}

############################
# Start Bagheera Service  #
#############################
function bageera_service_start()
{
     echo "=== bagheera service start ==="
     echo "Starting service"
     sudo nd_service.sh -c start -n bagheera
     sleep 5
}

############################
# Update single AT command  #
#############################

function update_single_command()
{


   command=$1

   echo "Updating command : " "$command"

   count9=0
   while [ $count9 -lt $RETRY ]
   do
       echo "Retry Count - $count9"

       check_at_cmd_port

       lte_gps_test "$command" > log.txt & pid=$! > /dev/null
       sleep 1

       ([ -e /proc/$pid ] && sleep 5 && sudo kill -9 $pid ) > /dev/null
       grep "OK" log.txt > /dev/null

       ret=$?

       if [ "$ret" -eq 0 ]
       then
           echo "Command : $command Success"
           break;
       else
           echo "failed in updating command - $count9"
       fi

       let count9=count9+1
   done

   rm -f log.txt

   if [ "$count9" -ge "$RETRY" ]
   then
       echo "AT command update failed"
       exit 115
   fi
}


###############################
# Update AT Command so that it should resume again #
###############################

function update_at_commands()
{
     echo "=== update AT Commands ==="
     var1="AT!GPSAUTOSTART=1,1,250,250,1"
     update_single_command "$var1"
     var2="AT!GPSNMEA=1"
     update_single_command "$var2"
     var3="AT!GPSNMEACONFIG=1,1"
     update_single_command "$var3"
     var4="AT!ENTERCND="\""A710"\"
     update_single_command "$var4"
     var5="AT!GPSXTRADATAENABLE=2,3,10,1,24,24"
     update_single_command "$var5"
     var6='AT!GPSXTRAINITDNLD'
     update_single_command "$var6"
     #disable LTE B12 , B17 , B26 / WCDMA B5
     var7="AT!BAND=00,"\""All bands"\"",0000000002800000,000000000100001A,0000000000000000"
     update_single_command "$var7"
     var8='AT!BAND=0'
     update_single_command "$var8"
}


function reset_modem()
{
    echo "=== Resetting modem ==="
    var9="AT!RESET"
    update_single_command "$var9"
    wait_modem_reset_enumeration
    sleep 5
}

#main Function
     folder=$1
     path="latest"
     check_if_flash_binary
     connection_manager_stop
     bageera_service_stop
     lte_fm_update
     verify_lte_firmware
     update_at_commands
     reset_modem

     if [ "$#" -eq 2 ]; then
         echo "Current OTA package is : $2"
         path="$2"
     fi

     connection_manager_start "$path"

     echo "LTE_GPS SIERRA FIRMWARE UPGRADE IS COMPLETED"
     exit 0
