#!/usr/bin/env bash
#########################################################################
#
# Script to run sudo commands 
#       1. command
#       2 file

#########################################################################


# Prints help message
function print_help {
  cat << EOH >&2
  Description:
    $0 script [OPTION]

  OPTIONS:
    -c | --command      <command to run>        Command to perform some action
    -cf | --file         <file path to run set of commands>
EOH
}
#default value
commands="none"
file="none"
#scan the arguments 
cflag="none"
args_count=$#

if [ $args_count  -lt 2 ];then
   print_help
   exit 0
fi


 
while true
do
    case "$1" in
    -h | --help)
        print_help
        exit 0
        ;;
     -c | --command )
        commands="$2"
        shift 2
        ;;
     -cf | --file )
        file="$2"
        shift 2
        ;;
     -cc | --continue )
        cflag="$2"
        shift 2
        ;;

     *)
        break;;

   esac
done

if [ "$commands" == "none" -a $file == "none" ]; then
    
    echo "Invalid arguments....."
    print_help

fi

if [ "$cflag" == "none" ];then
    cflag=1
fi

if [ "$commands" != "none" ]; then

   eval $commands
   result=$?
   exit $result

fi

if [ $file != "none" -a  -f $file ]; then
   count=0
   while read line; do
      commands=$line
      count=$((count+1));
      eval $commands
      exitCode=$?
      if [ "$exitCode"  != "0" -a  "$cflag"  == "1" ];then
      exit $exitCode      
      fi
      
  done < $file
fi
   

