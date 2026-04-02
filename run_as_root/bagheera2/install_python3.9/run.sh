#!/usr/bin/env bash

set -e
source  /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh

log "===============Start of python3.9 installation ==================================="
log "Extracting tar file"
tar -xzf python3.9.14_setup1_B2.tar.gz
status1=$?
if [[ $status1 == 0 ]] ; then log "Extracted successfully" ; else log "Extraction  not successful. Exiting..."; exit 1 ; fi;
chmod +x *

log "Installing python3.9-minimal and libpython3.9-stdlib"
sudo dpkg -i libpython3.9-minimal_3.9.14-1+bionic1_arm64.deb
sudo dpkg -i python3.9-minimal_3.9.14-1+bionic1_arm64.deb
sudo dpkg -i libpython3.9-stdlib_3.9.14-1+bionic1_arm64.deb
sudo dpkg -i python3.9_3.9.14-1+bionic1_arm64.deb

#distutls need to be installed for python3.9.14 and that is dependent on lib2to3
sudo dpkg -i libpython3.9_3.9.14-1+bionic1_arm64.deb
sudo dpkg -i python3.9-lib2to3_3.9.14-1+bionic1_all.deb
sudo dpkg -i python3.9-distutils_3.9.14-1+bionic1_all.deb
python3.9 -V | grep 'Python 3.9.14'
status=$?
if [ $status == 0 ]
then
log "python3.9 installed sucessfully"
else
exit 1
log "python3.9 installation failed"
fi

sudo update-alternatives --install /usr/bin/python3 python3 /usr/bin/python3.9 1
log "updated alternatives for python3.9"

log "Start of Installing python packages"
sudo pip3 install pip-22.3-py3-none-any.whl

pip3 -V | grep 'pip.*(python 3.9)'
status=$?
if [ $status == 0 ]
then
log "pip3 for python3.9 installed sucessfully"
else
exit 1
log "pip3 installtion failed "
fi

log "Installing python packages"
sudo pip3 -v install pytz-2022.4-py2.py3-none-any.whl
sudo pip3 -v install astral-1.10.1-py2.py3-none-any.whl 
sudo pip3 -v install python_dateutil-2.8.2-py2.py3-none-any.whl
sudo pip3 -v install urllib3-1.26.12-py2.py3-none-any.whl

sudo pip3 -v install setuptools-36.2.7-py2.py3-none-any.whl
sudo pip3 -v install idna-3.4-py3-none-any.whl
sudo pip3 -v install certifi-2022.9.24-py3-none-any.whl

log "installing pycurl"
sudo pip3 -v install --ignore-installed pycurl-7.45.1-cp39-cp39-linux_aarch64.whl

log "installing numpy"
sudo pip3 -v install charset_normalizer-2.1.1-py3-none-any.whl
sudo pip3 -v install requests-2.28.1-py3-none-any.whl
sudo pip3 -v install Pillow-9.3.0-cp39-cp39-manylinux_2_17_aarch64.manylinux2014_aarch64.whl
sudo pip3 -v install Shapely-1.8.4-py3-none-any.whl
sudo pip3 -v install pyparsing-3.0.9-py3-none-any.whl
sudo pip3 -v install numpy-1.23.5-cp39-cp39-manylinux_2_17_aarch64.manylinux2014_aarch64.whl

sudo pip3 -v install PyWavelets-1.4.1-cp39-cp39-manylinux_2_17_aarch64.manylinux2014_aarch64.whl
sudo pip3 -v install scipy-1.9.1-cp39-cp39-manylinux_2_17_aarch64.manylinux2014_aarch64.whl
sudo pip3 -v install tifffile-2022.10.10-py3-none-any.whl

log "installing psutil"
sudo pip3 -v install --ignore-installed psutil-5.3.1-cp39-cp39-linux_aarch64.whl

log "installing scikit-image"
sudo pip3 -v install networkx-2.8.7-py3-none-any.whl
sudo pip3 -v install packaging-21.3-py3-none-any.whl
sudo pip3 -v install imageio-2.22.4-py3-none-any.whl
sudo pip3 -v install scikit_image-0.19.3-cp39-cp39-manylinux_2_17_aarch64.manylinux2014_aarch64.whl
sudo pip3 -v install wheel-0.37.1-py2.py3-none-any.whl


sudo pip3 -v install pycparser-2.21-py2.py3-none-any.whl
sudo pip3 -v install cffi-1.15.1-cp39-cp39-manylinux_2_17_aarch64.manylinux2014_aarch64.whl
sudo pip3 -v install cryptography-38.0.1-cp36-abi3-manylinux_2_17_aarch64.manylinux2014_aarch64.manylinux_2_24_aarch64.whl

sudo pip3 -v install --ignore-installed sysv_ipc-0.7.0-cp39-cp39-linux_aarch64.whl

sudo pip3 -v install urllib3-1.26.12-py2.py3-none-any.whl 
sudo pip3 -v install opencv_python-4.6.0.66-cp36-abi3-manylinux_2_17_aarch64.manylinux2014_aarch64.whl
sudo pip3 -v install timezonefinder-3.0.1-py2.py3-none-any.whl
sudo pip3 -v install cppy-1.2.1-py3-none-any.whl

log "installing pyzmq"
sudo pip3 -v install --ignore-installed pyzmq-24.0.1-cp39-cp39-linux_aarch64.whl
status=$?
check_status $(basename $(pwd)) $status
