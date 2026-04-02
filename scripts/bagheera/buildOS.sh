#!/bin/bash 

#binary system.img

#Environment variable setup
#OS_BUILD_DIR=/home/praveen/image/release_8_0/OS_EDIT
OS_BUILD_DIR=$1
#version="99.00.02.01.US"
version=$2

#echo '"version:'$version'"' > version_icdc.txt
#exit
echo "The OS build folder is " $OS_BUILD_DIR ", and system.img is  " $OS_BUILD_DIR/$1

echo "Creating a folder for OS mount point "
mkdir $OS_BUILD_DIR/OS

#mount the system.img provided
sudo mount -t ext4 -o loop,rw system.img $OS_BUILD_DIR/OS

# remove the home folder as it is already seprated out 
echo "Press a delete the home folder ..."
read KEY
cp -R  $OS_BUILD_DIR/OS/home $OS_BUILD_DIR/.
sudo rm -rf $OS_BUILD_DIR/OS/home



#Add the mount point for the home folder
#TODO don't add if there is already a entry point or if home is auto mounted
echo "Press a enter key to add a mount point for /home as /dev/mmcblk0p21. Also ensure there is no /home mount exists in $OS_BUILD_DIR/OS/etc/fstab ..."
read KEY
sudo sh -c "echo '/dev/mmcblk0p21 /home   ext4  defaults       0  0' >> $OS_BUILD_DIR/OS/etc/fstab"


#updateting the version number based on the user input
echo "Press a enter key to update the version number ..."
read KEY
sudo sh -c "echo '\"Version:$version\"' > $OS_BUILD_DIR/OS/etc/version_icdc.txt"


#zip the file and make the tar
echo "Press a enter key to make a tar of rootfs ..."
read KEY
mkdir IMG
cd  $OS_BUILD_DIR/OS 
sudo tar -cjvf $OS_BUILD_DIR/IMG/icdc_rootfs.tar.bz2 *
echo '"version:'$version'"' > $OS_BUILD_DIR/IMG/version_icdc.txt
md5sum $OS_BUILD_DIR/IMG/icdc_rootfs.tar.bz2 > $OS_BUILD_DIR/IMG/icdc_md5sum.txt
cd $OS_BUILD_DIR


#zip the file and make the tar
echo "Press a enter key to make a tar of rootfs ..."
read KEY

cd  $OS_BUILD_DIR/IMG
sudo tar -cjvf $OS_BUILD_DIR/icdc_rootfs_md5sum.tar.bz2 *
md5sum $OS_BUILD_DIR/icdc_rootfs_md5sum.tar.bz2 > $OS_BUILD_DIR/icdc_rootfs_md5sum.tar_md5sum.txt
cd $OS_BUILD_DIR


#umount the system.img
echo "Press a enter key unmounting ..."
read KEY
sudo umount $OS_BUILD_DIR/OS

#delete the folder
echo "Press enter: deleting a folder ..."
read KEY
rm -rf $OS_BUILD_DIR/OS
