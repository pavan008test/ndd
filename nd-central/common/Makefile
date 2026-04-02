include  $(Ndcentral_BASE)/Makefile.ndcentralcommon

$(MAKECMDGOALS): all

all: io central genmeta

central: .PHONY $(ND_PROTO)/audio.pb.o
	cd central; make Ndcentral_BASE=$(Ndcentral_BASE) $(MAKECMDGOALS)

io: .PHONY
	cd io; make Ndcentral_BASE=$(Ndcentral_BASE) $(MAKECMDGOALS)

genmeta: .PHONY
	cd genmeta; make Ndcentral_BASE=$(Ndcentral_BASE) $(MAKECMDGOALS)

utils: .PHONY
	cd utils; make Ndcentral_BASE=$(Ndcentral_BASE) $(MAKECMDGOALS)

$(ND_PROTO)/%.o:
	if [ $(MAKECMDGOALS) = "x86" ] ; then \
	DEVICE=1 MACHINE=arm PROTOBUF_BASE=$(PROTOBUF)/rivian make -C $(ND_PROTO) device ; \
	else \
	DEVICE=1 make -C $(ND_PROTO) device  ; fi ;

.PHONY:
