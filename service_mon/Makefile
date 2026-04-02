BASE ?= $(PWD)/..
include $(BASE)/Makefile.common

C_INCLUDES += ./inc
EXE := sm
SRC_DIR := src
OUT_DIR:= out

SRC := sm.cpp sm_logger.cpp
OBJ := $(patsubst %.cpp, %.o, $(SRC) )

SRC := $(patsubst %, $(SRC_DIR)/%, $(SRC) )
OBJ := $(patsubst %, $(OUT_DIR)/%, $(OBJ) )
C_INCLUDES := $(addprefix -I, $(C_INCLUDES))


ifeq "$(MAKECMDGOALS)" "krait_32"

else ifeq "$(MAKECMDGOALS)" "krait_64"

else ifeq "$(MAKECMDGOALS)" "bagheera2"

else ifeq "$(MAKECMDGOALS)" "bagheera3"

else ifeq "$(MAKECMDGOALS)" "bagheera"

else ifeq "$(MAKECMDGOALS)" "x86"
endif


exe: checkdirs $(OBJ) 
	$(CXX) $(OBJ) $(LDFLAGS) -o $(OUT_DIR)/$(EXE)

$(OUT_DIR)/%.o: $(SRC_DIR)/%.cpp
	$(CXX) -c $< -o $@ $(C_INCLUDES) $(CFLAGS)

checkdirs: $(OUT_DIR)

$(OUT_DIR):
	@mkdir -p $@


build_tools := msg_reporter

msg_reporter_dir := $(PWD)/cli/msg_reporter

.PHONY: $(build_tools)
$(build_tools):
	@echo ===============================
	@echo Building $@
	@echo ===============================
	cd $($@_dir); $(MAKE) $(MAKECMDGOALS)

krait_32: exe msg_reporter

krait_64: exe msg_reporter

bagheera2 : exe msg_reporter

bagheera3 : exe msg_reporter

x86: exe msg_reporter

clean:
	rm -rf $(OUT_DIR)
	rm -rf $(msg_reporter_dir)/out


