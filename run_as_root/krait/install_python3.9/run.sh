#!/usr/bin/env bash

set -e 
source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "===============Start of python3.9 installation ==================================="

python_version=$(python3 -V | cut -d " " -f2)

if [[ $python_version == "3.9.14" ]]; then 

log "Python version is already $python_version. Skipping the installation of python3.9"
exit 0
else

log "Installing a recovery service for py3.5 if device reboots at any point after this py3.9 install"
cp py_reboot_recovery.service /etc/systemd/system/
cp py3.5_reboot_recovery.sh /home/ubuntu/.nddevice/ 
systemctl enable py_reboot_recovery.service

log "Extracting python libs tar file"

tar -xvf python3.9_setup_krait.tar.gz -C /usr/
status1=$?
rm python3.9_setup_krait.tar.gz

if [[ $status1 == 0 ]] ; then echo "Extracted successfully" ; check_status $(basename $(pwd)) 0 ; else echo "Extraction  not successful. Exiting..."; check_status $(basename $(pwd)) 1 ; exit 1 ; fi;

log "copying the libpython3.9.so.1 to /usr/lib64/"

cp -f /usr/local/lib/libpython3.9.so.1.0 /usr/lib64/

sudo update-alternatives --install /usr/bin/python3 python3 /usr/local/bin/python3.9 2

python3 -V | grep 'Python 3.9.14'
status=$?
if [ $status == 0 ]
then
log "python3.9 installed sucessfully"
check_status $(basename $(pwd)) 0
else
check_status $(basename $(pwd)) 1
log "python3.9 installation failed"
exit 1
fi

log "Installing the pip using ensurepip "

python3 -m ensurepip

pip3 -V | grep 'pip.*(python 3.9)'
status=$?
if [ $status == 0 ]
then
log "pip3 for python3.9 installed sucessfully"
check_status $(basename $(pwd)) 0
else
log "pip3 installtion failed "
check_status $(basename $(pwd)) 1
exit 1
fi

log "Extracting wheel packages "

tar -xvf python3.9_krait_whl.tar.gz
rm python3.9_krait_whl.tar.gz

function install_whl {
whl_name=$1
log "installing wheel $1"
python3 -m pip install $1 >> /home/ubuntu/.nddevice/log/bhcopy.log
status=$?
if [ $status == 0 ];then
	log "$1 installed succesfully removing the wheel file"
	rm $1
	check_status $(basename $(pwd)) 0
else
	log"$1 not installed properly exiting the script"
	check_status $(basename $(pwd)) 1
fi
}


wheel_array=(six-1.16.0-py2.py3-none-any.whl configparser-3.8.1-py2.py3-none-any.whl sysv_ipc-0.7.0-cp39-cp39-linux_aarch64.whl psutil-5.3.1-cp39-cp39-linux_aarch64.whl urllib3-1.26.12-py2.py3-none-any.whl charset_normalizer-2.1.1-py3-none-any.whl idna-3.4-py3-none-any.whl certifi-2022.9.24-py3-none-any.whl requests-2.28.1-py3-none-any.whl numpy-1.23.3-cp39-cp39-manylinux_2_17_aarch64.manylinux2014_aarch64.whl scipy-1.9.1-cp39-cp39-manylinux_2_17_aarch64.manylinux2014_aarch64.whl xgboost-1.6.1-py3-none-manylinux2014_aarch64.whl protobuf-3.4.0-py2.py3-none-any.whl pyzmq-21.0.0-cp39-cp39-manylinux2014_aarch64.whl pyparsing-3.0.9-py3-none-any.whl packaging-21.3-py3-none-any.whl networkx-2.8.7-py3-none-any.whl tifffile-2022.8.12-py3-none-any.whl PyWavelets-1.4.1-cp39-cp39-manylinux_2_17_aarch64.manylinux2014_aarch64.whl Pillow-9.2.0-cp39-cp39-manylinux_2_17_aarch64.manylinux2014_aarch64.whl imageio-2.22.1-py3-none-any.whl scikit_image-0.19.3-cp39-cp39-manylinux_2_17_aarch64.manylinux2014_aarch64.whl pytz-2022.4-py2.py3-none-any.whl astral-1.10.1-py2.py3-none-any.whl timezonefinder-3.0.1-py2.py3-none-any.whl pycparser-2.21-py2.py3-none-any.whl cffi-1.15.1-cp39-cp39-manylinux_2_17_aarch64.manylinux2014_aarch64.whl cryptography-38.0.1-cp36-abi3-manylinux_2_17_aarch64.manylinux2014_aarch64.whl nose-1.3.7-py3-none-any.whl chardet-3.0.4-py2.py3-none-any.whl pyasn1-0.3.6-py2.py3-none-any.whl asn1crypto-0.23.0-py2.py3-none-any.whl joblib-1.2.0-py3-none-any.whl threadpoolctl-3.1.0-py3-none-any.whl scikit_learn-1.1.2-cp39-cp39-manylinux_2_17_aarch64.manylinux2014_aarch64.whl shapely-2.0.1-cp39-cp39-manylinux_2_17_aarch64.manylinux2014_aarch64.whl PySocks-1.6.7-py3-none-any.whl opencv_python_headless-4.7.0.72-cp37-abi3-manylinux_2_17_aarch64.manylinux2014_aarch64.whl pycurl-7.21.5-cp39-cp39-linux_aarch64.whl)

for i in ${wheel_array[@]};do
        install_whl $i
done
log "pip packages installed successfully"
check_status $(basename $(pwd)) 0

fi

log "===============End of python3.9 installation ==================================="
