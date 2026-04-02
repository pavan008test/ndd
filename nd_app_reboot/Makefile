
BASE = $(PWD)/..
include $(BASE)/Makefile.common

EXE := nd_app_reboot
SRC := nd_app_reboot.cpp
SRC_DIR := src
OUT_DIR:= out

OBJ := $(patsubst %.cpp, %.o, $(SRC) )

SRC := $(patsubst %, $(SRC_DIR)/%, $(SRC) )
OBJ := $(patsubst %, $(OUT_DIR)/%, $(OBJ) )

C_INCLUDES := $(addprefix -I, $(C_INCLUDES))

exe: checkdirs $(OBJ) 
	$(CXX) $(OBJ) $(LDFLAGS) -o $(OUT_DIR)/$(EXE)

$(OUT_DIR)/%.o: $(SRC_DIR)/%.cpp
	$(CXX) -c $< -o $@ $(C_INCLUDES) $(CFLAGS)


checkdirs: $(OUT_DIR)

$(OUT_DIR):
	@mkdir -p $@

krait_32: exe
krait_64: exe
bagheera2: exe
bagheera3: exe

clean:
	rm -rf $(OUT_DIR)