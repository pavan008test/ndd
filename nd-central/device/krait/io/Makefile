include  $(BASE)/Makefile.common
include $(Ndcentral_BASE)/Makefile.ndcentralcommon

krait_32 : DEVICE_TRGT = $(MAKECMDGOALS)
krait_64 : DEVICE_TRGT = $(MAKECMDGOALS)
x86 : DEVICE_TRGT = $(MAKECMDGOALS)

TRGTS = imu gps recorder photodiode

all: $(TRGTS)

$(MAKECMDGOALS): all

imu: .PHONY
	cd imu; make BASE=$(BASE) Ndcentral_BASE=$(Ndcentral_BASE) $(DEVICE_TRGT)

gps: .PHONY
	cd gps; make BASE=$(BASE) Ndcentral_BASE=$(Ndcentral_BASE) $(DEVICE_TRGT)

recorder: .PHONY
	cd recorder; make BASE=$(BASE) Ndcentral_BASE=$(Ndcentral_BASE) $(DEVICE_TRGT)

photodiode: .PHONY
	cd photodiode; make BASE=$(BASE) Ndcentral_BASE=$(Ndcentral_BASE) $(DEVICE_TRGT)

.PHONY:

