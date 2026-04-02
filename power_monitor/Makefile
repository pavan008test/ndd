BASE=$(PWD)/..
include $(BASE)/Makefile.common

OUT=$(PWD)/out
TRGTS = folder src
EXEC_NAME=power_monitor
OBJ_LIST= $(wildcard $(OUT)/*.o)
OBJ_LIST += $(ND_PROTO)/audio.pb.o

C_INCLUDES += ./inc

ifeq "$(MAKECMDGOALS)" "krait_32"
    LDFLAGS += -L$(ND_CORE_LIB_ROOT)/nd_thirdparty_lib/lib/krait/arm32 -lzmq
    LDFLAGS += -L$(PROTOBUF)/krait_1/lib/arm32/ -lprotobuf
    PROTO_MAKE_CMD=MACHINE=$(strip $(shell uname -m)) DEVICE=1 KRAIT_64=1 PROTOBUF_BASE=$(PROTOBUF)/krait_1 make -C $(ND_PROTO) device
else ifeq "$(MAKECMDGOALS)" "krait_64"
    LDFLAGS += -L$(ND_CORE_LIB_ROOT)/nd_thirdparty_lib/lib/krait/arm32 -lzmq
    LDFLAGS += -L$(PROTOBUF)/krait_1/lib/arm64/ -lprotobuf
    PROTO_MAKE_CMD=MACHINE=$(strip $(shell uname -m)) DEVICE=1 KRAIT_64=1 PROTOBUF_BASE=$(PROTOBUF)/krait_1 make -C $(ND_PROTO) device
else ifeq "$(MAKECMDGOALS)" "bagheera2"
    LDFLAGS += -lgobject-2.0 -lglib-2.0 -L/usr/lib/aarch64-linux-gnu/ -lgstreamer-1.0
    PROTO_MAKE_CMD = MACHINE=$(strip $(shell uname -m)) DEVICE=1 PROTOBUF_BASE=$(PROTOBUF)/bagheera_1_py3.5 make -C $(ND_PROTO) device
    LDFLAGS += $(PROTOBUF)/bagheera_1_py3.5/lib/libprotobuf.so
else ifeq "$(MAKECMDGOALS)" "bagheera3"
    LDFLAGS += -lgobject-2.0 -lglib-2.0 -L/usr/lib/aarch64-linux-gnu/ -lgstreamer-1.0
    PROTO_MAKE_CMD = MACHINE=$(strip $(shell uname -m)) DEVICE=1 PROTOBUF_BASE=$(PROTOBUF)/bagheera_1_py3.5 make -C $(ND_PROTO) device
    LDFLAGS += $(PROTOBUF)/bagheera_1_py3.5/lib/libprotobuf.so
    C_INCLUDES += /usr/include/glib-2.0 /usr/lib/aarch64-linux-gnu/glib-2.0/include
    C_INCLUDES += $(ND_CORE_LIB_ROOT)/cpp/inc/bagheera3
else ifeq "$(MAKECMDGOALS)" "x86"
    LDFLAGS += -lgobject-2.0 -lglib-2.0 -L/usr/lib/aarch64-linux-gnu/ -lgstreamer-1.0
    PROTO_MAKE_CMD = MACHINE=$(strip $(shell uname -m)) DEVICE=1 PROTOBUF_BASE=$(PROTOBUF)/rivian/inc make -C $(ND_PROTO) device
    LDFLAGS += -L$(PROTOBUF)/rivian/inc -lprotobuf
else ifeq "$(MAKECMDGOALS)" "bagheera"
    LDFLAGS += 
endif

all: .PHONY $(ND_PROTO)/audio.pb.o $(TRGTS)
	$(CXX) $(OBJ_LIST) -o $(OUT)/$(EXEC_NAME) $(LDFLAGS)

folder:
	mkdir -p ./out

src: .PHONY
	make -C src $(MAKECMDGOALS) BASE=$(BASE) OUT=$(PWD)/out
 
$(ND_PROTO)/%.o:
	$(PROTO_MAKE_CMD)

clean:
	rm -rf ./out

krait_32: all 

krait_64: all

bagheera2 : all

bagheera3 : all

x86 : all

.PHONY:
