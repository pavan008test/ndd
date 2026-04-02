BASE = $(PWD)/../..
include $(BASE)/Makefile.common
ND_CORE_LIB_ROOT ?= $(BASE)/nd_core_utils

CC := g++ -std=c++11
AR := ar
LDFLAGS += -lgstreamer-1.0 -lgobject-2.0 -lglib-2.0 -lgstbase-1.0 -lgstapp-1.0 -L$(ND_CORE_LIB_ROOT)/dms_installer_app/build -ldmsinstaller -L$(ND_CORE_LIB_ROOT)/dms_installer_app/ndtensorrt/out -lndtensorrt -L/usr/local/opencv4.1.1/lib/ -lopencv_core -lopencv_imgproc -lopencv_imgcodecs -lopencv_highgui -lopencv_videoio -L/usr/local/cuda-10.2/targets/aarch64-linux/lib -lcudart -lnvinfer -lnvinfer_plugin -lnvonnxparser -lgflags
EXE := installer_app

C_INCLUDES+= $(ND_CORE_LIB_ROOT)/nd_thirdparty_lib/bagheera/inc/libutils/inc/gstreamer-1.14.5 /usr/include/glib-2.0 /usr/lib/aarch64-linux-gnu/glib-2.0/include $(PWD)/inc $(ND_CORE_LIB_ROOT)/cpp/inc/bagheera3 $(ND_CORE_LIB_ROOT)/dms_installer_app/service/include $(BASE)/nd-central/common/central $(BASE)/nd-central/common/inc
SRC_DIR := src
OUT_DIR:= out

ifeq "$(MAKECMDGOALS)" "krait_32"
    SRC +=
else ifeq "$(MAKECMDGOALS)" "krait_64"
    SRC +=
else ifeq "$(MAKECMDGOALS)" "bagheera2"
    C_FLAGS = -DBAGHEERA2
    SRC +=
else ifeq "$(MAKECMDGOALS)" "bagheera3"
    C_FLAGS = -g -DFIREWALL_ENABLE -DBAGHEERA2
    SRC +=
endif

SRC := response_thread.cpp reader_thread.cpp installer_app.cpp
OBJ := $(patsubst %.cpp, %.o, $(SRC) )

SRC := $(patsubst %, $(SRC_DIR)/%, $(SRC) )
OBJ := $(patsubst %, $(OUT_DIR)/%, $(OBJ) )
C_INCLUDES := $(addprefix -I, $(C_INCLUDES))

krait_32: exe
krait_64: exe
bagheera2: exe
bagheera3: exe

exe: checkdirs $(OBJ)
	$(CC) $(OBJ) $(LDFLAGS) -o $(OUT_DIR)/$(EXE)

$(OUT_DIR)/%.o: $(SRC_DIR)/%.cpp
	$(CC) -c $< -o $@ $(C_INCLUDES) $(C_FLAGS)

checkdirs: $(OUT_DIR)

$(OUT_DIR):
	@mkdir -p $@

clean:
	rm -rf $(OUT_DIR)
