#!/bin/bash
sleep 1
rmmod brcmfmac.ko brcmutil.ko cfg80211.ko compat.ko
sleep 1 
insmod /lib/modules/misc/compat.ko
insmod  /lib/modules/misc/cfg80211.ko
insmod  /lib/modules/misc/brcmutil.ko
insmod  /lib/modules/misc/brcmfmac.ko debug=0x100000
# Audio: Enabling DMIC Routing Path
amixer -c tegrasndt186ref cset name="MVC1 Mux" DMIC3
amixer -c tegrasndt186ref cset name="ADMAIF1 Mux" MVC1
amixer -c tegrasndt186ref cset name="DMIC3 Boost Gain" 50
amixer -c tegrasndt186ref cset name="MVC1 Vol" 14500
amixer -c tegrasndt186ref cset name="MVC1 input bit format" 32
amixer -c tegrasndt186ref cset name="DMIC3 output bit format" 32
# Audio: Enabling DSPK Routing Path
amixer -c tegrasndt186ref cset name="DSPK2 Mux" ADMAIF1
amixer -c tegrasndt186ref cset name="MVC2 Mux" ADMAIF1
amixer -c tegrasndt186ref cset name="DPSK2 Mux" MVC2
amixer -c tegrasndt186ref cset name="MVC2 Vol" 11500
