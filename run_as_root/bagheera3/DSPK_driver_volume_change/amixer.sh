#! /bin/sh

sleep 5

# Default volume level variable
VOLUME_LEVEL=12500

#Check which speaker is connected
err=$(amixer -c 1 cget name="x tas2563-digital-volume" 2>&1 >/dev/null)
# If there is no error, set SPEAKER to "tas2563"
if [ -z "$err" ]; then
        SPEAKER="tas2563"
else
        # Run the second amixer command and capture any error
        err=$(amixer -c tegrasndt186ref cget name="x Speaker Driver Volume" 2>&1 >/dev/null)
        # If there is no error, set SPEAKER to "tas2505"
        if [ -z "$err" ]; then
                SPEAKER="tas2505"
        else
                echo "No compatible speaker found"
                exit 1
        fi
fi

echo "Connected speaker: $SPEAKER"
# Run the appropriate set of commands based on the speaker type

if [ "$SPEAKER" = "tas2563" ]; then
        #Audio: Enabling DMIC Routing Path for tas2563
        echo "Enabling DMIC Routing Path for tas2563..."
        amixer -c tegrasndt186ref sset 'MVC2 Mux' 'DMIC1'
        amixer -c tegrasndt186ref sset 'ADMAIF2 Mux' 'MVC2'
        amixer -c tegrasndt186ref cset name="DMIC1 Boost Gain" 50
        amixer -c tegrasndt186ref cset name="MVC2 Vol" 14500
        amixer -c tegrasndt186ref cset name="MVC2 input bit format" 32
        amixer -c tegrasndt186ref cset name="DMIC1 output bit format" 32
        #Audio: Enabling DSPK Routing Path for tas2563
        echo "Enabling DSPK Routing Path for tas2563..."
        amixer -c tegrasndt186ref sset 'I2S1 Mux' 'ADMAIF1'
        amixer -c 1 cset name="x tas2563-amp-gain-volume" 16
        amixer -c 1 cset name="x tas2563-digital-volume" 16384
        amixer -c 1 cset name="x TASDEVICE Profile id" 3
        amixer -c 1 sset 'I2S1 Mux' 'MVC1'
        amixer -c 1 sset 'MVC1 Mux' 'ADMAIF1'
        amixer -c 1 cset name="MVC1 Vol" $VOLUME_LEVEL

elif [ "$SPEAKER" = "tas2505" ]; then
        #Audio: Enabling DMIC Routing Path for tas2505
        echo "Enabling DMIC Routing Path for tas2505..."
        amixer -c tegrasndt186ref cset name="MVC1 Mux" DMIC1
        amixer -c tegrasndt186ref cset name="ADMAIF2 Mux" MVC1
        amixer -c tegrasndt186ref cset name="DMIC1 Boost Gain" 50
        amixer -c tegrasndt186ref cset name="MVC1 Vol" 14500
        amixer -c tegrasndt186ref cset name="MVC1 input bit format" 32
        amixer -c tegrasndt186ref cset name="DMIC1 output bit format" 32
        #Audio: Enabling DSPK Routing Path for older speaker
        echo "Enabling DSPK Routing Path for older speaker..."
        amixer -c tegrasndt186ref cset name="I2S1 Mux" ADMAIF1
        amixer -c tegrasndt186ref cset name="x Speaker Amplifer Volume" 0
        amixer -c tegrasndt186ref cset name="x Speaker Amplifer Volume" 4
        amixer -c tegrasndt186ref cset name="x Speaker Driver Volume" 105
fi

