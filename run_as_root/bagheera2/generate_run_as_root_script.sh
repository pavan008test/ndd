#!/usr/bin/env bash

set -ex

OUTDIR=$(pwd)/out
mkdir -p $OUTDIR
OUTPUT=out/run_as_root.sh
ND_VDM_REPO=$(pwd)/../../../nd_vdm/
conf_file_path=$(pwd)/../../../device-build/run_as_root/bagheera2/

if [[ $1 == "bagheera2_US" ]]; then
conf_file=${conf_file_path}/run_as_root_bagheera2_US.conf
elif [[ $1 == "bagheera2_IN" ]]; then
conf_file=${conf_file_path}/run_as_root_bagheera2_IN.conf
else
conf_file=${conf_file_path}/run_as_root_bagheera2_US.conf
fi

echo "#!/usr/bin/env bash" >$OUTPUT
echo "set -e" >>$OUTPUT
echo "export PARENT_SCRIPT=run_as_root" >>$OUTPUT
echo "source /home/ubuntu/.nddevice/ota_temp/run_as_root/run_as_root_lib.sh" >>$OUTPUT
echo "root_folder=/home/ubuntu/.nddevice/ota_temp/run_as_root">>$OUTPUT
echo "schedule_shutdown" >>$OUTPUT
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
		cp -rf $(pwd)/$folder_name $(pwd)/out/
	else
		echo "###$folder operations###" >>$OUTPUT
		echo "cd \$root_folder/$folder" >>$OUTPUT
		echo "bash run.sh" >>$OUTPUT
		cp -rf $(pwd)/$folder $(pwd)/out/
	fi


done<$conf_file

echo "create_link_to_nd_config" >>$OUTPUT
echo "end_of_run" >>$OUTPUT

chmod 775 $OUTPUT
cp $(pwd)/run_as_root.service $(pwd)/out/
cp $(pwd)/run_as_root_statuscheck.sh $(pwd)/out/
cp $(pwd)/run_as_root_lib.sh $(pwd)/out/
cp $conf_file $(pwd)/out/

#Explicit copy of opencv3.4.2 libs from s3 to copy_opencv3.4.2
if [[ -d out/copy_opencv3.4.2 ]]; then
s3cmd get s3://netradyne-sharing/analytics/binaries/bagheera/opencv3.4.2_cutdown_25082020.tar.gz out/copy_opencv3.4.2/
fi

#Explicit copy of black video from s3 to copy_blackvideo
if [[ -d out/copy_blackvideo ]]; then
s3cmd get s3://netradyne-sharing/bagheera2_blackvideo/3.6.3/blackvideo_editing*.mp4 out/copy_blackvideo/
fi

if [[ -d out/install_python3.9 ]]; then
s3cmd get --force s3://netradyne-sharing/python3.9.14/python3.9.14_setup1_B2.tar.gz out/install_python3.9/
fi

#Downloading mdvr fw file into copy_mdvr_fw/ folder
if [[ -d out/copy_mdvr_fw ]]; then
cp -f $(pwd)/../../prebuilts/firmware/mdvr/gen4/FL3521A_4CH-V18062301-V18070301-V18112801-S25122501.35848.sw out/copy_mdvr_fw/
fi 

#Explicit copy of vdm_iosix.ini config file from nd_vdm repo to copy_vdm_iosix_config folder
if [[ -d out/copy_vdm_iosix_config ]] ; then
cp -rf $(pwd)/../../../nd_vdm/conf/vdm_iosix.ini out/copy_vdm_iosix_config/
fi

#Explicit copy of vdm_can_adapter.ini & iosix_config.ini config file from nd_vdm repo to copy_iosix_config_vdm_can_file folder
if [[ -d out/copy_iosix_config_vdm_can_file ]] ; then
cp -rf $(pwd)/../../../nd_vdm/conf/vdm_can_adapter.ini out/copy_iosix_config_vdm_can_file/
cp -rf $(pwd)/../../../nd_vdm/conf/iosix_config.ini out/copy_iosix_config_vdm_can_file/
fi

#Explicit copy of fw_version.ini from nd_vdm repo to copy_vbus_ini_files folder
if [[ -d out/copy_vbus_ini_files ]]; then
# cp -f ${ND_VDM_REPO}/conf/vbus_common_config.ini ${OUTDIR}/copy_vbus_ini_files/
cp -f ${ND_VDM_REPO}/prebuilts/firmware/vbus_v1/fw_details.ini ${OUTDIR}/copy_vbus_ini_files/
fi

#Explicit copy of nd_service.sh to folder copy_nd_service_script
if [[ -d out/copy_nd_service_script ]]; then
cp $(pwd)/../scripts/nd_service.sh out/copy_nd_service_script/
fi

#Explicit get libprotobuf.so.14 from s3 when task is defined
if [[ -d out/copy_updated_libssl_crypto_protobuf ]]; then
s3cmd get --force s3://nd-device-artifacts/bagheera_libprotobuf.3.4.0/libprotobuf.so.14.0.0 out/copy_updated_libssl_crypto_protobuf/libprotobuf.so.14
fi

#Explicit get the XGB 1.6.1 and scikit-learn 1.1.2 whl files from s3 to install_scikit-learn_XGB
if [[ -d out/install_scikit-learn_XGB ]]; then
s3cmd get --force s3://netradyne-sharing/python3.9.14/install_XGB_scikit-learn.tar.gz out/install_scikit-learn_XGB/
fi

#Explicit get the D430_TRT8_with_dev.tar.gz from s3 to install_D430_trt8_withdev
if [[ -d out/install_D430_trt8_withdev ]]; then
s3cmd get --force s3://netradyne-sharing/TRT_821/D430_TRT8_with_dev.tar.gz out/install_D430_trt8_withdev/
fi

#Explicit getting the nvidia-l4t-kernel_4.9.140-tegra-32.4.3-20200625213407_arm64.deb from s3 to install_kernal_forinwardquality
if [[ -d out/install_kernal_forinwardquality ]]; then
s3cmd get --force s3://netradyne-sharing/irled/nvidia-l4t-kernel_4.9.140-tegra-32.4.3-20200625213407_arm64.deb out/install_kernal_forinwardquality/
fi

#Explicit copy of libcrypto, libssl and awsiot libs from nd_core_utils and nd_device_services repo to copy_libssl_libcrypto_awsiot folder
if [[ -d ${OUTDIR}/copy_libssl_libcrypto_awsiot ]]
then
	mkdir -p ${OUTDIR}/copy_libssl_libcrypto_awsiot/bagheera/awsiot_lib/
	cp $(pwd)/../../../nd_core_utils/nd_thirdparty_lib/bagheera/packages/openssl_1.1.1v/bin/openssl ${OUTDIR}/copy_libssl_libcrypto_awsiot/bagheera/
	cp $(pwd)/../../../nd_core_utils/nd_thirdparty_lib/bagheera/packages/openssl_1.1.1v/lib/libcrypto.so.1.1 ${OUTDIR}/copy_libssl_libcrypto_awsiot/bagheera/
	cp $(pwd)/../../../nd_core_utils/nd_thirdparty_lib/bagheera/packages/openssl_1.1.1v/lib/libssl.so.1.1 ${OUTDIR}/copy_libssl_libcrypto_awsiot/bagheera/
	cp -rf --preserve $(pwd)/../../../nd_core_utils/nd_thirdparty_lib/bagheera/packages/iot-sdk/lib/* ${OUTDIR}/copy_libssl_libcrypto_awsiot/bagheera/awsiot_lib/
fi

#Explicit copy of accessory_details.ini from conf folder
if [[ -d out/copy_accessory_details ]] ; then
cp -rf $(pwd)/../../conf/${PRODUCT}/accessory_details.ini ${OUTDIR}/copy_accessory_details/
fi

