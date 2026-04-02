
include  $(BASE)/Makefile.common
INPUTS := diagnostic.cpp fsck_recovery.cpp component_base.cpp sdcard.cpp sdcard_recovery.cpp msgq.cpp processinfo.cpp cpugpuinfo.cpp diag_resource_info_helper.cpp waf_utils.cpp
OUTPUTS := $(patsubst %.cpp,$(OUT)/%.o,$(INPUTS))
C_INCLUDES += ../inc
C_INCLUDES += $(ND_CORE_LIB_ROOT)/nd_thirdparty_lib/tool
C_INCLUDES += $(DIR_INCLUDES)
LS_FLAGS := -lemmchs.so
CFLAGS += -DNO_SDCARD
all: $(OUTPUTS)

$(MAKECMDGOALS): all

$(OUT)/%.o : %.cpp
	$(CXX) -O0 -c $(CFLAGS) -o $@ $^ $(addprefix -I, $(C_INCLUDES))
