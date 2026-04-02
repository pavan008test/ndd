CC := g++
AR := ar
C_FLAGS :=
LD_FLAGS := -L./lib/ -L../nd_core_utils/out/ -lndcore
EXE := ut

C_INCLUDES := inc ../nd_core_utils/inc
SRC_DIR := src
OUT_DIR:= out

SRC := ut.cpp
OBJ := $(patsubst %.cpp, %.o, $(SRC) )

SRC := $(patsubst %, $(SRC_DIR)/%, $(SRC) )
OBJ := $(patsubst %, $(OUT_DIR)/%, $(OBJ) )
C_INCLUDES := $(addprefix -I, $(C_INCLUDES))

exe: checkdirs $(OBJ) 
	$(CC) $(OBJ) $(LD_FLAGS) -o $(OUT_DIR)/$(EXE)

$(OUT_DIR)/%.o: $(SRC_DIR)/%.cpp
	$(CC) -c $< -o $@ $(C_INCLUDES) $(CFLAGS)

checkdirs: $(OUT_DIR)

$(OUT_DIR):
	@mkdir -p $@

clean:
	rm -rf $(OUT_DIR)


