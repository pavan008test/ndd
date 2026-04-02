#script for invoking the free_cache service
log_folder=/home/ubuntu/.nddevice/log/free_cache

system_uptime=$(date +"%s")
system_uptime_ms=$(date +"%s%3N")
mkdir -p $log_folder

echo "folder $log_folder created"
log_file=$log_folder/log_$(date +"%s").log
i=1
while [ 1 ] ; do
    if [ $(($i%30)) == 0 ]
    then
        new_log_file=log_$(date +"%s").log
        echo "Routing log to new file : $new_log_file " >> $log_file
        log_file=$log_folder/$new_log_file
    fi
    epoch_time=$(date +"%s")  # same epoch time will be used for writing and reading logs
    #echo "log file full path: $log_file " >> $log_file
    #echo "log file name: $log_folder/$log_file " >> $log_file
    echo -e "$epoch_time : $(expr $epoch_time - $system_uptime)  : DBG : I : 0000  : 0000  : free |grep Mem => $(free | grep "Mem:") \n" >> $log_file
    echo "$epoch_time : $(expr $epoch_time - $system_uptime)  : DBG : I : 0000  : 0000  : writing 3 in /proc/sys/vm/drop_caches at Iteration no $i" >> $log_file
    echo 3 > /proc/sys/vm/drop_caches
    echo "$epoch_time : $(expr $epoch_time - $system_uptime)  : DBG : I : 0000  : 0000  : reading file /proc/sys/vm/drop_caches: $(cat "/proc/sys/vm/drop_caches") " >> $log_file
    echo "$epoch_time : $(expr $epoch_time - $system_uptime)  : DBG : I : 0000  : 0000  : ps -ely |grep analytics => $(ps -ely | grep analytics$) " >> $log_file
    echo -e "$epoch_time : $(expr $epoch_time - $system_uptime)  : DBG : I : 0000  : 0000  : free |grep Mem => $(free | grep "Mem:") \n" >> $log_file
    j=0
    while [ $j -lt 60 ] ; do
        epoch_time=$(date +"%s%3N")  # same epoch time will be used for writing and reading logs
        echo -e "$epoch_time : $(expr $epoch_time - $system_uptime_ms)  : DBG : I : 0000  : 0000  : free |grep Mem => $(free | grep "Mem:") \n" >> $log_file
        sleep 1
        ((j++))
#        j=`expr $j + 1`
    done
    ((i++))
#    i=`expr $i + 1`
done
echo END OF SCRIPT



