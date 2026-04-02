#!/usr/bin/env bash

set -ex

OUTDIR=$(pwd)/out
ND_VDM_REPO=$(pwd)/../../../nd_vdm/
mkdir -p $OUTDIR
OUTPUT=out/run_as_root.sh
conf_file_path=$(pwd)/../../../device-build/run_as_root/krait/

if [[ $1 == "Krait_US" || $1 == "Krait_NA" ]]; then
conf_file=${conf_file_path}/run_as_root_Krait_US.conf
elif [[ $1 == "Krait_IN" ]]; then
conf_file=${conf_file_path}/run_as_root_Krait_IN.conf
elif [[ $1 == "Krait_UK" || $1 == "Krait_DE" || $1 == "Krait_ANZ" ]]; then
conf_file=${conf_file_path}/run_as_root_Krait_UK.conf
elif [[ $1 == "Krait2_US" || $1 == "Krait2_NA" ]]; then
conf_file=${conf_file_path}/run_as_root_Krait2_US.conf
elif [[ $1 == "Krait2_IN" ]]; then
conf_file=${conf_file_path}/run_as_root_Krait2_IN.conf
fi

cat << EOS > $OUTPUT
#!/usr/bin/env bash
set -e
export PARENT_SCRIPT=run_as_root
source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh
root_folder=/home/ubuntu/.nddevice/ota_temp/run_as_root
#schedule_shutdown

EOS

##run of each script
while read folder
do
	if [ -n "`awk '{print $2}' <<< "$folder"`" ]
	then 
		folder_name=$(awk '{print $1}' <<< "$folder")
		time=$(awk '{print $3}' <<< "$folder")
		echo "###$folder_name operations###" >>$OUTPUT
		echo "cd \$root_folder/$folder_name" >>$OUTPUT
		echo -e "timeout -k $time $time bash run.sh && echo \"obd FW update success\"" >>$OUTPUT
		cp -rf $(pwd)/$folder_name $OUTDIR

	else
		echo "###$folder operations###" >>$OUTPUT
		echo "cd \$root_folder/$folder" >>$OUTPUT
		echo "bash run.sh" >>$OUTPUT
		cp -rf $(pwd)/$folder $OUTDIR
	fi
done< $conf_file
echo "create_link_to_nd_config" >>$OUTPUT
echo "end_of_run" >>$OUTPUT

chmod 775 $OUTPUT
cp $(pwd)/run_as_root.service $OUTDIR/
cp $(pwd)/run_as_root_statuscheck.sh $OUTDIR/
cp $(pwd)/run_as_root_lib.sh $OUTDIR/
cp $conf_file $OUTDIR/

#Explicit copy of libbluetooth lib to copy_libbluetooth folder
if [[ -d ${OUTDIR}/copy_libbluetooth ]]; then
	if [[ "$1" == Krait_* ]]; then
		cp -f $(pwd)/../../prebuilts/libbluetooth/krait/libbluetoothdefault.so.0.0.0 ${OUTDIR}/copy_libbluetooth/
	elif [[ "$1" == Krait2_* ]]; then
		cp -f $(pwd)/../../prebuilts/libbluetooth/krait2/libbluetoothdefault.so.0.0.0 ${OUTDIR}/copy_libbluetooth/
	fi
fi

#Explicit copy of kinesis libs to runasroot task folder
if [[ -d ${OUTDIR}/copy_kinesis_libs ]]; then
cp -rf $(pwd)/../../nd-central/device/krait/kvs_sdk/lib/*.so* ${OUTDIR}/copy_kinesis_libs/
fi

#Explicit copy of nd_service.sh to folder copy_nd_service_script
if [[ -d out/copy_nd_service_script ]] ; then
cp -rf $(pwd)/../../scripts/${PRODUCT}/nd_service.sh ${OUTDIR}/copy_nd_service_script/
fi

#Explicit copy of libcrypto and libssl lib from nd_core_utils repo to copy_libssl_libcrypto folder
if [[ -d out/copy_libssl_libcrypto ]] ; then
mkdir -p ${OUTDIR}/copy_libssl_libcrypto/arm32/
mkdir -p ${OUTDIR}/copy_libssl_libcrypto/arm64/
cp -rf $(pwd)/../../../nd_core_utils/nd_thirdparty_lib/krait/packages/openssl_1.1.1s/arm32/lib/libssl.so.1.1  ${OUTDIR}/copy_libssl_libcrypto/arm32/
cp -rf $(pwd)/../../../nd_core_utils/nd_thirdparty_lib/krait/packages/openssl_1.1.1s/arm32/lib/libcrypto.so.1.1  ${OUTDIR}/copy_libssl_libcrypto/arm32/
cp -rf $(pwd)/../../../nd_core_utils/nd_thirdparty_lib/krait/packages/openssl_1.1.1s/arm64/lib/libssl.so.1.1  ${OUTDIR}/copy_libssl_libcrypto/arm64/
cp -rf $(pwd)/../../../nd_core_utils/nd_thirdparty_lib/krait/packages/openssl_1.1.1s/arm64/lib/libcrypto.so.1.1  ${OUTDIR}/copy_libssl_libcrypto/arm64/
fi

#Explicit copy of libcrypto, libssl and awsiot libs from nd_core_utils and nd_device_services repo to copy_libssl_libcrypto_awsiot folder
if [[ -d ${OUTDIR}/copy_libssl_libcrypto_awsiot ]]
then
	mkdir -p ${OUTDIR}/copy_libssl_libcrypto_awsiot/krait/arm32/
	mkdir -p ${OUTDIR}/copy_libssl_libcrypto_awsiot/krait/arm64/
	mkdir -p ${OUTDIR}/copy_libssl_libcrypto_awsiot/krait/awsiot_lib/
	cp $(pwd)/../../../nd_core_utils/nd_thirdparty_lib/krait/packages/openssl_1.1.1v/arm32/lib/libcrypto.so.1.1 ${OUTDIR}/copy_libssl_libcrypto_awsiot/krait/arm32/
	cp $(pwd)/../../../nd_core_utils/nd_thirdparty_lib/krait/packages/openssl_1.1.1v/arm32/lib/libssl.so.1.1 ${OUTDIR}/copy_libssl_libcrypto_awsiot/krait/arm32/
	cp $(pwd)/../../../nd_core_utils/nd_thirdparty_lib/krait/packages/openssl_1.1.1v/arm64/bin/openssl ${OUTDIR}/copy_libssl_libcrypto_awsiot/krait/arm64/
	cp $(pwd)/../../../nd_core_utils/nd_thirdparty_lib/krait/packages/openssl_1.1.1v/arm64/lib/libcrypto.so.1.1 ${OUTDIR}/copy_libssl_libcrypto_awsiot/krait/arm64/
	cp $(pwd)/../../../nd_core_utils/nd_thirdparty_lib/krait/packages/openssl_1.1.1v/arm64/lib/libssl.so.1.1 ${OUTDIR}/copy_libssl_libcrypto_awsiot/krait/arm64/
	cp -rf --preserve $(pwd)/../../../nd_core_utils/nd_thirdparty_lib/krait/packages/iot-sdk/lib/* ${OUTDIR}/copy_libssl_libcrypto_awsiot/krait/awsiot_lib/
fi

#Downloading firmware_WP7609.zip file into copy_wp7609_sierra_fw/ folder
if [[ -d ${OUTDIR}/copy_wp7609_sierra_fw ]]; then
cp -f $(pwd)/../prebuilts/firmware/sierra/scripts/sierra_fw_upgrade_WP7609_* ${OUTDIR}/copy_wp7609_sierra_fw/
cp -f $(pwd)/../prebuilts/firmware/sierra/scripts/lte_gps_sierra_upgrade* ${OUTDIR}/copy_wp7609_sierra_fw/
s3cmd get --recursive s3://netradyne-sharing/sierra_fw/firmware_WP7609.zip ${OUTDIR}/copy_wp7609_sierra_fw
fi

#Downloading mdvr fw file into copy_mdvr_fw/ folder
if [[ -d out/copy_mdvr_fw ]]; then
cp -f $(pwd)/../../prebuilts/firmware/mdvr/gen4/FL3521A_4CH-V18062301-V18070301-V18112801-S25122501.35848.sw out/copy_mdvr_fw/
fi 

if [[ -d ${OUTDIR}/install_python3.9 ]];then
s3cmd get s3://netradyne-sharing/python3.9.14/python3.9_setup_krait.tar.gz ${OUTDIR}/install_python3.9/
s3cmd get s3://netradyne-sharing/python3.9.14/krait/python3.9_krait_whl.tar.gz ${OUTDIR}/install_python3.9/
fi

#Explicit copy of libboost_filesystem and libboost_system lib from cross_compiler repo to copy_Boost_libraries folder
if [[ -d out/copy_Boost_libraries ]] ; then
mkdir -p ${OUTDIR}/copy_Boost_libraries/arm32/
mkdir -p ${OUTDIR}/copy_Boost_libraries/arm64/
cp -rf $(pwd)/../../../cross_compiler/krait/rootfs/usr/lib/libboost_system.so.1.64.0  ${OUTDIR}/copy_Boost_libraries/arm32/
cp -rf $(pwd)/../../../cross_compiler/krait/rootfs/usr/lib/libboost_filesystem.so.1.64.0  ${OUTDIR}/copy_Boost_libraries/arm32/
cp -rf $(pwd)/../../../cross_compiler/krait/rootfs/usr/lib64/libboost_system.so.1.64.0  ${OUTDIR}/copy_Boost_libraries/arm64/
cp -rf $(pwd)/../../../cross_compiler/krait/rootfs/usr/lib64/libboost_filesystem.so.1.64.0  ${OUTDIR}/copy_Boost_libraries/arm64/
fi

#Explicit copy of vdm_iosix.ini config file from nd_vdm repo to copy_vdm_iosix_config folder
if [[ -d out/copy_vdm_iosix_config ]] ; then
cp -rf $(pwd)/../../../nd_vdm/conf/vdm_iosix.ini  ${OUTDIR}/copy_vdm_iosix_config/
fi

#Explicit copy of vdm_can_adapter.ini & iosix_config.ini config file from nd_vdm repo to copy_iosix_config_vdm_can_file folder
if [[ -d out/copy_iosix_config_vdm_can_file ]] ; then
cp -rf $(pwd)/../../../nd_vdm/conf/vdm_can_adapter.ini  ${OUTDIR}/copy_iosix_config_vdm_can_file/
cp -rf $(pwd)/../../../nd_vdm/conf/iosix_config.ini  ${OUTDIR}/copy_iosix_config_vdm_can_file/
fi

#Explicit copy of 423B.bin,fw_details.ini,vbus_fw_version.ini from nd_vdm repo to copy_vbus_ini_files folder
if [[ -d out/copy_vbus_ini_files ]] ; then
#cp -f ${ND_VDM_REPO}/prebuilts/firmware/vbus_v1/* ${OUTDIR}/copy_vbus_ini_files/
#cp -f ${ND_VDM_REPO}/prebuilts/firmware/vbus_v3/* ${OUTDIR}/copy_vbus_ini_files/
cp -f ${ND_VDM_REPO}/conf/vbus_common_config.ini ${OUTDIR}/copy_vbus_ini_files/
cp -f ${ND_VDM_REPO}/prebuilts/firmware/vbus_v1/fw_details.ini ${OUTDIR}/copy_vbus_ini_files/
fi


#Explicit copy of accessory_details.ini from conf folder
if [[ -d out/copy_accessory_details ]] ; then
cp -rf $(pwd)/../../conf/${PRODUCT}/accessory_details.ini ${OUTDIR}/copy_accessory_details/
fi

#Explicit copy of slotchangerofix.sh from script folder
if [[ -d out/copy_early_slot_slotchangerofix ]] ; then
cp -rf $(pwd)/../../scripts/${PRODUCT}/slotchangerofix.sh ${OUTDIR}/copy_early_slot_slotchangerofix/
fi

#Explicit copy of remount_wlanmdsp for specific krait version
if [[ -d ${OUTDIR}/copy_remount_wlanmdsp ]]; then
	if [[ "$1" == Krait_* ]]; then
		cp -f $(pwd)/../../prebuilts/krait1/copy_remount_wlanmdsp/wlanmdsp.mbn ${OUTDIR}/copy_remount_wlanmdsp/
	elif [[ "$1" == Krait2_* ]]; then
		cp -f $(pwd)/../../prebuilts/krait2/copy_remount_wlanmdsp/wlanmdsp.mbn ${OUTDIR}/copy_remount_wlanmdsp/
	fi
fi
