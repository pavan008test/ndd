include  $(BASE)/Makefile.common

INPUTS := nd_shutdown.cpp
OUTPUTS := $(patsubst %.cpp,$(OUT)/%.o,$(INPUTS))

all: $(OUTPUTS)

$(OUT)/%.o : %.cpp
	$(CC) -O3 -c $(CFLAGS) -o $@ $^ $(addprefix -I, $(C_INCLUDES))
