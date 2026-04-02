#This target is to ensure accidental execution of Makefile as a bash script will not execute commands like rm in unexpected directories and exit gracefully.
.prevent_execution:
	exit 0

BASE ?= $(PWD)/..
include $(BASE)/Makefile.common
ND_CORE_UTILS = $(BASE)/../nd_core_utils

APP_DIR = ./nd_iot
APP_INCLUDE_DIRS += -I $(APP_DIR) -I $(APP_DIR)/inc

# Set Python configuration based on target
ifeq ($(MAKECMDGOALS),krait_32)
    # Special configuration for Python binaries when building krait_32
    # (Python binaries need to be 64-bit to match target device libraries)
    CROSS_COMPILER_BASE ?= $(BASE)/../cross_compiler
    PYTHON_CXX := $(CROSS_COMPILER_BASE)/krait/recipe-sysroot-native/usr/bin/aarch64-oe-linux/aarch64-oe-linux-g++
    PYTHON_CFLAGS := --sysroot=$(CROSS_COMPILER_BASE)/krait/recipe-sysroot -march=armv8-a -std=c++11
    PYTHON_INCLUDES := -I$(CROSS_COMPILER_BASE)/krait/recipe-sysroot/usr/include/python3.5m
    PYTHON_LDFLAGS := -lpython3.5m
else
    # Use system Python for bagheera2, bagheera3, x86
    PYTHON_INCLUDES := -I/usr/include/python3.9
    PYTHON_LDFLAGS := -lpython3.9
endif

bagheera2 : IOT_SDK_DIR = $(ND_CORE_UTILS)/nd_thirdparty_lib/bagheera/packages/iot-sdk
bagheera3 : IOT_SDK_DIR = $(ND_CORE_UTILS)/nd_thirdparty_lib/bagheera/packages/iot-sdk
krait_32 : IOT_SDK_DIR = $(ND_CORE_UTILS)/nd_thirdparty_lib/krait/packages/iot-sdk
x86 : IOT_SDK_DIR = $(ND_CORE_UTILS)/nd_thirdparty_lib/x86

APP_NAME = AwsIot
APP_SRC_FILES = $(APP_DIR)/src/$(APP_NAME).cpp $(APP_DIR)/src/AwsIotShadow.cpp $(APP_DIR)/src/aws_iot_parser.cpp  $(APP_DIR)/src/aws_iot_publish.cpp  $(APP_DIR)/src/aws_eld.cpp $(APP_DIR)/src/accessoryhandler.cpp

bagheera2 : IOT_INCLUDE_DIRS += -I $(IOT_SDK_DIR)/include
bagheera3 : IOT_INCLUDE_DIRS += -I $(IOT_SDK_DIR)/include
krait_32 : IOT_INCLUDE_DIRS += -I $(IOT_SDK_DIR)/include
x86 : IOT_INCLUDE_DIRS += -I $(ND_CORE_UTILS)/nd_thirdparty_lib/bagheera/packages/iot-sdk/include

IOT_LIBS = $(IOT_SDK_DIR)/lib

LDFLAGS += -ldl -L$(IOT_SDK_DIR)/lib -laws-crt-cpp -laws-c-common -lIotShadow-cpp 
LDFLAGS += -Wl,-rpath,$(IOT_LIBS) -lstdc++
LDFLAGS += -Wl,--rpath,$(NDCORESO_PATH)
krait_32 : LDFLAGS += -laws-c-http -laws-c-mqtt -laws-c-event-stream -laws-c-s3 -laws-c-auth -laws-c-http -laws-c-io -ls2n -laws-c-compression -laws-c-cal -laws-c-sdkutils -laws-checksums


#Aggregate all include and src directories
INCLUDE_ALL_DIRS += $(IOT_INCLUDE_DIRS)
INCLUDE_ALL_DIRS += $(APP_INCLUDE_DIRS)

C_INCLUDES := $(addprefix -I, $(C_INCLUDES))
INCLUDE_ALL_DIRS += $(C_INCLUDES)

SRC_FILES += $(APP_SRC_FILES)



# Logging level control
LOG_FLAGS += -DENABLE_IOT_DEBUG
LOG_FLAGS += -DENABLE_IOT_INFO
LOG_FLAGS += -DENABLE_IOT_WARN
LOG_FLAGS += -DENABLE_IOT_ERROR

# Add encryption/decryption flags for krait and bagheera targets
ENCRYPTION_FLAGS := -DENABLE_DECRYPTION -DENABLE_ENCRYPTION -DCA_CERT_PATH

COMPILER_FLAGS += $(CFLAGS) -Wno-narrowing $(LOG_FLAGS) $(ENCRYPTION_FLAGS)
#If the processor is big endian uncomment the compiler flag
#COMPILER_FLAGS += -DREVERSED

MAKE_CMD = $(CXX) $(SRC_FILES) $(COMPILER_FLAGS) -o $(APP_NAME) $(LDFLAGS)  $(INCLUDE_ALL_DIRS)

# Python source files to compile into binaries
# Note: For krait_32 target, Python binaries are compiled as 64-bit ARM
#       to match the target device's 64-bit Python libraries in /usr/lib64/
#       For other targets, Python binaries follow the target architecture
PYTHON_SRCS := $(APP_DIR)/AwsIotWrapper.py \
               $(APP_DIR)/registerDevice.py

PYTHON_BINS := $(basename $(PYTHON_SRCS))

# Define doop as a service to build
doop_dir := doop
build_tools := doop

# Rule for building services (similar to main Makefile pattern)
.PHONY: $(build_tools)
$(build_tools):
	@echo ===============================
	@echo Building $@
	@echo ==============================
	cd $($@_dir); $(MAKE) $(MAKECMDGOALS)

# Rule to build Python binaries (special case for krait_32: use 64-bit compiler)
$(APP_DIR)/%: $(APP_DIR)/%.py
	@echo "Creating Python executable $(<:.py=) for target architecture"
	$(Q)$(CYTHON) --embed -o $(<:.py=.c) $<

ifeq ($(MAKECMDGOALS),krait_32)
	@echo "Building Python binary as 64-bit ARM for krait_32 target"
	$(Q)$(PYTHON_CXX) $(PYTHON_CFLAGS) \
		$(PYTHON_INCLUDES) \
		$(C_INCLUDES) \
		-o $@ $(<:.py=.c) \
		$(PYTHON_LDFLAGS)
else
	$(Q)$(CXX) $(CFLAGS) \
		$(PYTHON_INCLUDES) \
		$(C_INCLUDES) \
		-o $@ $(<:.py=.c) \
		$(LDFLAGS) $(PYTHON_LDFLAGS)
endif
	$(Q)rm -f $(<:.py=.c)
	
bagheera : all

bagheera2 : all

bagheera3 : all

krait_32 : all

Krait_64 : all

x86 : all

$(APP_NAME): $(APP_SRC_FILES)
	$(CXX) $(APP_SRC_FILES) $(COMPILER_FLAGS) -o $(APP_NAME) $(LDFLAGS) $(INCLUDE_ALL_DIRS)

all: $(APP_NAME) $(PYTHON_BINS) $(build_tools)
	$(DEBUG)$(MAKE_CMD)
	$(POST_MAKE_CMD)

clean:
	rm -f $(APP_NAME) $(PYTHON_BINS)
	rm -f $(PYTHON_SRCS:.py=.c)
	cd $(doop_dir); $(MAKE) clean
