include  $(BASE)/Makefile.common

INPUTS := 
OUTPUTS := $(patsubst %.cpp,$(OUT)/%.o,$(INPUTS))

all: $(OUTPUTS)

$(OUT)/%.o : %.cpp
	$(CC) -c $(CFLAGS)-o $@ $^  $(addprefix -I, $(C_INCLUDES))

