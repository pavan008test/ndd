include  $(Ndcentral_BASE)/Makefile.ndcentralcommon

INPUTS := gps_dev.cpp
OUTPUTS := $(patsubst %.cpp,$(OUT)/%.o,$(INPUTS))

$(MAKECMDGOALS): all

all: $(OUTPUTS)

$(OUT)/%.o : %.cpp
	$(CXX) -c $(CFLAGS) -o $@ $^  $(addprefix -I, $(C_INCLUDES))

