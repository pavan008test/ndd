include  $(Ndcentral_BASE)/Makefile.ndcentralcommon

ifeq "$(MAKECMDGOALS)" "krait_32"
TRGTS = imu gps recorder speaker audio photodiode
else ifeq "$(MAKECMDGOALS)" "bagheera2"
TRGTS = imu gps recorder speaker audio photodiode ublox
else ifeq "$(MAKECMDGOALS)" "bagheera3"
TRGTS = imu gps recorder speaker audio photodiode ublox
else ifeq "$(MAKECMDGOALS)" "x86"
TRGTS = imu gps recorder speaker audio photodiode ublox
endif

$(MAKECMDGOALS): all

all: $(TRGTS)

imu: .PHONY
	cd imu; make  Ndcentral_BASE=$(Ndcentral_BASE) $(MAKECMDGOALS)

gps: .PHONY
	cd gps; make  Ndcentral_BASE=$(Ndcentral_BASE) $(MAKECMDGOALS)

recorder: .PHONY
	cd recorder; make Ndcentral_BASE=$(Ndcentral_BASE) $(MAKECMDGOALS)
	
speaker: .PHONY
	cd speaker; make Ndcentral_BASE=$(Ndcentral_BASE) $(MAKECMDGOALS)

ublox: .PHONY
	cd ublox; make Ndcentral_BASE=$(Ndcentral_BASE) $(MAKECMDGOALS)

audio: .PHONY
	cd audio; make Ndcentral_BASE=$(Ndcentral_BASE) $(MAKECMDGOALS)
	
photodiode: .PHONY
	cd photodiode; make Ndcentral_BASE=$(Ndcentral_BASE) $(MAKECMDGOALS)
.PHONY:

