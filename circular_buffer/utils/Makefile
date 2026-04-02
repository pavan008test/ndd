BASE := $(PWD)/..
include $(BASE)/Makefile.common

exe := read_xattr
src := read_xattr.cpp
src_dir := src
out_dir := out

obj := $(patsubst %.cpp, %.o, $(src))

src := $(patsubst %, $(src_dir)/%, $(src))
obj := $(patsubst %, $(out_dir)/%, $(obj))

C_INCLUDES += ./inc
C_INCLUDES := $(addprefix -I, $(C_INCLUDES))

# Add RUNPATH/RPATH so you don't need to export LD_LIBRARY_PATH each time
LDFLAGS += -Wl,-rpath,/home/ubuntu/.nddevice/latest -Wl,-rpath,/nd_lib

exe: checkdirs $(obj)
	$(CXX) $(obj) $(LDFLAGS) -o $(out_dir)/$(exe)

$(out_dir)/%.o: $(src_dir)/%.cpp
	$(CXX) -c $< -o $@ $(C_INCLUDES) $(CFLAGS)

checkdirs: $(out_dir)

$(out_dir):
	@mkdir -p $@

krait_32: exe
krait_64: exe
bagheera2: exe
bagheera3: exe

clean:
	rm -rf $(out_dir)

