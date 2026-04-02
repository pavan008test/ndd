#!/bin/bash
set -e

source $1

echo "Creating release directory" bagheera_rel_${rel[VER]}
rm -rf "bagheera_rel_${rel[VER]}"
mkdir "bagheera_rel_${rel[VER]}"
cd bagheera_rel_${rel[VER]}
mkdir bagheera_rel_${rel[VER]}

#########################BAGHEERA##################################
mkdir -p bagheera_rel_${rel[VER]}/package/bagheera_service

echo "Syncing bagheera with ${rel[BAGHEERA]} ..."
rm -rf bagheera
#git clone https://github.com/sureshkumar-nd/bhageera -b dvt1_bhageers
git clone https://github.com/netradyne/bhageera -b bagheera_rel_2_0
cd bhageera
git checkout ${rel[BAGHEERA]}
echo "Sync done"

echo "Building bagheera..."
cd device/bhageera
make clean ; make all
echo "Build done"

echo "Copying executables ..."

cd ../../../
cp bhageera/device/bhageera/out/bagheera  bagheera_rel_${rel[VER]}/package/bagheera_service/bagheera

echo "Copy done"
###################################################################


####################Analytics#####################################
mkdir -p bagheera_rel_${rel[VER]}/package/nddevice/latest

echo "Syncing Analytics with ${rel[ANALYTICS]} ..."
rm -rf analytics
git clone https://github.com/netradyne/analytics.git
cd analytics
git checkout ${rel[ANALYTICS]}
echo "Sync done"

echo "Building Analytics..."
cd src
chmod 777 ./buildInference.sh
./buildInference.sh
echo "Build done"

echo "Copying executables ..."
cd ../../
cp analytics/build/* bagheera_rel_${rel[VER]}/package/nddevice/latest
echo "Copy done"
###################################################################

#######################Shield######################################

echo "Syncing Shield with ${rel[ANALYTICS]} ..."
rm -rf shield
git clone https://github.com/netradyne/shield.git
cd shield
git checkout ${rel[SHIELD]}
echo "Sync done"

echo "Building Shield..."
cd scripts
chmod 777 ./buildEdgescript.sh
./buildEdgescript.sh
echo "Build done"

echo "Copying executables ..."
cd ../../
cp ./shield/build/* bagheera_rel_${rel[VER]}/package/nddevice/latest
echo "Copy done"

echo "Copying Wrappers ..."
mkdir -p bagheera_rel_${rel[VER]}/package/bin
cp ./shield/scripts/wrapper_* bagheera_rel_${rel[VER]}/package/bin
###################################################################



#########################AUTOCAM###################################
###################################################################


##########################TYCHO2##################################
###################################################################


##########################LANECAL##################################
###################################################################


###########################Install.sh##############################
###################################################################


echo "Done with Script"

