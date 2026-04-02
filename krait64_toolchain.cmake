# Copyright (C) 2023 NetraDyne, Inc - All Rights Reserved
# Unauthorized copying of this file, via any medium is strictly prohibited
# Proprietary and confidential
# Written by Sunil KS <sunil.sampath@netradyne.com>, June 2023
#
set( CMAKE_SYSTEM_NAME Linux )
set( CMAKE_SYSTEM_PROCESSOR arm )
list( APPEND CMAKE_PREFIX_PATH ${CMAKE_CURRENT_LIST_DIR}/../cross_compiler/krait/recipe-sysroot-native/usr/bin/aarch64-oe-linux)
set( CMAKE_C_COMPILER aarch64-oe-linux-gcc )
set( CMAKE_CXX_COMPILER aarch64-oe-linux-g++ )
set( CROSS_COMPLER_BASE ${CMAKE_CURRENT_LIST_DIR}/../cross_compiler )
set( ND_CORE_BASE ${CMAKE_CURRENT_LIST_DIR}/../nd_core_utils )
set( REPO_BASE ${CMAKE_CURRENT_LIST_DIR} )
set( CMAKE_SYSROOT ${CROSS_COMPLER_BASE}/krait/recipe-sysroot )
set( SYSROOT_NATIVE ${CROSS_COMPLER_BASE}/krait/recipe-sysroot-native )
set( KVS_LIB64_PATH ${ND_CORE_BASE}/cpp/lib/kvs_sdk/krait/lib64 )
