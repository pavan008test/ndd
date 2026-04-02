
BASE = $(PWD)/..
include $(BASE)/Makefile.common

EXE := scheduler_manager

SRC_DIR := src
OUT_DIR:= out
SRC := scheduler_manager.cpp

ifeq "$(MAKECMDGOALS)" "krait_32"
    SRC += 
else ifeq "$(MAKECMDGOALS)" "krait_64"
    SRC +=
else ifeq "$(MAKECMDGOALS)" "bagheera2"
    SRC += 
else ifeq "$(MAKECMDGOALS)" "bagheera"
    SRC +=
else ifeq "$(MAKECMDGOALS)" "bagheera3"
    SRC +=
else ifeq "$(MAKECMDGOALS)" "x86"
    SRC +=
endif

OBJ := $(patsubst %.cpp, %.o, $(SRC) )
SRC := $(patsubst %, $(SRC_DIR)/%, $(SRC) )
OBJ := $(patsubst %, $(OUT_DIR)/%, $(OBJ) )

CFLAGS += -Wall -Werror=return-type
C_INCLUDES := $(addprefix -I, $(C_INCLUDES))

krait_32: exe
krait_64: exe
bagheera: exe
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
