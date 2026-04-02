# if you want to add another file, then update the OBJS variable with proper object file name

obj-m	+=	nd_pmic_wdt_vfs.o

all:
	$(Q) $(MAKE) $(MK_FLAGS) $(KERN_DIR)/out M=$(MODULES_DIR)/nd_pmic_wdt_vfs/ modules
	#@echo " [CP]	gpio_ntdi.ko"
	#$(Q) $(CP) gpio_ntdi.ko $(BUILD_DIR)/bin/modules

.PHONY:	all clean

clean:
	$(Q) ( if [ -d $(KERN_DIR)/out ]; then \
	$(MAKE) $(MK_FLAGS) $(KERN_DIR)/out M=$(MODULES_DIR)/nd_pmic_wdt_vfs clean ; fi ; )
	$(Q) $(RM) *.o *.mod.o *.ko
	#$(Q) $(RM) $(BUILD_DIR)/bin/modules/*.ko
