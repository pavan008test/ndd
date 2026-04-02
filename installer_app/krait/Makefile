BASE ?= $(PWD)/../..
include $(BASE)/Makefile.common
#COMPILER_BASE=$(BASE)/../../cross_compiler

CFLAGS += -DFIREWALL_ENABLE
C_INCLUDES += ./inc

krait_32 : C_INCLUDES += -I$(ND_CORE_LIB_ROOT)/cpp/inc -I$(ND_CORE_LIB_ROOT)/nd_thirdparty_lib/inc
krait_32 : C_INCLUDES += -I$(PWD)/../inc

krait_32 : LDFLAGS += -L$(COMPILER_BASE)/krait/lib32-qmmf-sdk/git-r0/lib32-recipe-sysroot/usr/lib/ -ldl
krait_32 : LDFLAGS += -L$(ND_CORE_LIB_ROOT)/cpp/out -lndcore
krait_32 : LDFLAGS += -L$(ND_CORE_LIB_ROOT)/nd_thirdparty_lib/lib/krait/arm32 -liniparser
krait_32 : LDFLAGS += -L$(COMPILER_BASE)/krait/rootfs/usr/lib -lhttp_interface -lBasicUsageEnvironment -lgroupsock -lliveMedia -lUsageEnvironment -lmux_interface -ljson_apis -lFileMux -lvam_interface -lqmmf_database -lqmmf_memory_interface -lVAManager -llive555rtsp_in
krait_32 : LDFLAGS += -L$(COMPILER_BASE)/krait/lib32-qmmf-sdk/git-r0/package/usr/lib/ -lqmmf_recorder_client -lqmmf_display_client -lqmmf_av_queue -lqmmf_camera_adaptor -lqmmf_utils

EXE := installer_app

SRC_DIR := src
OUT_DIR := out

SRC := response_thread.cpp reader_thread.cpp installer_app.cpp qmmf-rtsp.cpp
OBJ := $(patsubst %.cpp, %.o, $(SRC) )

SRC := $(patsubst %, $(SRC_DIR)/%, $(SRC) )
OBJ := $(patsubst %, $(OUT_DIR)/%, $(OBJ) )
C_INCLUDES := $(addprefix -I, $(C_INCLUDES))

exe: checkdirs $(OBJ) 
	$(CXX) -Wl,--start-group $(LDFLAGS) $(OBJ) -Wl,--end-group -o $(OUT_DIR)/$(EXE)

$(OUT_DIR)/%.o: $(SRC_DIR)/%.cpp
	$(CXX) -c $< -o $@ $(C_INCLUDES) $(CFLAGS)

checkdirs: $(OUT_DIR)

$(OUT_DIR):
	@mkdir -p $@

krait_32: exe 

krait_64: exe 

clean:
	rm -rf $(OUT_DIR)
