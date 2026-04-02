include $(Ndcentral_BASE)/Makefile.ndcentralcommon

INPUTS := cam_recorder.cpp cam_access.cpp file_helper.cpp
OUTPUTS := $(patsubst %.cpp,$(OUT)/%.o,$(INPUTS))
C_INCLUDES += ../inc

all: $(OUTPUTS)

$(MAKECMDGOALS): all

$(OUT)/%.o : %.cpp
	$(CXX) -O0 -c $(CFLAGS) -o $@ $^ $(addprefix -I, $(C_INCLUDES))
