

include  $(BASE)/Makefile.common

INPUTS := circular_buffer.cpp cirbuf_sqlrequests.cpp enums_to_string.cpp circular_buffer_vid_class.cpp transcode_thread.cpp transcode_sqlrequests.cpp buffer_common_utils.cpp

OUTPUTS := $(patsubst %.cpp,$(OUT)/%.o,$(INPUTS))

CFLAGS += -DTEMP_FIX_CAMERA_OFFSET -DTEMP_FIX_ADJUST_BITRATE -D_FILE_OFFSET_BITS=64 -DCA_CERT_PATH

C_INCLUDES +=  ../inc 

all: $(OUTPUTS)

$(MAKECMDGOALS): all

$(OUT)/%.o : %.cpp
	$(CXX) -O0 -c $(CFLAGS) -o $@ $^ $(addprefix -I, $(C_INCLUDES))
