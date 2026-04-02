/* Copyright (C) 2016 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Shravan Kumar M <shravan.kumar@netradyne.com>, August 2019
 */

#ifndef AUDIO_H
#define AUDIO_H

#include <vector>
#include <string>
#include <stdint.h>

#include <component.h>
#include <error.h>

using namespace std;

#ifdef BAGHEERA2
#define AUDIO_PACKET_SIZE_MAX 1024
#elif KRAIT
#define AUDIO_PACKET_SIZE_MAX 2048
#endif

class AudioCtx;

class Audio: public Component {
public:

	/** Inherited**/

	bool register_alive_callback(component_alive_cb_t *cb , void *app);

	bool register_error_callback(component_error_cb_t *cb , void *app);

	vector< pair< string, string > > get_capabilities();

	vector< pair< string, string > > dump_stats();

	bool configure(vector< pair< string, string > > &config);

	bool configure(string key, string value);

	string get_config(string key);

	struct audio_t {
		char data[AUDIO_PACKET_SIZE_MAX]; // signed 16-bit audio PCM data for 16K mono audio.
                                 // Actual size would be always 800 bytes but keeping 1024
                uint16_t size; // size of frame in bytes
		uint64_t time_stamp; // monotonic time at which we got the data
	};

	typedef bool audio_callback_t (uint64_t cb_pts, int data_size, char* data);
	typedef bool audio_err_callback_t (void);

	static Audio* get_audio();
	static bool release_audio();
        static bool start_audio();
        static bool stop_audio();

	bool   register_audio_callback(audio_callback_t *cb, audio_err_callback_t *err_cb);
        bool   set_data(Audio::audio_t *audio_sample, int64_t cb_pts, int data_size, char* data);
        static bool   audio_encode(string input, string output);
    string audio_pcm_partial_file_base_path ;
    string next_audio_pcm_partial_file_base_path ;    
  	vector< Audio::audio_t > audio_partial_pcm_buff_vec;
    int fd_partial_pcm ;

private:
	static Audio    *sObj;
	static AudioCtx ctx;
	Audio( string name);
	~Audio();

};

#endif


