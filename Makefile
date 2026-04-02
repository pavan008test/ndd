BASE = $(PWD)
include $(BASE)/Makefile.common

awsiot_dir := awsiot
apm_dir := apm
nd_bt_dir := nd_bt
cam_rec_dir := nd-cam_recorder
circular_buffer_dir := circular_buffer
diagnostic_dir := diagnostic
ext_cam_dir := ext_cam
installer_app_dir := installer_app
nd_sam_dir := nd_sam
nd_shutdown_dir := nd_shutdown
nd_suspendresume_dir := nd_suspendresume
scheduler_manager_dir := scheduler_manager
onetime_service_dir := onetime_service
power_monitor_dir := power_monitor
run_as_root_dir := run_as_root
speed_dir := speed
svc_dir := svc
service_mon_dir := service_mon
time_sync_dir := time_sync
uploader_dir := uploader
update_recovery_dir := update_recovery
wifi_mgr_dir := wifi_mgr
fan_control_dir := fan_control
nd_app_reboot_dir := nd_app_reboot
nd_dta_dir := nd_dta
gps_dir := gps

bagheera2 : bagheera_dir := nd-central/device/bagheera2
bagheera3 : bagheera_dir := nd-central/device/bagheera2
krait_32 : bagheera_dir := nd-central/device/krait
x86: bagheera_dir := nd-central/device/bagheera2

krait_32 : conn_mgr_dir := misc/lte/Connection_Manager/bin/arm
krait_64 : conn_mgr_dir := misc/lte/Connection_Manager/bin/arm64

##Services to build in common
ifeq "$(MAKECMDGOALS)" "bagheera2"
build_services := apm awsiot bagheera cam_rec circular_buffer uploader scheduler_manager ext_cam wifi_mgr installer_app speed svc service_mon time_sync power_monitor diagnostic nd_shutdown nd_suspendresume nd_sam nd_bt nd_dta gps run_as_root
else ifeq "$(MAKECMDGOALS)" "bagheera3"
build_services := awsiot apm bagheera cam_rec circular_buffer uploader scheduler_manager ext_cam wifi_mgr installer_app nd_bt speed svc service_mon time_sync power_monitor diagnostic nd_shutdown nd_suspendresume nd_sam nd_app_reboot nd_dta gps run_as_root
else ifeq "$(MAKECMDGOALS)" "x86"
build_services := awsiot bagheera circular_buffer uploader scheduler_manager ext_cam wifi_mgr installer_app speed svc service_mon time_sync power_monitor diagnostic nd_sam nd_bt cam_rec nd_shutdown nd_suspendresume apm fan_control nd_dta gps
else
build_services := awsiot bagheera circular_buffer uploader scheduler_manager ext_cam wifi_mgr installer_app speed svc service_mon time_sync power_monitor apm fan_control  diagnostic nd_sam nd_bt nd_dta gps run_as_root
endif

clean_services := $(patsubst %,clean_%, $(build_services))

ifneq "$(MAKECMDGOALS)" "x86"
copy_services := $(patsubst %,copy_%, $(build_services))
$(copy_services): copy_%: %
endif

ifdef KRAIT2
       export SUB_PRODUCT := krait2
else
       export SUB_PRODUCT := $(PRODUCT)
endif

RELEASE ?= false

ifeq ($(RELEASE), true)
    CMAKE_ARG=-DRELEASE=ON
    MAPFLAG += -Wl,-Map,$@.map
else
    CMAKE_ARG=-DRELEASE=OFF
endif

.PHONY: all
all : export_base $(build_services) $(copy_services)
	@if [ $(RELEASE) = "true" ]; then \
		find build -type f -exec sh -c ' \
		for src; do \
			if file -b "$$src" | grep -q "ELF"; then \
				dst="stripped_build/$${src#build/}"; \
				mkdir -p "$$(dirname "$$dst")"; \
				$(STRIP_TOOL) --strip-unneeded -o "$$dst" "$$src"; \
			fi; \
		done' _ {} + ; cp -a -n build/. stripped_build/; \
	fi

krait_32: all
krait_64: all
bagheera3: all
bagheera2: all
bagheera: all
x86: all

export_base:
	$(eval export BASE=$(BASE))

.PHONY : $(build_services)
$(build_services) :
	  	@echo ===============================
		@echo Building $@
		@echo ==============================
		if [ $@ = "nd_bt" ]; then \
			rm -rf $($@_dir)/build && mkdir $($@_dir)/build && cd $($@_dir)/build; \
			if [ $(MAKECMDGOALS) = "bagheera" ]; then \
				cmake .. -DFOR_TARGET=BAGHEERA $(CMAKE_ARG) && $(MAKE); \
			elif [ $(MAKECMDGOALS) = "bagheera2" ]; then \
				cmake .. -DFOR_TARGET=BAGHEERA2 $(CMAKE_ARG) && $(MAKE); \
			elif [ $(MAKECMDGOALS) = "bagheera3" ]; then \
                                cmake .. -DFOR_TARGET=BAGHEERA2 $(CMAKE_ARG) && $(MAKE); \
			elif [ $(MAKECMDGOALS) = "krait_32" ] || [ $(MAKECMDGOALS) = "krait_64" ]; then \
				cmake .. -DFOR_TARGET=KRAIT $(CMAKE_ARG) -DCMAKE_TOOLCHAIN_FILE=../krait64_toolchain.cmake && $(MAKE); \
			elif [ $(MAKECMDGOALS) = "x86" ]; then \
				cmake .. -DFOR_TARGET=x86 && $(MAKE); \
			fi; \
		elif [ $@ = "nd_sam" ]; then \
			rm -rf $($@_dir)/build && mkdir $($@_dir)/build && cd $($@_dir)/build; \
			if [ $(MAKECMDGOALS) = "bagheera" ]; then \
				cmake .. -DFOR_TARGET=BAGHEERA $(CMAKE_ARG) && $(MAKE); \
			elif [ $(MAKECMDGOALS) = "bagheera2" ]; then \
				cmake .. -DFOR_TARGET=BAGHEERA2 $(CMAKE_ARG) && $(MAKE); \
			elif [ $(MAKECMDGOALS) = "bagheera3" ]; then \
                                cmake .. -DFOR_TARGET=BAGHEERA2 $(CMAKE_ARG) && $(MAKE); \
			elif [ $(MAKECMDGOALS) = "krait_32" ]; then \
				cmake .. -DFOR_TARGET=KRAIT $(CMAKE_ARG) -DCMAKE_TOOLCHAIN_FILE=../krait32_toolchain.cmake && $(MAKE); \
			elif [ $(MAKECMDGOALS) = "x86" ]; then \
                                cmake .. -DFOR_TARGET=x86 && $(MAKE); \
			fi; \
		elif [ $@ = "installer_app" ]; then \
		cd $($@_dir)/$(PRODUCT); $(MAKE) $(MAKECMDGOALS) EXTRA_LDFLAGS="$(MAPFLAG)"; \
		elif [ $@ = "run_as_root" ] && [ $(MAKECMDGOALS) = "krait_32" ]; then \
		cd $($@_dir)/krait; $(MAKE) $(MAKECMDGOALS) EXTRA_LDFLAGS="$(MAPFLAG)"; \
		elif [ $@ = "run_as_root" ] && [ $(MAKECMDGOALS) = "bagheera2" ]; then \
		cd $($@_dir)/$(PRODUCT); $(MAKE) $(MAKECMDGOALS) EXTRA_LDFLAGS="$(MAPFLAG)"; \
		elif [ $@ = "run_as_root" ] && [ $(MAKECMDGOALS) = "bagheera3" ]; then \
		cd $($@_dir)/$(PRODUCT); $(MAKE) $(MAKECMDGOALS) EXTRA_LDFLAGS="$(MAPFLAG)"; \
		else \
		cd $($@_dir); $(MAKE) $(MAKECMDGOALS) EXTRA_LDFLAGS="$(MAPFLAG)"; \
		fi ;
		mkdir -p build/service/$@ ; \

$(copy_services):
		@echo =================
		@echo copying $(subst copy_,,$@) service
		@echo =================
		if [ $(subst copy_,,$@) = "awsiot" ]; then \
			cp $(awsiot_dir)/AwsIot build/service/awsiot/; \
			cp $(awsiot_dir)/doop/doop build/; \
			cp $(awsiot_dir)/nd_iot/AwsIotWrapper build/service/awsiot/; \
			cp $(awsiot_dir)/nd_iot/registerDevice build/service/awsiot/; \
			cp $(awsiot_dir)/nd_iot/aws_iot_utils.py build/service/awsiot/; \
			cp scripts/$(SUB_PRODUCT)/awsiot* build/service/awsiot/; \
		elif [ $(subst copy_,,$@) = "nd_bt" ]; then \
			cp -rf $(nd_bt_dir)/build/src/daemon/nd_bt_man build/service/nd_bt/ ; \
			cp -rf $(nd_bt_dir)/build/src/cli/nd_bt_cli build/service/nd_bt/ ; \
			cp -rf $(nd_bt_dir)/build/src/lib/libndbt.so build/service/nd_bt/ ; \
			cp scripts/$(SUB_PRODUCT)/nd_bt* build/service/nd_bt/ ; \
		elif [ $(subst copy_,,$@) = "installer_app" ]; then \
			cp -rf $(installer_app_dir)/$(PRODUCT)/out/installer_app $(BASE)/build/service/installer_app/; \
			cp scripts/$(SUB_PRODUCT)/installer_app* $(BASE)/build/service/installer_app/; \
		elif [ $(subst copy_,,$@) = "run_as_root" ]; then \
			cp -rf $(run_as_root_dir)/$(PRODUCT)/out/* $(BASE)/build/service/run_as_root/; \
		elif [ $(subst copy_,,$@) = "service_mon" ]; then \
			cp $(service_mon_dir)/out/sm build/service/service_mon/; \
			cp scripts/$(SUB_PRODUCT)/service_mon.* build/service/service_mon/ ; \
		elif [ $(subst copy_,,$@) = "nd_sam" ]; then \
			cp -rf $(nd_sam_dir)/build/src/daemon/nd_sam build/service/nd_sam/ ; \
			cp -rf $(nd_sam_dir)/build/src/console/nd_sam_cli build/service/nd_sam/ ; \
			cp scripts/$(SUB_PRODUCT)/nd_sam.* build/service/nd_sam/ ; \
		elif [ $(subst copy_,,$@) = "wifi_mgr" ]; then \
			cp -rf $(wifi_mgr_dir)/out/wifi_mgr build/service/wifi_mgr/ ; \
			cp scripts/$(SUB_PRODUCT)/wifi_mgr* build/service/wifi_mgr/ ; \
		else \
			cp -rf $($(subst copy_,,$@)_dir)/out/$(subst copy_,,$@) build/service/$(subst copy_,,$@)/ ; \
			cp scripts/$(SUB_PRODUCT)/$(subst copy_,,$@).* build/service/$(subst copy_,,$@) ; \
		fi ;

clean : clean_artifacts $(clean_services)

clean_artifacts:
		rm -rf ./build

$(clean_services) :
		@echo ================
		@echo Cleaning $@
		@echo ================
		if [ $@ = "clean_run_as_root" ]; then \
		cd run_as_root/bagheera2 ; $(MAKE) clean; cd - ;\
                cd run_as_root/krait ; $(MAKE) clean; cd - ;\
                elif [ $@ = "clean_installer_app" ]; then \
                cd installer_app/bagheera2 ; $(MAKE) clean; cd - ;\
                cd installer_app/krait ; $(MAKE) clean; cd - ;\
                elif [ $@ = "clean_bagheera" ]; then \
                cd nd-central/device/bagheera2 ; $(MAKE) clean; cd - ;\
                cd nd-central/device/krait ; $(MAKE) clean; cd - ;\
		elif [ $@ = "clean_nd_sam" ] || [ $@ = "clean_nd_bt" ]; then \
		cd $($(subst clean_,,$@)_dir) ; rm -rf ./build ;\
                else \
                cd $($(subst clean_,,$@)_dir) ; $(MAKE) clean ;\
                fi;
