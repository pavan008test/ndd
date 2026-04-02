include  $(BASE)/Makefile.common

INPUTS := power_monitor.cpp power_monitor_obd.cpp power_monitor_sqlrequests.cpp adc_voltage.cpp
OUTPUTS := $(patsubst %.cpp,$(OUT)/%.o,$(INPUTS))
C_INCLUDES += ../inc


CFLAGS += -DIGNITION_AUDIO_ALERT -DIGNITION_BROADCAST
OUTPUTS += $(ND_PROTO)/audio.pb.o
C_INCLUDES += $(PROTOBUF)/inc $(ND_PROTO)/ # for google protobuf headers

all: $(OUTPUTS)

$(MAKECMDGOALS): all

$(OUT)/%.o : %.cpp
	$(CXX) -O0 -c $(CFLAGS) -o $@ $^ $(addprefix -I, $(C_INCLUDES))

$(ND_PROTO)/%.o:
	DEVICE=1 make -C $(ND_PROTO) device

