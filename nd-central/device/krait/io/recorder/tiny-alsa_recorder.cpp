#include <stdio.h>
#include <stdlib.h>

#include <tinyalsa/pcm.h>

#include <tinyalsa/asoundlib.h>
#include <errno.h>
#include <getopt.h>
#include <ctype.h>
#include <string.h>
#include <limits.h>
#include <deque>
#include <cstdint>
#include <time.h>
#include <sys/time.h>
#include <log.h>
#include "audio_record.h"
#include "tiny-alsa_mixer.h"
#include "nd_time.h"
#include "nd_file_utils.h"
#include "nd_factory.h"


#define TAG "AUD"
#define FORMAT_PCM 1
#define AUDIO_SAMPLING_RATE 16000
using namespace std;

static int capturing = 1;
int prinfo = 1;
static Audio::audio_callback_t *audio_cb=NULL;
static Audio::audio_err_callback_t *audio_error_cb=NULL;

#define nd_device_obj (ND_DeviceFactory::Create_NDDevice())

bool audio_encode_dev(string input, string output) {

    int64_t time_before = 0, time_after = 0;
    std::string sampling_rate = std::to_string(AUDIO_SAMPLING_RATE);
    string audio_encode_cmd = "ffmpeg -f s16le -ar " + sampling_rate + " -ac 1 -i "+ input +" -c:a aac -strict -2 -vn -ar " + sampling_rate + " -ac 1 -b:a 64k -v warning -y " + output;

    LOG_I(TAG, "Audio encode cmd: %s", audio_encode_cmd.c_str());
    time_before = get_system_time();
    int res = system(audio_encode_cmd.c_str());
    time_after = get_system_time();
    LOG_I (TAG, "Audio encode took %lld ms", (time_after - time_before));

    LOG_I(TAG, "Audio pcm size = %d, aac size = %d, returned = %d", file_size(input), file_size(output), res);
        return true;

}

bool audio_dev_reg_cb( Audio::audio_callback_t *cb, Audio::audio_err_callback_t *err_cb ) {

    if( cb == NULL ) {
        LOG_E(TAG, "cb to be registered is NULL");
        return false;
    }

    audio_cb = cb;
    audio_error_cb = err_cb;

    return true;
}

static void *capture_audio(void *ptr)
{
    LOG_I(TAG, "Started audio capture thread");

    unsigned int card = 0;
    bool init_mixer_status = nd_device_obj->mic_init();
    if(!init_mixer_status){
        LOG_E(TAG, "Mixer can't be initialized...");
        audio_error_cb();
        return NULL;
    }
    struct pcm_config config;
    struct pcm *pcm;
    char buffer[AUDIO_PACKET_SIZE_MAX];
    unsigned int size;
    unsigned int frames_read;
    unsigned int total_frames_read;
    unsigned int bytes_per_frame;

    memset(&config, 0, sizeof(config));

    unsigned int device = 0;
    config.channels = 1;
    config.rate = 16000;
    config.period_size = 256;
    config.period_count = 4;
    config.format = PCM_FORMAT_S16_LE;
    config.start_threshold = 0;
    config.stop_threshold = 0;
    config.silence_threshold = 0;
    unsigned int capture_time = UINT_MAX;

    pcm = pcm_open(card, device, PCM_IN, &config);
    if (!pcm || !pcm_is_ready(pcm)) {
        LOG_E(TAG, "Unable to open PCM device (%s)",
                pcm_get_error(pcm));
        audio_error_cb();
        return NULL;
    }

    size = pcm_frames_to_bytes(pcm, pcm_get_buffer_size(pcm));

    if (prinfo) {
        LOG_I(TAG, "Capturing sample: %u ch, %u hz, %u bit", config.channels, config.rate,
           pcm_format_to_bits(config.format));
    }

    bytes_per_frame = pcm_frames_to_bytes(pcm, 1);
    total_frames_read = 0;
    frames_read = 0;

    while (capturing) {
        frames_read = pcm_readi(pcm, buffer, pcm_get_buffer_size(pcm));
        total_frames_read += frames_read;
        if ((total_frames_read / config.rate) >= capture_time) {
            capturing = 0;
            LOG_E(TAG, "Recorder stopped");
            audio_error_cb();
        }
        audio_cb(get_system_monotonic_time(), size, buffer );
    }
    pcm_close(pcm);
    return NULL;
}

bool audio_stop_recorder() {
    LOG_I(TAG, "Stopping recorder");
    capturing = 0;
    return true;
}

bool audio_start_recorder() {

    pthread_t audio_capture_thread;

    //Create audio capture thread
    if(pthread_create(&audio_capture_thread, NULL, capture_audio, NULL)) {
        LOG_E(TAG, "Error creating thread");
        audio_error_cb();
        return false;
    }
    return true;
}