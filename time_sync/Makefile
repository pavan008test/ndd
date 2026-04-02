# ROOT_DIR = /home/ushas/Krait/Krait_code_crossCompile/krait/armv7ahf-neon-oe-linux-gnueabi
# CC=arm-oe-linux-gnueabi-g++  -march=armv7-a -std=c++11  -mfloat-abi=hard --sysroot=$(ROOT_DIR)/zlib/1.2.11-r0/recipe-sysroot
# AR := arm-oe-linux-gnueabi-ar
# C_FLAGS := -march=armv7-a
# CFLAGS := -D_GLIBCXX_USE_CXX11_ABI=0
BASE ?= $(PWD)/..
include $(BASE)/Makefile.common

EXE := time_sync

C_INCLUDES += inc
OUT_DIR := out
SRC_DIR := src

SRC := time_sync.cpp udid.cpp
OBJ := $(patsubst %.cpp, %.o, $(SRC) )

OBJ := $(patsubst %, $(OUT_DIR)/%, $(OBJ) )
C_INCLUDES := $(addprefix -I, $(C_INCLUDES))

ifeq "$(MAKECMDGOALS)" "krait_32"
    SRC +=
else ifeq "$(MAKECMDGOALS)" "krait_64"
    SRC +=
else ifeq "$(MAKECMDGOALS)" "bagheera2"
    SRC +=
else ifeq "$(MAKECMDGOALS)" "x86"
    SRC +=
else ifeq "$(MAKECMDGOALS)" "bagheera"
    SRC +=
else ifeq "$(MAKECMDGOALS)" "bagheera3"
	SRC +=
endif

krait_32: exe
krait_64: exe
bagheera2: exe
bagheera3: exe
x86: exe

exe: checkdirs $(OBJ)
	$(CXX) $(OBJ) $(LDFLAGS) -o $(OUT_DIR)/$(EXE)

$(OUT_DIR)/%.o: $(SRC_DIR)/%.cpp
	$(CXX) -c $< -o $@ $(C_INCLUDES) $(CFLAGS)

checkdirs: $(OUT_DIR)

$(OUT_DIR):
	@mkdir -p $@

clean:
	rm -rf $(OUT_DIR)

