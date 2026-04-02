BASE=$(PWD)/..
Ndcentral_BASE=$(PWD)/../nd-central/device/bagheera2

include $(BASE)/Makefile.common
include $(Ndcentral_BASE)/Makefile.ndcentralcommon

OUT=$(PWD)/out
TRGTS = folder SRC
EXEC_NAME=cam_rec
OBJ_LIST= $(wildcard $(OUT)/*.o)
OBJ_LIST += $(ND_PROTO)/audio.pb.o

C_INCLUDES += inc

x86: all
bagheera2: all
bagheera3: all

all: $(TRGTS)
	echo "================================="
	echo "Building cam_rec with this Libraries and Linking flags"
	echo "================================="
	$(CXX) $(OBJ_LIST) $(LDFLAGS) -o $(OUT)/$(EXEC_NAME)
folder:
	mkdir -p ./out

SRC: audio_proto
	cd src; make $(MAKECMDGOALS) BASE=$(BASE) Ndcentral_BASE=$(Ndcentral_BASE)  OUT=$(OUT)

audio_proto:
	DEVICE=1 make -C $(ND_PROTO) device

clean:
	rm -rf ./out
	rm -f $(ND_PROTO)/*.o  $(ND_PROTO)/*.cc $(ND_PROTO)/*.h

.PHONY:
