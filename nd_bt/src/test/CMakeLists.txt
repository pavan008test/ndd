#/*********************************************************************************
#*********************************************************************************/

cmake_minimum_required (VERSION 3.1)
project (nd_bt_test)

if (${FOR_TARGET} STREQUAL "BAGHEERA")
endif (${FOR_TARGET} STREQUAL "BAGHEERA")

if (${FOR_TARGET} STREQUAL "BAGHEERA2")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DBAGHEERA2 -Wnull-dereference")
endif (${FOR_TARGET} STREQUAL "BAGHEERA2")
if (${FOR_TARGET} STREQUAL "BAGHEERA3")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DBAGHEERA2 -Wnull-dereference")
endif (${FOR_TARGET} STREQUAL "BAGHEERA3")

set (CMAKE_CXX_STANDARD 14)
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DFIREWALL_ENABLE -DENABLE_ENCRYPTION -DENABLE_DECRYPTION")
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror=return-type")

if (NOT ND_CORE_LIB_ROOT)
    set (ND_CORE_LIB_ROOT ../../../../nd_core_utils)
endif (NOT ND_CORE_LIB_ROOT)

if (NOT NDCORESO_PATH)
    set (NDCORESO_PATH /home/ubuntu/.nddevice/latest/)
endif (NOT NDCORESO_PATH)

# Header Files
include_directories(
    ${CMAKE_SOURCE_DIR}/inc/lib
    ${CMAKE_SOURCE_DIR}/inc/lib/hci
    ${CMAKE_SOURCE_DIR}/inc/utils
    ${ND_CORE_LIB_ROOT}/cpp/inc
    ${ND_CORE_LIB_ROOT}/nd_thirdparty_lib/common/inc
    ${ND_CORE_LIB_ROOT}/nd_thirdparty_lib/common/inc/gattlib
    ${Boost_INCLUDE_DIR}
)

if (${FOR_TARGET} STREQUAL "BAGHEERA")
    set (BT_MAN_LINK_DIRECTORIES ${ND_CORE_LIB_ROOT}/cpp/bagheera/packages/openssl_1.1.1/lib)
endif (${FOR_TARGET} STREQUAL "BAGHEERA")

set (BT_MAN_LINK_DIRECTORIES ${BT_MAN_LINK_DIRECTORIES}
                             ${ND_CORE_LIB_ROOT}/cpp/out
                             ${ND_CORE_LIB_ROOT}/nd_thirdparty_lib/bagheera/lib
)

# library directories
link_directories(
    ${BT_MAN_LINK_DIRECTORIES}
)

# Source Files
set (nd_bt_test_SRCS
    ${CMAKE_SOURCE_DIR}/src/test/main.cpp
)

set(CMAKE_SKIP_BUILD_RPATH FALSE)
set(CMAKE_INSTALL_RPATH ${NDCORESO_PATH})
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

add_executable (nd_bt_test ${nd_bt_test_SRCS})

set (BT_MAN_TARGET_LINK_LIBRARIRES ${BT_MAN_TARGET_LINK_LIBRARIRES} rt ndcore sys iniparser jansson pthread dl crypt sqlite3 curl gattlib bluetooth)

target_link_libraries (nd_bt_test ${BT_MAN_TARGET_LINK_LIBRARIRES})

set_target_properties (nd_bt_test PROPERTIES LINKER_LANGUAGE CXX)
