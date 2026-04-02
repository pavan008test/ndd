cc := g++

LIB := ndcore

C_INCLUDES := ../inc ../../nd_core_utils/inc
C_INCLUDES := $(addprefix -I, $(C_INCLUDES))

LD_FLAGS := -l$(LIB) -L../../nd_core_utils/out/
C_FLAGS :=

TEST_APP1 := smon_test
TEST_APP1_SRC := smon_test.cpp

all: $(TEST_APP1)

$(TEST_APP1): $(TEST_APP1_SRC)
	g++ $< -o $@ $(C_INCLUDES) $(LD_FLAGS) $(C_FLAGS)

clean:
	rm $(TEST_APP1) -rf

