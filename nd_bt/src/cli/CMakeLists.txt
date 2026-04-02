#/*********************************************************************************
#*********************************************************************************/

cmake_minimum_required (VERSION 3.5)
project (nd_bt)

if (NOT ND_CORE_LIB_ROOT)
    set (ND_CORE_LIB_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../../nd_core_utils)
endif (NOT ND_CORE_LIB_ROOT)

if (${FOR_TARGET} STREQUAL "BAGHEERA")
    set (BT_CLI_LINK_DIRECTORIES ${BT_CLI_LINK_DIRECTORIES}
        ${ND_CORE_LIB_ROOT}/nd_thirdparty_lib/bagheera/packages/openssl_1.1.1v/lib
    )
elseif (${FOR_TARGET} STREQUAL "BAGHEERA2")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DBAGHEERA2 -Wnull-dereference")
    set (BT_CLI_INC_DIRECTORIES
         ${BT_CLI_INC_DIRECTORIES}
         ${CMAKE_SOURCE_DIR}/inc/lib/hci
         ${ND_CORE_LIB_ROOT}/cpp/inc/bagheera2
    )
    set (BT_CLI_LINK_DIRECTORIES ${BT_CLI_LINK_DIRECTORIES}
                                 ${ND_CORE_LIB_ROOT}/cpp/out
                                 ${ND_CORE_LIB_ROOT}/nd_thirdparty_lib/bagheera/lib
    )
elseif (${FOR_TARGET} STREQUAL "BAGHEERA3")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DBAGHEERA2 -Wnull-dereference")
    set (BT_CLI_INC_DIRECTORIES
         ${BT_CLI_INC_DIRECTORIES}
         ${CMAKE_SOURCE_DIR}/inc/lib/hci
         ${ND_CORE_LIB_ROOT}/cpp/inc/bagheera3
    )
    set (BT_CLI_LINK_DIRECTORIES ${BT_CLI_LINK_DIRECTORIES}
                                 ${ND_CORE_LIB_ROOT}/cpp/out
                                 ${ND_CORE_LIB_ROOT}/nd_thirdparty_lib/bagheera/lib
    )
elseif (${FOR_TARGET} STREQUAL "KRAIT")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DKRAIT -Wnull-dereference")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC -fstack-protector-strong -pie -g -fPIE -D_FORTIFY_SOURCE=2 -Wa,--noexecstack -Wformat -Wformat-security -Werror=format-security")
    set (CMAKE_INCLUDE_PATH ${CMAKE_INCLUDE_PATH} ${CROSS_COMPLER_BASE}/krait/rootfs/usr/include)
    set (CMAKE_LIBRARY_PATH ${CMAKE_LIBRARY_PATH} ${CROSS_COMPLER_BASE}/krait/rootfs/usr/lib64)
    set (BT_CLI_INC_DIRECTORIES
        ${BT_CLI_INC_DIRECTORIES}
        ${CMAKE_SOURCE_DIR}/inc/lib/qcom
        ${CROSS_COMPLER_BASE}/krait/lib32-qmmf-sdk/git-r0/lib32-recipe-sysroot/usr/include
        ${CROSS_COMPLER_BASE}/krait/recipe-sysroot-native/usr/include
        ${CMAKE_SYSROOT}
        ${ND_CORE_LIB_ROOT}/cpp/inc/krait
    )
    set (BT_CLI_LINK_DIRECTORIES ${BT_CLI_LINK_DIRECTORIES}
                                 ${ND_CORE_LIB_ROOT}/cpp/out_64
                                 ${CROSS_COMPLER_BASE}/krait/rootfs/usr/lib64
                                 ${ND_CORE_LIB_ROOT}/nd_thirdparty_lib/krait/lib/arm64
                                 ${CROSS_COMPLER_BASE}/krait/curl/7.58.0-r0/package/usr/lib64
                                 ${ND_CORE_LIB_ROOT}/nd_thirdparty_lib/krait/packages/openssl_1.1.1v/arm64/lib
    )
elseif (${FOR_TARGET} STREQUAL "x86")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DBAGHEERA2 -Wnull-dereference")
    set (BT_CLI_INC_DIRECTORIES
         ${BT_CLI_INC_DIRECTORIES}
         ${CMAKE_SOURCE_DIR}/inc/lib/hci
         ${ND_CORE_LIB_ROOT}/cpp/inc/bagheera2
    )
    set (BT_CLI_LINK_DIRECTORIES ${BT_CLI_LINK_DIRECTORIES}
                                 ${ND_CORE_LIB_ROOT}/cpp/out
                                 ${ND_CORE_LIB_ROOT}/nd_thirdparty_lib/x86/lib
    )
endif ()

set (CMAKE_CXX_STANDARD 14)
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror=return-type")

if (NOT NDCORESO_PATH)
    set (NDCORESO_PATH /home/ubuntu/.nddevice/latest/)
endif (NOT NDCORESO_PATH)

set (BT_CLI_INC_DIRECTORIES
    ${CMAKE_SOURCE_DIR}/inc/daemon
    ${CMAKE_SOURCE_DIR}/inc/lib
    ${CMAKE_SOURCE_DIR}/inc/utils
    ${ND_CORE_LIB_ROOT}/cpp/inc
    ${ND_CORE_LIB_ROOT}/nd_thirdparty_lib/common/inc
    ${BT_CLI_INC_DIRECTORIES}
)

# Header Files

include_directories(
    ${BT_CLI_INC_DIRECTORIES}
)

# library directories
link_directories(
    ${BT_CLI_LINK_DIRECTORIES}
)

# Source Files
set (nd_bt_cli_SRCS
    ${nd_bt_cli_SRCS}
    ${CMAKE_SOURCE_DIR}/src/cli/nd_bt_cli.cpp
)

set(CMAKE_SKIP_BUILD_RPATH FALSE)
set(CMAKE_INSTALL_RPATH ${NDCORESO_PATH})
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

option(RELEASE "Enable release mode" OFF)
message(STATUS "RELEASE is set to: ${RELEASE}")
add_executable (nd_bt_cli ${nd_bt_cli_SRCS})
if(RELEASE)
    set_target_properties(nd_bt_cli PROPERTIES
    LINK_FLAGS "-Wl,-Map=nd_bt_cli.map"
    )
endif()

if (${FOR_TARGET} STREQUAL "BAGHEERA")
    set (BT_CLI_TARGET_LINK_LIBRARIES ${BT_CLI_TARGET_LINK_LIBRARIES} -l:libcrypto.so.1.1 curl dw)
elseif (${FOR_TARGET} STREQUAL "KRAIT")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-rpath-link,${CROSS_COMPLER_BASE}/krait/rootfs/usr/lib64")
    set (BT_CLI_TARGET_LINK_LIBRARIES ${BT_CLI_TARGET_LINK_LIBRARIES} cutils g726 curl -l:libssl.so.1.1 -l:libcrypto.so.1.1 z dw-0.170 elf-0.170 -l:libunwind.so.0.0.0 -l:libzmq.so tinyalsa)
else()
    set (BT_CLI_TARGET_LINK_LIBRARIES ${BT_CLI_TARGET_LINK_LIBRARIES} avutil avcodec zmq ssl crypto curl dw zbar)
endif ()

set (BT_CLI_TARGET_LINK_LIBRARIES ${BT_CLI_TARGET_LINK_LIBRARIES} rt sys iniparser jansson pthread dl crypt
                                  sqlite3 ${CMAKE_THREAD_LIBS_INIT})

target_link_libraries (nd_bt_cli ndcore ${BT_CLI_TARGET_LINK_LIBRARIES})

set_target_properties (nd_bt_cli PROPERTIES LINKER_LANGUAGE CXX)
