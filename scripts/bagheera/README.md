# scripts folder

 * addbootstrap.sh - is used for creating one first time installer(install.sh) for Bagheera
 
 * bootstrap.sh - user for creating install.sh

 * install.sh.in - installer script which need to be added with bootstrap_nddevice.tar.gz to get install.sh

 * install.sh - installer script to do first time installation on Bagheera device

 * bagheera_rel_packager.sh - intial release script`
 
 * release_2_0.sh - release tag info 

 * Release_Readme - release Readme


 * wrapper_service.sh - wrapper for starting the service 
 * bagheera.sh - Service Bagheera intiator script
 * bagheera.service - Service file for bagheera


 * awsiot.sh - Service awsiot intiator script
 * awsiot.service - Service file for awsiot

 * lte_fm_upgrade.sh : LTE firware upgrade script to flash new binaries 

##Creating bootstrap_nddevice.tar.gz from shield repository

* Let assume you have to create a bootstrap_nddevice.tar.gz on the Device_1.6.1 tag of shield (https://github.com/netradyne/shield.git)
```
$git clone git@github.com:netradyne/shield.git 
cd shield
$git checkout Device_1.6.1
#copy the bootstrap from the bagheera/script to shiled/script folder
$./bootstrap.sh -o ~/OUT_BOOT #folder where to generate the binay
```

##Creating First Time Installer for Bagheera Device

* Let assume you have the bootstrap_nddevice.tar.gz from  /tmp/edge/conf or you have created it new from above  step

```sh
$cd tmp/edge/scripts
$bash addbootstrap.sh --binary bootstrap_nddevice.tar.gz
```
above command will generate the install.sh 


## Running  First Time Installer

```sh
$bash install.sh --deviceId NDPRAVEEN --ssidPassword Hello1234@
```

For help 
```sh
$bash install -help
```

lte_fm_upgrade.sh : LTE firware upgrade script to flash new binaries 
```
lte_fm_upgrade.sh  <folder containing .spk file > 
```
