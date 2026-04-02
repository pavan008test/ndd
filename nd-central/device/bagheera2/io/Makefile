include  $(Ndcentral_BASE)/Makefile.ndcentralcommon

TRGTS = imu gps recorder photodiode ublox

bagheera2 : all
bagheera3 : all

x86 : all

all: $(TRGTS)

imu: .PHONY
	cd imu; make Ndcentral_BASE=$(Ndcentral_BASE) $(MAKECMDGOALS)

gps: .PHONY
	cd gps; make Ndcentral_BASE=$(Ndcentral_BASE) $(MAKECMDGOALS)

recorder: .PHONY
	cd recorder; make Ndcentral_BASE=$(Ndcentral_BASE) $(MAKECMDGOALS)

ublox: .PHONY
	cd ublox; make Ndcentral_BASE=$(Ndcentral_BASE) $(MAKECMDGOALS)

photodiode: .PHONY
	cd photodiode; make Ndcentral_BASE=$(Ndcentral_BASE) $(MAKECMDGOALS)

.PHONY:

