#/*********************************************************************************
#*********************************************************************************/

cmake_minimum_required (VERSION 3.5)

if (NOT ND_CORE_LIB_ROOT)
    set (ND_CORE_LIB_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../../nd_core_utils)
endif (NOT ND_CORE_LIB_ROOT)

if (${FOR_TARGET} STREQUAL "BAGHEERA")
    set (SAM_LINK_DIRECTORIES ${SAM_LINK_DIRECTORIES}
                          ${ND_CORE_LIB_ROOT}/cpp/out
                          ${ND_CORE_LIB_ROOT}/nd_thirdparty_lib/bagheera/lib
                          ${ND_CORE_LIB_ROOT}/nd_thirdparty_lib/bagheera/packages/openssl_1.1.1v/lib
    )
elseif (${FOR_TARGET} STREQUAL "BAGHEERA2")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DBAGHEERA2 -Wnull-dereference")
    set (SAM_LINK_DIRECTORIES ${SAM_LINK_DIRECTORIES}
                          ${ND_CORE_LIB_ROOT}/cpp/out
                          ${ND_CORE_LIB_ROOT}/nd_thirdparty_lib/bagheera/lib
			  ${ND_CORE_LIB_ROOT}/nd_thirdparty_lib/bagheera/packages/openssl_1.1.1v/lib
    )
    set (SAM_INC_DIRECTORIES ${SAM_INC_DIRECTORIES}
                          ${ND_CORE_LIB_ROOT}/cpp/inc/bagheera2
    )
elseif (${FOR_TARGET} STREQUAL "BAGHEERA3")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DBAGHEERA2 -Wnull-dereference")
    set (SAM_LINK_DIRECTORIES ${SAM_LINK_DIRECTORIES}
                          ${ND_CORE_LIB_ROOT}/cpp/out
                          ${ND_CORE_LIB_ROOT}/nd_thirdparty_lib/bagheera/lib
			  ${ND_CORE_LIB_ROOT}/nd_thirdparty_lib/bagheera/packages/openssl_1.1.1v/lib
    )
    set (SAM_INC_DIRECTORIES ${SAM_INC_DIRECTORIES}
                          ${ND_CORE_LIB_ROOT}/cpp/inc/bagheera3
    )
elseif (${FOR_TARGET} STREQUAL "x86")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DBAGHEERA2 -Wnull-dereference")
    set (SAM_LINK_DIRECTORIES ${SAM_LINK_DIRECTORIES}
                          ${ND_CORE_LIB_ROOT}/cpp/out
                          ${ND_CORE_LIB_ROOT}/nd_thirdparty_lib/x86/lib
    )
    set (SAM_INC_DIRECTORIES ${SAM_INC_DIRECTORIES}
                          ${ND_CORE_LIB_ROOT}/cpp/inc/bagheera2
    )
elseif (${FOR_TARGET} STREQUAL "KRAIT")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DKRAIT -Wnull-dereference")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC -fstack-protector-strong -pie -fPIE -D_FORTIFY_SOURCE=2 -Wa,--noexecstack -Wformat -Wformat-security -Werror=format-security")
    set (CMAKE_INCLUDE_PATH ${CMAKE_INCLUDE_PATH} ${CROSS_COMPLER_BASE}/krait/rootfs/usr/include)
    set (CMAKE_LIBRARY_PATH ${CMAKE_LIBRARY_PATH} ${CROSS_COMPLER_BASE}/krait/rootfs/usr/lib)
    set (SAM_INC_DIRECTORIES
        ${SAM_INC_DIRECTORIES}
        ${CMAKE_SOURCE_DIR}/inc/lib/qcom
        ${CROSS_COMPLER_BASE}/krait/lib32-qmmf-sdk/git-r0/lib32-recipe-sysroot/usr/include
        ${CROSS_COMPLER_BASE}/krait/recipe-sysroot-native/usr/include
        ${CMAKE_SYSROOT}
    )
    set (SAM_LINK_DIRECTORIES ${SAM_LINK_DIRECTORIES}
                            ${ND_CORE_LIB_ROOT}/nd_thirdparty_lib/krait/packages/kvs_sdk/lib
                            ${CROSS_COMPLER_BASE}/krait/rootfs/usr/lib
                            ${ND_CORE_LIB_ROOT}/cpp/out
                            ${ND_CORE_LIB_ROOT}/nd_thirdparty_lib/krait/lib/arm32
                            ${CROSS_COMPLER_BASE}/krait/rootfs/lib
                            ${ND_CORE_LIB_ROOT}/nd_thirdparty_lib/krait/packages/openssl_1.1.1v/arm32/lib
    )
endif ()

set (CMAKE_CXX_STANDARD 14)
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DFIREWALL_ENABLE -DENABLE_ENCRYPTION -DENABLE_DECRYPTION")
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror=return-type")

find_package(Boost COMPONENTS system filesystem REQUIRED)

if (NOT NDCORESO_PATH)
    set (NDCORESO_PATH /home/ubuntu/.nddevice/latest/)
endif (NOT NDCORESO_PATH)

set (SAM_INC_DIRECTORIES
    ${CMAKE_SOURCE_DIR}/inc
    ${ND_CORE_LIB_ROOT}/cpp/inc
    ${ND_CORE_LIB_ROOT}/nd_thirdparty_lib/common/inc
    ${Boost_INCLUDE_DIR}
    ${SAM_INC_DIRECTORIES}
)

# Header Files
include_directories(
    ${SAM_INC_DIRECTORIES}
)

# library directories
link_directories(
    ${SAM_LINK_DIRECTORIES}
)

# Source Files
set (nd_sam_SRCS
    ${CMAKE_SOURCE_DIR}/src/daemon/main.cpp
    ${CMAKE_SOURCE_DIR}/src/daemon/nd_persistence.cpp
    ${CMAKE_SOURCE_DIR}/src/daemon/nd_persistence_helper.cpp
    ${CMAKE_SOURCE_DIR}/src/daemon/nd_sam.cpp
    ${CMAKE_SOURCE_DIR}/src/daemon/nd_security_shadow.cpp
)

set(CMAKE_SKIP_BUILD_RPATH FALSE)
set(CMAKE_INSTALL_RPATH ${NDCORESO_PATH})
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

option(RELEASE "Enable release mode" OFF)
message(STATUS "RELEASE is set to: ${RELEASE}")
add_executable (nd_sam ${nd_sam_SRCS})
if(RELEASE)
    set_target_properties(nd_sam PROPERTIES
    LINK_FLAGS "-Wl,-Map=nd_sam.map"
    )
endif()

if (${FOR_TARGET} STREQUAL "BAGHEERA")
    set (SAM_TARGET_LINK_LIBRARIRES ${SAM_TARGET_LINK_LIBRARIRES} zmq -l:libcrypto.so.1.1 curl avutil avcodec )
elseif (${FOR_TARGET} STREQUAL "BAGHEERA2")
    set (SAM_TARGET_LINK_LIBRARIRES ${SAM_TARGET_LINK_LIBRARIRES} zmq -l:libcrypto.so.1.1 curl avutil avcodec zbar)
elseif (${FOR_TARGET} STREQUAL "BAGHEERA3")
    set (SAM_TARGET_LINK_LIBRARIRES ${SAM_TARGET_LINK_LIBRARIRES} zmq -l:libcrypto.so.1.1 curl avutil avcodec zbar)
elseif (${FOR_TARGET} STREQUAL "KRAIT")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-rpath-link,${ND_CORE_LIB_ROOT}/nd_thirdparty_lib/krait/packages/kvs_sdk/lib")
    set (SAM_TARGET_LINK_LIBRARIRES ${SAM_TARGET_LINK_LIBRARIRES} curl -l:libssl.so.1.1 -l:libcrypto.so.1.1 z ${ND_CORE_LIB_ROOT}/nd_thirdparty_lib/krait/lib/arm32/libzmq.so tinyalsa)
    MESSAGE(STATUS "Using toolchain file: ${CMAKE_TOOLCHAIN_FILE}")
elseif (${FOR_TARGET} STREQUAL "x86")
    set (SAM_TARGET_LINK_LIBRARIRES ${SAM_TARGET_LINK_LIBRARIRES} zmq ssl crypto curl avutil avcodec )
endif ()

set (SAM_TARGET_LINK_LIBRARIRES rt sys iniparser jansson dl crypt sqlite3 dw elf
                                 ${CMAKE_THREAD_LIBS_INIT} Boost::filesystem Boost::system ${SAM_TARGET_LINK_LIBRARIRES} pthread)

target_link_libraries (nd_sam ndcore ${SAM_TARGET_LINK_LIBRARIRES})

set_target_properties (nd_sam PROPERTIES LINKER_LANGUAGE CXX)
