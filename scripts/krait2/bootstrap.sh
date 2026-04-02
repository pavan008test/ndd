#!/usr/bin/env bash

set -o errexit

# List of files which should go inside bootstrap tar file
TAR_FILES=(scheduler otacheck updateEngine cleanupstate nddevicelib.so)

# Wrapper scripts which should go inside bootstrap tar file
WRAPPER_SCRIPTS=(wrapper_scheduler wrapper_otacheck wrapper_cleanupstate)

# List of files which should go inside bootstrap tar file
FILES="bootstrap log latest nddevice.ini wrapper_cleanupstate wrapper_scheduler wrapper_otacheck laneCal.json nd_service.sh certificate wrapper_service.sh lte_fm_upgrade.sh isp_fm_upgrade.sh"

# Prints help message
function print_help {
  cat << EOH >&2
  Description:
    Bootstrapping script

  Mandatory Options:
    -o | --output <outputDirectory> (Default: /tmp/ota)
EOH
}

# Print warning of default output directory
function print_warning {
  echo "Warning: Using default output directory: /tmp/ota"
}

# Create output directory if not exists
function create_directory {
  echo "Creating: $1"
  [ -d $1 ] || mkdir -p $1
}

# Create link
function create_link {
  echo "Creating link: from $1 to $2"
  ln -s $1 $2
}

# Cleanup output directory if anything exists inside it
function clean_directory {
  echo "Cleaning: $1"
  rm -rf $1
}

# Execute another script
function execute_script {
  echo "Executing script: $1"
  clean_directory $(dirname $PWD)/build
  /bin/sh $1
}

# Default output directory
OUT_DIR="/tmp/ota"

# Bootstrap environment
function bootstrap_environment {
  echo "===== EXECUTING BOOTSTRAPING TASKS ====="
  clean_directory ${OUT_DIR}
  create_directory ${OUT_DIR}/bootstrap
}

# Copy file from source to destination
function copy_file {
  echo "Copying $1 to $2"
  cp $1 $2
}

# Copy set of output files
function copy_files {
  for file in ${TAR_FILES[*]}; do
    copy_file $(dirname ${PWD})/build/${file} ${OUT_DIR}/bootstrap/${file}
  done
}

# Copy set of wrapper scripts
function copy_scripts {
  for file in ${WRAPPER_SCRIPTS[*]}; do
    copy_file ${file} ${OUT_DIR}/${file}
  done
}

# Core of bootstrapping task
function bootstrap_core {
  echo "===== EXECUTING CORE TASKS ====="
  execute_script buildEdgescript.sh
  copy_files
  copy_scripts
}

# Create package
function create_package {
  echo "Create package: bootstrap_nddevice.tar.gz"
  cd $1
  tar -czf bootstrap_nddevice.tar.gz ${FILES}
}

# Wrapping up tasks
function finish_tasks {
  echo "===== EXECUTING FINISHING TASKS ====="
  copy_file ${OUT_DIR}/bagheera/conf/nddevice.ini ${OUT_DIR}
  copy_file ${OUT_DIR}/bagheera/conf/laneCal.json ${OUT_DIR}
  copy_file ${OUT_DIR}/bagheera/conf/cloudconfig.ini ${OUT_DIR}/bootstrap
  create_directory ${OUT_DIR}/log
  create_link bootstrap ${OUT_DIR}/latest
  create_package ${OUT_DIR}
}

#cloing required code and model files
function clone_code {
  echo "==== CLONE CODE ===="
  cd ${OUT_DIR} 
  echo "==== CLONE bagheera CODE ==="
  
  #git clone https://github.com/netradyne/bhageera -b bagheera_rel_2_0
  git clone git@github.com:netradyne/bagheera.git -b development 
  
  echo "==== CLONE LTE binaries ==="
  cd ${OUT_DIR} 
  #aws s3 cp s3://netradyne-sharing/bagheera/LTE/sierra.tar.bz2 .
  mkdir ${OUT_DIR}/service
  #tar -jxf sierra.tar.bz2  -C service/
  tar -jxf ${OUT_DIR}/bagheera/prebuilts/sierra.tar.bz2  -C service/
  
}

# Builds nd_core_utils codebase
function build_nd_core_utils {
  echo "==== BUILDING ND_CORE_UTILS ===="
  echo ${OUT_DIR}
  cd ${OUT_DIR}/bagheera/nd_core_utils/
  make clean
  make
}

# Builds bagheera codebase
function build_bagheera {
  echo "==== BUILDING BAGHEERA ===="
  echo ${OUT_DIR}
  cd ${OUT_DIR}/bagheera/nd-central/device/bhageera
  make
  mkdir -p ${OUT_DIR}/bootstrap/service/bagheera

  cp ${OUT_DIR}/bagheera/nd-central/device/bhageera/out/bagheera ${OUT_DIR}/bootstrap/service/bagheera/.
  #copy default files from the bagheera/script/ 
  cp ${OUT_DIR}/bagheera/scripts/bagheera.sh ${OUT_DIR}/bootstrap/service/bagheera/.
  cp ${OUT_DIR}/bagheera/scripts/bagheera.service ${OUT_DIR}/bootstrap/service/bagheera/.
  #cp ${OUT_DIR}/bagheera/scripts/wrapper_service.sh ${OUT_DIR}/bootstrap/service/bagheera/.
  cp ${OUT_DIR}/bagheera/scripts/nd_service.sh ${OUT_DIR}/.
  cp ${OUT_DIR}/bagheera/scripts/lte_fm_upgrade.sh ${OUT_DIR}/.
  cp ${OUT_DIR}/bagheera/scripts/isp_fm_upgrade.sh ${OUT_DIR}/.
  cp ${OUT_DIR}/bagheera/scripts/wrapper_service.sh ${OUT_DIR}/.
#  cp ${OUT_DIR}/bagheera/scripts/reboot.sh ${OUT_DIR}/.
}

# copy sierra binaries
function copy_sierra {
  echo "==== COPING sierra ===="
  cp -R ${OUT_DIR}/service/sierra ${OUT_DIR}/bootstrap/service/
}

# copy certificate
function copy_certificate {
  echo "==== COPING certificate ===="
  cp -R ${OUT_DIR}/bagheera/conf/certificate  ${OUT_DIR}/.
}
function main {
  bootstrap_environment
  bootstrap_core
  clone_code 
  build_nd_core_utils
  build_bagheera
  copy_sierra
  copy_certificate
  finish_tasks
}

# Script EntryPoint
case "$1" in
  -h | --help)
    print_help
    exit 0
    ;;
  -o | --output)
    OUT_DIR="$2"
    main
    ;;
  *)
    print_help
    exit 1
    ;;
esac
