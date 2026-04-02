BASE=$(PWD)/..
include $(BASE)/Makefile.common

OUT=$(PWD)/out
TRGTS = folder src
EXEC_NAME=diagnostic
DIAG_OBJ_LIST= diagnostic.o fsck_recovery.o component_base.o sdcard.o sdcard_recovery.o msgq.o  processinfo.o  cpugpuinfo.o diag_resource_info_helper.o waf_utils.o
DIAG_OBJ_LIST_PATH= $(addprefix $(OUT)/, $(DIAG_OBJ_LIST))

LDFLAGS += -L$(ND_CORE_LIB_ROOT)/nd_thirdparty_lib/lib/krait/arm64  -lm -lz

C_INCLUDES := $(addprefix -I, $(C_INCLUDES))

ifeq "$(MAKECMDGOALS)" "krait_32"
    LDFLAGS += -lprocps
    DIR_INCLUDES += ../inc/krait
else ifeq "$(MAKECMDGOALS)" "krait_64"
    LDFLAGS += -lprocps
    DIR_INCLUDES += ../inc/krait
else ifeq "$(MAKECMDGOALS)" "bagheera2"
    LDFLAGS += -L/usr/lib/aarch64-linux-gnu/ -lgstreamer-1.0 -lgobject-2.0 -lglib-2.0
    LDFLAGS += -lprocps
    DIR_INCLUDES += ../inc/bagheera
else ifeq "$(MAKECMDGOALS)" "bagheera3"
    LDFLAGS += -L/usr/lib/aarch64-linux-gnu/ -lgstreamer-1.0 -lgobject-2.0 -lglib-2.0
    LDFLAGS += -lprocps
    DIR_INCLUDES += ../inc/bagheera
else ifeq "$(MAKECMDGOALS)" "x86"
    LDFLAGS += -L/usr/lib/aarch64-linux-gnu/ -lgstreamer-1.0 -lgobject-2.0 -lglib-2.0
    LDFLAGS += -lprocps
    DIR_INCLUDES += ../inc/bagheera
else ifeq "$(MAKECMDGOALS)" "bagheera"
    LDFLAGS += 
endif

all: $(TRGTS)
	$(CXX) -Wl,--start-group $(LDFLAGS) $(DIAG_OBJ_LIST_PATH) -Wl,--end-group -o $(OUT)/$(EXEC_NAME)

folder:
	mkdir -p ./out

src: .PHONY
	cd src; make $(MAKECMDGOALS) BASE=$(BASE) OUT=$(OUT) DIR_INCLUDES=$(DIR_INCLUDES)

krait_32: all 

krait_64: all

bagheera2 : all

bagheera3: all

x86 : all

clean:
	rm -rf ./out
.PHONY:


