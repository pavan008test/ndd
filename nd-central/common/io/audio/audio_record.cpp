/* Copyright (C) 2019 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Shravan Kumar M <shravan.kumar@netradyne.com>, August 2019
 */

#include <string.h>
#include <audio_dev.h>
#include <log.h>

#define TAG "AUD"

struct AudioCtx {
	int handle;
};

Audio * Audio::sObj=NULL;
AudioCtx Audio::ctx = {-1};
string audio_str = "";

Audio::Audio( string name ) : Component(name) {
    audio_pcm_partial_file_base_path = "";
    next_audio_pcm_partial_file_base_path = "";
}

Audio::~Audio() {
}

Audio* Audio::get_audio() {
	if( sObj == NULL ) {
		sObj = new Audio(audio_str);
	}
	return sObj;
}

bool Audio::release_audio() {
	if( sObj ) {
		delete sObj;
		sObj = NULL;
		return true;
	}
	return false;
}

bool   Audio::register_audio_callback(Audio::audio_callback_t *cb, Audio::audio_err_callback_t *err_cb) {
	return audio_dev_reg_cb(cb, err_cb);
}

bool   Audio::start_audio() {
	return audio_start_recorder();
}

bool   Audio::stop_audio() {
	return audio_stop_recorder();
}

bool Audio::register_alive_callback(component_alive_cb_t *cb, void *app) {

}

bool Audio::register_error_callback(component_error_cb_t *cb, void *app) {

}

vector< pair< string, string > > Audio::get_capabilities() {

}

vector< pair< string, string > > Audio::dump_stats() {

}

bool Audio::configure(vector< pair< string, string > > &config) {
}

bool Audio::configure(string key, string value) {
}

string Audio::get_config(string key) {

}

bool Audio::set_data(Audio::audio_t *audio_sample, int64_t cb_pts, int data_size, char* data) {

    audio_sample->time_stamp = cb_pts;
    audio_sample->size = data_size;
    memcpy((void *)&(audio_sample->data[0]), (const void *)data, data_size);
    return true;
}

bool Audio::audio_encode(string input, string output) {
    return audio_encode_dev(input, output);
}
