include  $(Ndcentral_BASE)/Makefile.ndcentralcommon

INPUTS := genmeta.cpp
OUTPUTS := $(patsubst %.cpp,$(OUT)/%.o,$(INPUTS))

$(MAKECMDGOALS): all

all: $(OUTPUTS)

$(OUT)/%.o : %.cpp
	$(CXX) -O0 -c $(CFLAGS) -o $@ $^ $(addprefix -I, $(C_INCLUDES))

