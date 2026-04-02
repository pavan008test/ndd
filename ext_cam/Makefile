BASE ?= $(PWD)/..
include $(BASE)/Makefile.common

EXE := ext_cam

C_INCLUDES += inc $(BASE)/service_utils/inc
OUT_DIR := out
SRC_DIR := src

SRC := ext_cam_recorder.cpp
OBJ := $(patsubst %.cpp, %.o, $(SRC) )

OBJ := $(patsubst %, $(OUT_DIR)/%, $(OBJ) )
SERVICE_UITLS_OBJ := ../service_utils/out/utils.o
C_INCLUDES := $(addprefix -I, $(C_INCLUDES))

ifeq "$(MAKECMDGOALS)" "krait_32"
    LDFLAGS += -lopencv_core -lopencv_imgproc -lopencv_imgcodecs
else ifeq "$(MAKECMDGOALS)" "krait_64"
    SRC +=
else ifeq "$(MAKECMDGOALS)" "bagheera2"
    LDFLAGS += -L/usr/local/opencv4.1.1/lib/ -lopencv_core -lopencv_imgproc -lopencv_imgcodecs

else ifeq "$(MAKECMDGOALS)" "x86"
    LDFLAGS += -L/usr/local/opencv4.1.1/lib/ -lopencv_core -lopencv_imgproc -lopencv_imgcodecs

else ifeq "$(MAKECMDGOALS)" "bagheera"
    SRC +=
else ifeq "$(MAKECMDGOALS)" "bagheera3"
    LDFLAGS += -L/usr/local/opencv4.1.1/lib/ -lopencv_core -lopencv_imgproc -lopencv_imgcodecs
endif

krait_32: exe
krait_64: exe
bagheera2: exe
bagheera3: exe
x86: exe

exe: UTILS_OBJ checkdirs $(OBJ)
	$(CXX) $(OBJ) $(SERVICE_UITLS_OBJ) $(LDFLAGS) -o $(OUT_DIR)/$(EXE)

UTILS_OBJ:
	cd $(BASE)/service_utils ; $(MAKE) $(MAKECMDGOALS) ; cd -

$(OUT_DIR)/%.o: $(SRC_DIR)/%.cpp
	$(CXX) -c $< -o $@ $(C_INCLUDES) $(CFLAGS)

checkdirs: $(OUT_DIR)

$(OUT_DIR):
	@mkdir -p $@

clean:
	rm -rf $(OUT_DIR)
	rm -rf $(BASE)/service_utils/out

