BASE=$(PWD)/..
include $(BASE)/Makefile.common

CFLAGS += -DIGNITION_BROADCAST -DTEMP_FIX_CAMERA_OFFSET -DTEMP_FIX_ADJUST_BITRATE

EXE := apm

C_INCLUDES := ./inc $(ND_CORE_LIB_ROOT)/cpp/inc $(ND_CORE_LIB_ROOT)/nd_thirdparty_lib/common/inc
SRC_DIR := src
OUT_DIR:= out

ifeq "$(MAKECMDGOALS)" "krait_32"
SRC := apm_imu.cpp apm_ignition.cpp apm_gps.cpp apm_main.cpp apm_supercap.cpp apm_can.cpp apm_power_volt.cpp apm_worker.cpp apm.cpp
C_INCLUDES += $(ND_CORE_LIB_ROOT)/cpp/inc/krait
else ifeq "$(MAKECMDGOALS)" "krait_64"
SRC := apm_imu.cpp apm_ignition.cpp apm_gps.cpp apm_main.cpp apm_supercap.cpp apm_can.cpp apm_power_volt.cpp apm_worker.cpp apm.cpp
else ifeq "$(MAKECMDGOALS)" "bagheera3"
SRC := apm_imu.cpp apm_ignition.cpp apm_gps.cpp apm_main.cpp apm_supercap.cpp apm_can.cpp apm_power_volt.cpp apm_worker.cpp apm.cpp
C_INCLUDES += $(ND_CORE_LIB_ROOT)/cpp/inc/bagheera3
else ifeq "$(MAKECMDGOALS)" "x86"
SRC := apm_imu.cpp apm_ignition.cpp apm_gps.cpp apm_main.cpp apm_supercap.cpp apm_can.cpp apm_power_volt.cpp apm_worker.cpp apm.cpp
C_INCLUDES += $(ND_CORE_LIB_ROOT)/cpp/inc/krait
else ifeq "$(MAKECMDGOALS)" "bagheera2"
SRC := apm_imu.cpp apm_ignition.cpp apm_gps.cpp apm_main.cpp apm_supercap.cpp apm_can.cpp apm_power_volt.cpp apm_worker.cpp apm.cpp
C_INCLUDES += $(ND_CORE_LIB_ROOT)/cpp/inc/bagheera2
endif

OBJ := $(patsubst %.cpp, %.o, $(SRC) )

SRC := $(patsubst %, $(SRC_DIR)/%, $(SRC) )
OBJ := $(patsubst %, $(OUT_DIR)/%, $(OBJ) )
C_INCLUDES += $(addprefix -I, $(C_INCLUDES))
bagheera3: exe
krait_32: exe
	$(MAKE) -C ./utils $(MAKECMDGOALS)
	mkdir -p ../build/service/apm/
	cp ./utils/out/apm_volt_read ../build/service/apm/
x86: exe
bagheera2: exe

exe: checkdirs $(OBJ) 
	$(CXX) -Wl,--start-group $(OBJ) $(LDFLAGS) -Wl,--start-group -o $(OUT_DIR)/$(EXE)

$(OUT_DIR)/%.o: $(SRC_DIR)/%.cpp
	$(CXX) -c $< -o $@ $(C_INCLUDES) $(CFLAGS)

checkdirs: $(OUT_DIR)

$(OUT_DIR):
	@mkdir -p $@

clean:
	rm -rf $(OUT_DIR)
	$(MAKE) -C ./utils clean
