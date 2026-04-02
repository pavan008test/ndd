
BASE = $(PWD)/..
include $(BASE)/Makefile.common


EXE := circular_buffer


SRC_DIR := src
OUT_DIR:= out

SRC := circular_buffer_vid_class.cpp enums_to_string.cpp cirbuf_sqlrequests.cpp buffer_common_utils.cpp circular_buffer.cpp sdcard_recovery.cpp transcode_sqlrequests.cpp transcode_thread.cpp

ifeq "$(MAKECMDGOALS)" "krait_32"
    SRC += krait_api.cpp

else ifeq "$(MAKECMDGOALS)" "krait_64"

else ifeq "$(MAKECMDGOALS)" "bagheera2"
    SRC += bagheera2_api.cpp

else ifeq "$(MAKECMDGOALS)" "bagheera3"
    SRC += bagheera2_api.cpp

else ifeq "$(MAKECMDGOALS)" "x86"
    SRC += bagheera2_api.cpp

else ifeq "$(MAKECMDGOALS)" "bagheera"

endif

OBJ := $(patsubst %.cpp, %.o, $(SRC) )
SRC := $(patsubst %, $(SRC_DIR)/%, $(SRC) )
OBJ := $(patsubst %, $(OUT_DIR)/%, $(OBJ) )

C_INCLUDES += ./inc
C_INCLUDES := $(addprefix -I, $(C_INCLUDES))
LDFLAGS += -luuid 

exe: checkdirs $(OBJ) 
	$(CXX) $(OBJ) $(LDFLAGS) -o $(OUT_DIR)/$(EXE)

$(OUT_DIR)/%.o: $(SRC_DIR)/%.cpp
	$(CXX) -c $< -o $@ $(C_INCLUDES) $(CFLAGS)

checkdirs: $(OUT_DIR)
$(OUT_DIR):
	@mkdir -p $@

.PHONY: utils

utils:
	$(MAKE) -C ./utils $(MAKECMDGOALS)
	mkdir -p ../build/service/circular_buffer/
	cp ./utils/out/read_xattr ../build/

krait_32: exe utils

krait_64: exe utils

bagheera2: exe utils

bagheera3: exe utils

x86: exe utils

clean:
	rm -rf ./out
	$(MAKE) -C ./utils clean


