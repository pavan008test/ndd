#/*********************************************************************************
#*********************************************************************************/

# cmake .. -DFOR_TARGET=BAGHEERA2

cmake_minimum_required (VERSION 3.5)
project (ndbt)

if (NOT ND_CORE_LIB_ROOT)
    set (ND_CORE_LIB_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../../nd_core_utils)
endif (NOT ND_CORE_LIB_ROOT)

if (${FOR_TARGET} STREQUAL "BAGHEERA")
    set (BT_LIB_LINK_DIRECTORIES ${BT_LIB_LINK_DIRECTORIES}
        ${ND_CORE_LIB_ROOT}/nd_thirdparty_lib/bagheera/packages/openssl_1.1.1v/lib
    )
elseif (${FOR_TARGET} STREQUAL "BAGHEERA2")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GLIB2 REQUIRED glib-2.0)
    pkg_check_modules(GIO2 REQUIRED gio-2.0)
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DBAGHEERA2 -Wnull-dereference")
    set (BT_LIB_INC_DIRECTORIES
         ${BT_LIB_INC_DIRECTORIES}
	 ${CMAKE_SOURCE_DIR}/inc/lib/hci
	 ${ND_CORE_LIB_ROOT}/nd_thirdparty_lib/bagheera/inc
         ${ND_CORE_LIB_ROOT}/nd_thirdparty_lib/bagheera/inc/gattlib
	 ${CMAKE_SOURCE_DIR}/inc/lib/dbus
         ${ND_CORE_LIB_ROOT}/cpp/inc/bagheera3
         ${GIO2_INCLUDE_DIRS}
         ${GLIB2_INCLUDE_DIRS}

    )
    set (BT_LIB_LINK_DIRECTORIES ${BT_LIB_LINK_DIRECTORIES}
                                 ${ND_CORE_LIB_ROOT}/cpp/out
                                 ${ND_CORE_LIB_ROOT}/nd_thirdparty_lib/bagheera/lib
                                 ${GLIB2_LIBRARY_DIRS}
                                 ${GIO2_LIBRARY_DIRS}
    )
    set (ndbt_SRCS
        ${ndbt_SRCS}
        ${CMAKE_SOURCE_DIR}/src/lib/hci/nd_bt_hci_device.cpp
        ${CMAKE_SOURCE_DIR}/src/lib/dbus/nd_bt_dbus_device.cpp
    )
elseif (${FOR_TARGET} STREQUAL "BAGHEERA3")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GLIB2 REQUIRED glib-2.0)
    pkg_check_modules(GIO2 REQUIRED gio-2.0)
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DBAGHEERA2 -Wnull-dereference")
    set (BT_LIB_INC_DIRECTORIES
         ${BT_LIB_INC_DIRECTORIES}
	 ${CMAKE_SOURCE_DIR}/inc/lib/hci
	 ${ND_CORE_LIB_ROOT}/nd_thirdparty_lib/bagheera/inc
	 ${ND_CORE_LIB_ROOT}/nd_thirdparty_lib/bagheera/inc/gattlib
	 ${CMAKE_SOURCE_DIR}/inc/lib/dbus
         ${ND_CORE_LIB_ROOT}/cpp/inc/bagheera3
         ${GIO2_INCLUDE_DIRS}
         ${GLIB2_INCLUDE_DIRS}
    )
    set (BT_LIB_LINK_DIRECTORIES ${BT_LIB_LINK_DIRECTORIES}
                                 ${ND_CORE_LIB_ROOT}/cpp/out
                                 ${ND_CORE_LIB_ROOT}/nd_thirdparty_lib/bagheera/lib
                                 ${GLIB2_LIBRARY_DIRS}
                                 ${GIO2_LIBRARY_DIRS}
    )
    set (ndbt_SRCS
        ${ndbt_SRCS}
        ${CMAKE_SOURCE_DIR}/src/lib/hci/nd_bt_hci_device.cpp
        ${CMAKE_SOURCE_DIR}/src/lib/dbus/nd_bt_dbus_device.cpp
    )
elseif (${FOR_TARGET} STREQUAL "KRAIT")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DKRAIT -Wnull-dereference")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC -fstack-protector-strong -pie -fPIE -D_FORTIFY_SOURCE=2 -Wa,--noexecstack -Wformat -Wformat-security -Werror=format-security")
    set (CMAKE_INCLUDE_PATH ${CMAKE_INCLUDE_PATH} ${ND_CORE_LIB_ROOT}/nd_thirdparty_lib/common/inc/boost_1_64_0)
    set (CMAKE_LIBRARY_PATH ${CMAKE_LIBRARY_PATH} ${CROSS_COMPLER_BASE}/krait/rootfs/usr/lib64)
    set (BT_LIB_INC_DIRECTORIES
        ${BT_LIB_INC_DIRECTORIES}
	${CMAKE_SOURCE_DIR}/inc/lib/qcom
	${ND_CORE_LIB_ROOT}/nd_thirdparty_lib/krait/inc/bt
        ${CROSS_COMPLER_BASE}/krait/lib32-qmmf-sdk/git-r0/lib32-recipe-sysroot/usr/include
        ${CROSS_COMPLER_BASE}/krait/recipe-sysroot-native/usr/include
        ${CMAKE_SYSROOT}
    )
    set (BT_LIB_LINK_DIRECTORIES ${BT_LIB_LINK_DIRECTORIES}
                                 ${ND_CORE_LIB_ROOT}/cpp/out_64
                                 ${CROSS_COMPLER_BASE}/krait/rootfs/usr/lib64
                                 ${ND_CORE_LIB_ROOT}/nd_thirdparty_lib/krait/lib/arm64
                                 ${CROSS_COMPLER_BASE}/krait/curl/7.58.0-r0/package/usr/lib64
                                 ${ND_CORE_LIB_ROOT}/nd_thirdparty_lib/krait/packages/openssl_1.1.1v/arm64/lib
    )
    set (ndbt_SRCS
        ${ndbt_SRCS}
        ${CMAKE_SOURCE_DIR}/src/lib/qcom/nd_bt_qcom_device.cpp
        ${ND_CORE_LIB_ROOT}/nd_thirdparty_lib/krait/inc/bt/Gatt.cpp
    )
elseif (${FOR_TARGET} STREQUAL "x86")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DBAGHEERA2 -Wnull-dereference")
    set (BT_LIB_INC_DIRECTORIES
         ${BT_LIB_INC_DIRECTORIES}
	 ${CMAKE_SOURCE_DIR}/inc/lib/hci
	 ${ND_CORE_LIB_ROOT}/nd_thirdparty_lib/bagheera/inc
         ${ND_CORE_LIB_ROOT}/nd_thirdparty_lib/bagheera/inc/gattlib
    )
    set (BT_LIB_LINK_DIRECTORIES ${BT_LIB_LINK_DIRECTORIES}
                                 ${ND_CORE_LIB_ROOT}/cpp/out
                                 ${ND_CORE_LIB_ROOT}/nd_thirdparty_lib/x86/lib
    )
    set (ndbt_SRCS
        ${ndbt_SRCS}
        ${CMAKE_SOURCE_DIR}/src/lib/hci/nd_bt_hci_device.cpp
    )
endif ()

set (CMAKE_CXX_STANDARD 14)
# set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DFIREWALL_ENABLE -DENABLE_ENCRYPTION -DENABLE_DECRYPTION")
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror=return-type")
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")

if (NOT NDCORESO_PATH)
    set (NDCORESO_PATH /home/ubuntu/.nddevice/latest/)
endif (NOT NDCORESO_PATH)

set (BT_LIB_INC_DIRECTORIES
    ${BT_LIB_INC_DIRECTORIES}
    ${CMAKE_SOURCE_DIR}/inc/lib
    ${CMAKE_SOURCE_DIR}/inc/utils
    ${ND_CORE_LIB_ROOT}/cpp/inc
    ${ND_CORE_LIB_ROOT}/nd_thirdparty_lib/common/inc
    ${Boost_INCLUDE_DIR}
)

# Header Files
include_directories(
    ${BT_LIB_INC_DIRECTORIES}
)

# library directories
link_directories(
    ${BT_LIB_LINK_DIRECTORIES}
)

# Source Files
set (ndbt_SRCS
    ${ndbt_SRCS}
)

set(CMAKE_SKIP_BUILD_RPATH FALSE)
set(CMAKE_INSTALL_RPATH ${NDCORESO_PATH})
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

option(RELEASE "Enable release mode" OFF)
message(STATUS "RELEASE is set to: ${RELEASE}")
add_library (ndbt SHARED ${ndbt_SRCS})
if(RELEASE)
    set_target_properties(ndbt PROPERTIES
    LINK_FLAGS "-Wl,-Map=ndbt.map"
    )
endif()

if (${FOR_TARGET} STREQUAL "BAGHEERA")
    set (BT_LIB_TARGET_LINK_LIBRARIES ${BT_LIB_TARGET_LINK_LIBRARIES} -l:libcrypto.so.1.1 gattlib curl)
elseif (${FOR_TARGET} STREQUAL "KRAIT")
    set (BT_LIB_TARGET_LINK_LIBRARIES ${BT_LIB_TARGET_LINK_LIBRARIES} cutils g726 curl -l:libssl.so.1.1 -l:libcrypto.so.1.1 btvendorhal z dw-0.170 elf-0.170 -l:libunwind.so.0.0.0 -l:libzmq.so tinyalsa)
    target_link_options(ndbt PRIVATE
        "-Wl,--start-group"
        "-Wl,--end-group"
    )
else()
    set (BT_LIB_TARGET_LINK_LIBRARIES ${BT_LIB_TARGET_LINK_LIBRARIES} avutil avcodec zmq crypto gattlib curl)
endif ()

set (BT_LIB_TARGET_LINK_LIBRARIES ${BT_LIB_TARGET_LINK_LIBRARIES} rt sys iniparser jansson pthread dl sqlite3)

target_link_libraries (ndbt ndcore ${BT_LIB_TARGET_LINK_LIBRARIES})

set_target_properties (ndbt PROPERTIES LINKER_LANGUAGE CXX)
