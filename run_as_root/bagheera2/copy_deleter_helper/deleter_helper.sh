#!/usr/bin/env bash

#This script is mainly to remove the root onwered path with sudo permission.
#It takes a single argument and the exact path what ever expects in argument is hardcoded here for safety reason.
#It checks the given argument path is present in the array list, if yes proceed to execute to delete that path.

path_argument=$1
path_array=( "/home/iriscli/ND_INPUT/" "/home/iriscli/ND_OUTPUT/" )

for path in ${path_array[@]}
do
	if [[ $path_argument =~ $path && ! $path_argument =~ .*\/".."\/* ]]; then
		logger "deleter_helper.sh : Deleting the $path_argument"
		#Deleting to given path
		sudo rm -rf $path_argument
		if [[ $? == 0 ]]; then 
			logger "deleter_helper.sh : Deleted successfully - $path_argument"
		else
			logger "deleter_helper.sh : Deleting unsuccessful - $path_argument"
			exit 1
		fi
	fi
done
