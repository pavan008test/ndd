BASE=$(PWD)/../../..
Ndcentral_BASE=$(PWD)

include $(BASE)/Makefile.common
include $(Ndcentral_BASE)/Makefile.ndcentralcommon

TRGTS = folder IO common #dev_utils
EXEC_NAME=bagheera
OBJ_LIST= $(wildcard $(OUT)/*.o)
OBJ_LIST += $(ND_PROTO)/audio.pb.o

bagheera2: all
bagheera3: all
x86: all

all: $(TRGTS)
	echo "================================="
	echo "Building Ndcentral with this Libraries and Linking flags"
	echo "================================="
	$(CXX) $(OBJ_LIST) $(LDFLAGS) -o $(OUT)/$(EXEC_NAME)

folder:
	mkdir -p ./out

IO: .PHONY
	cd io; make BASE=$(BASE) Ndcentral_BASE=$(Ndcentral_BASE) $(MAKECMDGOALS)

#dev_utils:
#	cd utils; make BASE=$(BASE) Ndcentral_BASE=$(Ndcentral_BASE) $(MAKECMDGOALS)

common: .PHONY
	cd ../../common; make BASE=$(BASE) Ndcentral_BASE=$(Ndcentral_BASE) $(MAKECMDGOALS)
 
clean:
	rm -rf ./out
	rm -rf $(BASE)/nd_proto/audio.pb.*

.PHONY:

