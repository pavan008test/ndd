include $(Ndcentral_BASE)/Makefile.ndcentralcommon

INPUTS := gst_recorder.cpp cam_access.cpp gst_recorder_audio.cpp nd_map.cpp file_helper.cpp
OUTPUTS := $(patsubst %.cpp,$(OUT)/%.o,$(INPUTS))

$(MAKECMDGOALS): all

all: $(OUTPUTS)

$(OUT)/%.o : %.cpp
	$(CXX) -c $(CFLAGS) -o $@ $^ $(addprefix -I, $(C_INCLUDES)) 

