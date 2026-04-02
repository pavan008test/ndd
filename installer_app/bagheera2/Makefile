BASE = $(PWD)/../..
include $(BASE)/Makefile.common

CC := g++ -std=c++11
AR := ar
LDFLAGS += -lgstreamer-1.0 -lgobject-2.0 -lglib-2.0
EXE := installer_app

C_INCLUDES+= $(PWD)/inc $(ND_CORE_LIB_ROOT)/nd_thirdparty_lib/bagheera/inc/libutils/inc/gstreamer-1.14.5 /usr/include/glib-2.0 /usr/lib/aarch64-linux-gnu/glib-2.0/include
SRC_DIR := src
OUT_DIR:= out

ifeq "$(MAKECMDGOALS)" "krait_32"
    SRC +=
else ifeq "$(MAKECMDGOALS)" "krait_64"
    SRC +=
else ifeq "$(MAKECMDGOALS)" "bagheera2"
	C_FLAGS = -g -DBAGHEERA2
	SRC +=
else ifeq "$(MAKECMDGOALS)" "x86"
      C_FLAGS = -DBAGHEERA2
      SRC +=
      C_INCLUDES += /usr/lib/x86_64-linux-gnu/glib-2.0/include
else ifeq "$(MAKECMDGOALS)" "bagheera"
    SRC +=
endif

SRC := response_thread.cpp reader_thread.cpp installer_app.cpp
OBJ := $(patsubst %.cpp, %.o, $(SRC) )

SRC := $(patsubst %, $(SRC_DIR)/%, $(SRC) )
OBJ := $(patsubst %, $(OUT_DIR)/%, $(OBJ) )
C_INCLUDES := $(addprefix -I, $(C_INCLUDES))

krait_32: exe
krait_64: exe
bagheera: exe
bagheera2: exe
x86: exe

exe: checkdirs $(OBJ)
	$(CC) $(OBJ) $(LDFLAGS) -o $(OUT_DIR)/$(EXE)

$(OUT_DIR)/%.o: $(SRC_DIR)/%.cpp
	$(CC) -c $< -o $@ $(C_INCLUDES) $(C_FLAGS)

checkdirs: $(OUT_DIR)

$(OUT_DIR):
	@mkdir -p $@

clean:
	rm -rf $(OUT_DIR)
