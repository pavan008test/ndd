BASE = $(PWD)/..
include $(BASE)/Makefile.common

CC := g++ -std=c++11 -Wall -Werror=return-type
#AR := ar
#ND_CORE_LIB_ROOT ?= ../../nd_core_utils
#NDCORESO_PATH ?= /home/ubuntu/.nddevice/latest/

EXE=nd_shutdown

#C_INCLUDES := $(ND_CORE_LIB_ROOT)/cpp/inc $(ND_CORE_LIB_ROOT)/cpp/inc/bagheera2
SRC_DIR := src
OUT_DIR:= out

SRC := nd_shutdown.cpp

ifeq "$(MAKECMDGOALS)" "krait_32"
    SRC += 
else ifeq "$(MAKECMDGOALS)" "krait_64"
    SRC +=
else ifeq "$(MAKECMDGOALS)" "bagheera2"
    SRC +=  
else ifeq "$(MAKECMDGOALS)" "bagheera3"
    SRC +=
else ifeq "$(MAKECMDGOALS)" "x86"
    SRC +=
else ifeq "$(MAKECMDGOALS)" "bagheera"
    SRC += 
endif

OBJ := nd_shutdown.o

SRC := $(patsubst %, $(SRC_DIR)/%, $(SRC) )

OBJ := $(patsubst %, $(OUT_DIR)/%, $(OBJ) )

C_INCLUDES := $(addprefix -I, $(C_INCLUDES))

krait_32: no_exe
krait_64: no_exe
bagheera: no_exe
bagheera2: exe
bagheera3: exe
x86: exe

exe: checkdirs $(OBJ)
	$(CC) $(OBJ) $(LDFLAGS) -o $(OUT_DIR)/$(EXE)

no_exe:
	$(info ************ NOT SUPPORTED FOR THIS TARGET **********)

$(OUT_DIR)/%.o: $(SRC_DIR)/%.cpp
	$(CC) -c $< -o $@ $(C_INCLUDES) $(CFLAGS)

checkdirs: $(OUT_DIR)

$(OUT_DIR):
	@mkdir -p $@

clean:
	rm -rf $(OUT_DIR)


