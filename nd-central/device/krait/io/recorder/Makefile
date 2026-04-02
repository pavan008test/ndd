include $(BASE)/Makefile.common
include $(Ndcentral_BASE)/Makefile.ndcentralcommon 

INPUTS := gst_recorder.cpp cam_access.cpp tiny-alsa_recorder.cpp file_helper.cpp
OUTPUTS := $(patsubst %.cpp,$(OUT)/%.o,$(INPUTS))

all: $(OUTPUTS)

krait_32: all

krait_64: all

x86: all

$(OUT)/%.o : %.cpp
	$(CXX) -c $(CFLAGS) -o $@ $^ $(addprefix -I, $(C_INCLUDES)) 

