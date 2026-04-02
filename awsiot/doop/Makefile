#This target is to ensure accidental execution of Makefile as a bash script will not execute commands like rm in unexpected directories and exit gracefully.
.prevent_execution:
	exit 0

BASE ?= $(PWD)/../..
OPENSSL_KRAIT_LIB_PATH = $(ND_CORE_LIB_ROOT)/nd_thirdparty_lib/krait/packages/openssl_1.1.1v/arm32/lib
OPENSSL_KRAIT_LIB64_PATH = $(ND_CORE_LIB_ROOT)/nd_thirdparty_lib/krait/packages/openssl_1.1.1v/arm64/lib
OPENSSL_BAGHEERA_PATH = $(ND_CORE_LIB_ROOT)/nd_thirdparty_lib/bagheera/packages/openssl_1.1.1v/lib
include $(BASE)/Makefile.common

# Application source files
DOOP_MAIN_SRC = doop.cpp
DOOP_ADDITIONAL_SRCS = src/config_parser_trim.cpp src/doop_patch.cpp src/nd_log_trim.cpp
DOOP_ALL_SRCS = $(DOOP_MAIN_SRC) $(DOOP_ADDITIONAL_SRCS)
DOOP_BIN = doop

# Aggregate necessary directories
C_INCLUDES += inc
C_INCLUDES := $(addprefix -I, $(C_INCLUDES))

CFLAGS += -O2 -Wall -pthread

bagheera2: LFLAGS = -L$(ND_CORE_LIB_ROOT)/nd_thirdparty_lib/bagheera/lib -liniparser -L$(OPENSSL_BAGHEERA_PATH) -lcrypto
bagheera: LFLAGS = -L$(ND_CORE_LIB_ROOT)/nd_thirdparty_lib/bagheera/lib -liniparser -L$(OPENSSL_BAGHEERA_PATH) -lcrypto
bagheera3: LFLAGS = -L$(ND_CORE_LIB_ROOT)/nd_thirdparty_lib/bagheera/lib -liniparser -L$(OPENSSL_BAGHEERA_PATH) -lcrypto
krait_32: LFLAGS = -L$(ND_CORE_LIB_ROOT)/nd_thirdparty_lib/krait/lib/arm32 -liniparser -L$(OPENSSL_KRAIT_LIB_PATH) -lcrypto
krait_64: LFLAGS = -L$(ND_CORE_LIB_ROOT)/nd_thirdparty_lib/krait/lib/arm64 -liniparser -L$(OPENSSL_KRAIT_LIB64_PATH) -lcrypto
x86: LFLAGS = -L$(ND_CORE_LIB_ROOT)/nd_thirdparty_lib/x86/lib -liniparser -lcrypto

# Rule to build doop binary
$(DOOP_BIN): $(DOOP_ALL_SRCS)
	$(CXX) $(DOOP_ALL_SRCS) $(CFLAGS) $(C_INCLUDES) -o $(DOOP_BIN) $(LDFLAGS)

# Default target
all: $(DOOP_BIN)

# Target-specific builds
bagheera: all
bagheera2: all
bagheera3: all
krait_32: all
krait_64: all
x86: all

clean:
	rm -f $(DOOP_BIN)

.PHONY: all clean bagheera bagheera2 bagheera3 krait_32 krait_64 x86
