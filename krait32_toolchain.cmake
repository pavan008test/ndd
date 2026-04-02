# Copyright (C) 2023 NetraDyne, Inc - All Rights Reserved
# Unauthorized copying of this file, via any medium is strictly prohibited
# Proprietary and confidential
# Written by Sunil KS <sunil.sampath@netradyne.com>, June 2023
#
set( CMAKE_SYSTEM_NAME Linux )
set( CMAKE_SYSTEM_PROCESSOR armv7-a )
list( APPEND CMAKE_PREFIX_PATH ${CMAKE_CURRENT_LIST_DIR}/../cross_compiler/krait/lib32-qmmf-sdk/git-r0/recipe-sysroot-native/usr/bin/arm-oe-linux-gnueabi )
set( CMAKE_C_COMPILER arm-oe-linux-gnueabi-gcc )
set( CMAKE_CXX_COMPILER arm-oe-linux-gnueabi-g++ )
set( CROSS_COMPLER_BASE ${CMAKE_CURRENT_LIST_DIR}/../cross_compiler )
set( REPO_BASE ${CMAKE_CURRENT_LIST_DIR} )
set( CMAKE_SYSROOT ${CROSS_COMPLER_BASE}/krait/lib32-qmmf-sdk/git-r0/lib32-recipe-sysroot )
set( SYSROOT_NATIVE ${CROSS_COMPLER_BASE}/krait/lib32-qmmf-sdk/git-r0/recipe-sysroot-native )
set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=armv7-a -mfloat-abi=hard -D_GLIBCXX_USE_CXX11_ABI=0" )
set( CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=armv7-a -mfloat-abi=hard -D_GLIBCXX_USE_CXX11_ABI=0" )
