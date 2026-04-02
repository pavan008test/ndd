BASE = $(PWD)/..
include $(BASE)/Makefile.common

EXE := uploader

SRC_DIR := src
OUT_DIR:= out
SRC := uploader.cpp uploader_db.cpp ea_db.cpp ffmpeg_utils.cpp uploader_cb_utils.cpp health_stats_utils.cpp rental_utils.cpp vod_health.cpp ea_images_utils.cpp

#LDFLAGS += -lopencv_core -lopencv_highgui -lopencv_imgproc -lopencv_imgcodecs -lopencv_videoio

ENABLE_UPLOADER_TEST = OFF
ifeq ($(ENABLE_UPLOADER_TEST), ON)
CFLAGS += -DUSE_UPLOADER_TEST
endif

ifeq "$(MAKECMDGOALS)" "krait_32"
    SRC +=
    LDFLAGS += -Wl,-Bstatic -lZipper-static -Wl,-Bdynamic -lz -lopencv_core -lopencv_highgui -lopencv_imgproc -lopencv_imgcodecs -lopencv_videoio 
else ifeq "$(MAKECMDGOALS)" "krait_64"
    SRC +=
    LDFLAGS += -Wl,-Bstatic -lZipper-static -Wl,-Bdynamic -lz -lcurl -ldl -lsys
else ifeq "$(MAKECMDGOALS)" "bagheera2"
    SRC +=  
    LDFLAGS += -lZipper -L/usr/local/opencv4.1.1/lib -lopencv_core -lopencv_imgproc -lopencv_imgcodecs

else ifeq "$(MAKECMDGOALS)" "x86"
    SRC +=
    LDFLAGS += -lZipper -L/usr/local/opencv4.1.1/lib -lopencv_core -lopencv_imgproc -lopencv_imgcodecs -lz

else ifeq "$(MAKECMDGOALS)" "bagheera"
    LDFLAGS += -lZipper
else ifeq "$(MAKECMDGOALS)" "bagheera3"
    SRC +=
    LDFLAGS += -lZipper -L/usr/local/opencv4.1.1/lib/ -lopencv_core -lopencv_imgproc -lopencv_imgcodecs
endif

OBJ := $(patsubst %.cpp, %.o, $(SRC) )
SRC := $(patsubst %, $(SRC_DIR)/%, $(SRC) )
OBJ := $(patsubst %, $(OUT_DIR)/%, $(OBJ) )
SERVICE_UITLS_OBJ := ../service_utils/out/utils.o

CFLAGS += -DENABLE_DECRYPTION -DENABLE_ENCRYPTION -DCA_CERT_PATH
C_INCLUDES += inc $(ND_CORE_LIB_ROOT)/nd_thirdparty_lib/common/lib_src/zipper/zipper ../awsiot/linux_mqtt_mbedtls-2.1.1/samples/linux/bagheera_iot/inc/jansson $(BASE)/service_utils/inc
C_INCLUDES := $(addprefix -I, $(C_INCLUDES))

krait_32: exe
krait_64: exe
bagheera: exe
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

