include  $(Ndcentral_BASE)/Makefile.ndcentralcommon

ifeq "$(MAKECMDGOALS)" "krait_32"
INPUTS := nd_central.cpp device_mode.cpp metadata_buffer.cpp nd_central_zmq.cpp obd_ndc.cpp
else ifeq "$(MAKECMDGOALS)" "bagheera2"
INPUTS := nd_central.cpp device_mode.cpp metadata_buffer.cpp nd_central_zmq.cpp Base64.cpp obd_ndc.cpp
else ifeq "$(MAKECMDGOALS)" "bagheera3"
INPUTS := nd_central.cpp device_mode.cpp metadata_buffer.cpp nd_central_zmq.cpp Base64.cpp obd_ndc.cpp
else ifeq "$(MAKECMDGOALS)" "x86"
INPUTS := nd_central.cpp device_mode.cpp metadata_buffer.cpp nd_central_zmq.cpp Base64.cpp obd_ndc.cpp
endif

OUTPUTS := $(patsubst %.cpp,$(OUT)/%.o,$(INPUTS))

$(MAKECMDGOALS): all

all: $(OUTPUTS)

$(OUT)/%.o : %.cpp
	$(CXX) -c $(CFLAGS) -o $@ $^  $(addprefix -I, $(C_INCLUDES))

