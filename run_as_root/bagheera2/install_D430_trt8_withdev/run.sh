#!/usr/bin/env bash

source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

logs=/home/ubuntu/.nddevice/log/bhcopy.log

#calling copy function from lib
log "============Installing D430_TRT8_with_dev and removing trt7 libs========="
log "============= Installing D430_TRT8_with_dev debian packages =================="
log "Extracting tar file"
tar -xzf D430_TRT8_with_dev.tar.gz
status1=$?
if [[ $status1 == 0 ]] ; then log "Extracted successfully" ; else log "Extraction  not successful. Exiting..."; exit 1 ; fi;

with_devs_folder=/home/ubuntu/.nddevice/ota_temp/run_as_root/install_D430_trt8_withdev/with_devs
cd $with_devs_folder
log "Installing list of with dev packages for TRT_8.2.1"
sudo dpkg -i *.deb 2>&1 | tee -a "$logs"
log "Installed all the deb files"
sleep 3

log "Verifying the libcublas installation"
   if [[ -n $(sudo dpkg -l | grep  libcublas) ]]; then
   log "libcublas  package was installed successfully"
   status2=0
   else
   log "libcublas was not installed. Please check!!!!!!!!! "
   status2=1
   fi
log "Successfully installed and verified the libcublas deb package"


log "Verifying the libnvinfer installation"
    if [[ -n $(sudo dpkg -l | grep libnvinfer) ]]; then
    log "libnvinfer  package was installed successfully"
    status3=0
    else 
    log "libnvinfer was not installed. Please check!!!!!!!!! "
    status3=1
    fi
log "Successfully installed and verified the libnvinfer deb package"

log "Verfying the libnvonnx installation"
     if [[ -n $(sudo dpkg -l | grep libnvonnx) ]]; then
     log "libnvonnx  package was installed successfully"
     status4=0
     else
     log "libnvonnx was not installed. Please check!!!!!!!!! "
     status4=1
     fi
log "Successfully installed and verified the libnvonnx deb package"


log "Verifying the libnvparser installation"
     if [[ -n $(sudo dpkg -l | grep libnvparser) ]]; then
     log "libnvparser package was installed successfully"
     status5=0
     else
     log "libnvparser was not installed. Please check!!!!!!!!! "
     status5=1
     fi
log "Successfully installed and verified the libnvparser deb package"

log "Verifying the nvidia-l4t-cuda  installation"
    if [[ -n $(sudo dpkg -l | grep nvidia-l4t-cuda) ]]; then
    log "nvidia-l4t-cuda package was installed successfully"
    status6=0
    else
    log "nvidia-l4t-cuda was not installed. Please check!!!!!!!!! "
    status6=1
    fi
log "Successfully installed and verified the nvidia-l4t-cuda deb package"

log "Verifying the tensorrt installation"
     if [[ -n $(sudo dpkg -l | grep tensorrt) ]]; then
     log "tensorrt package was installed successfully"
     status7=0
     else
     log "tensorrt was not installed. Please check!!!!!!!!! "
     status7=1
     fi
log "Successfully installed and verified the  tensorrt deb package"

log "Verfifying the python3-libnvinfer installing"
if [[ -n $(sudo dpkg -l | grep python3-libnvinfer) ]]; then
     log "tensorrt package was installed successfully"
     status8=0
     else
     log "tensorrt was not installed. Please check!!!!!!!!! "
     status8=1
     fi

if [[ $status2 == 0 && $status3 == 0 && $status4 == 0 && $status5 == 0 && $status6 == 0 && $status7 == 0 && $status8 == 0 ]]; then
status_trt8=0
else
log "Installation of trt8 is failed,so exiting"
check_status $(basename $(pwd)) 1
exit 1
fi
log "============= End of installing the D430_TRT8_with_dev debian packages ===================="


log "===Removing the trt7 libs which is not required while using trt8=========="

log "removing libnvinfer_plugin.so.7*"
rm -f /usr/lib/aarch64-linux-gnu/libnvinfer_plugin.so.7
rm -f /usr/lib/aarch64-linux-gnu/libnvinfer_plugin.so.7.1.3
log "removing libnvinfer.so.7*"
rm -f /usr/lib/aarch64-linux-gnu/libnvinfer.so.7
rm -f /usr/lib/aarch64-linux-gnu/libnvinfer.so.7.1.3
log "removing libnvonnxparser.so.7*"
rm -f /usr/lib/aarch64-linux-gnu/libnvonnxparser.so.7
rm -f /usr/lib/aarch64-linux-gnu/libnvonnxparser.so.7.1.3
log "removing libnvparsers.so.7*"
rm -f /usr/lib/aarch64-linux-gnu/libnvparsers.so.7
rm -f /usr/lib/aarch64-linux-gnu/libnvparsers.so.7.1.3
log "removing libnvcaffe_parser.so.7*"
rm -f /usr/lib/aarch64-linux-gnu/libnvcaffe_parser.so.7
rm -f /usr/lib/aarch64-linux-gnu/libnvcaffe_parser.so.7.1.3

status_trt7_rm=$?
if [[ $status_trt8 == 0 && $status_trt7_rm == 0 ]] ; then
check_status $(basename $(pwd)) 0
else
check_status $(basename $(pwd)) 1
fi

log "===Removing of trt7 libs is done==="
log "============End of Installing D430_TRT8_with_dev and removing trt7 libs========="

