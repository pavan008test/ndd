/* Copyright (C) 2019 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Shravan Kumar M <shravan.kumar@netradyne.com>, August 2019
 */

#include <glib.h>
#include <gst/gst.h>
#include "gst_recorder.h"
#include "gst/app/gstappsink.h"

#include "log.h"
#include "nd_time.h"
#include "nd_file_utils.h"
#include "audio_record.h"
#include "nd_factory.h"

#define GSTREAMER_ALSASRC_ELEMENT      "alsasrc"
#define GSTREAMER_CAPSFILTER_ELEMENT   "capsfilter"
#define GSTREAMER_QUEUE_ELEMENT        "queue"
#define GSTREAMER_AUDIOENC_ELEMENT     "voaacenc"
#define GSTREAMER_APPSINK_ELEMENT      "appsink"
#define GSTREAMER_FILESINK_ELEMENT     "filesink"

#define AUDIO_QUEUE_BUFF_TIME (4)
#define AUDIO_CRASH_THREAD_INTERVAL_IN_SECS 60
#define FPS_THRESHOLD_FOR_AUDIO_CRASH 5.0
#define AUDIO_SAMPLING_RATE 16000
static const string AUDIO_SAMPLING_RATE_STR  = "16000";

extern ND_DeviceFactory *nd_device_obj;

static const char *TAG="AUD";

  GstElement      *pipeline = NULL;
  GstElement      *alsasrc = NULL;
  GstElement      *alsacaps = NULL;
  GstElement      *audio_queue1 = NULL;
  GstElement      *appsink = NULL;

pthread_mutex_t audio_count_mutex = PTHREAD_MUTEX_INITIALIZER;
gulong          audio_frame_count;

static Audio::audio_callback_t *audio_cb=NULL;
static Audio::audio_err_callback_t *audio_error_cb=NULL;

bool audio_check_thread_created = false;
pthread_t audio_check;


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

void *audio_check_thread(void *arg)
{

	unsigned long int curr_time = g_get_monotonic_time();
	unsigned long int prev_time = curr_time;
	float fps;
	while (1) {
		sleep(AUDIO_CRASH_THREAD_INTERVAL_IN_SECS);
		curr_time = g_get_monotonic_time();
		pthread_mutex_lock(&audio_count_mutex);
		// time diff is in micro seconds
		fps = ( audio_frame_count * 1000.0 * 1000 ) / (curr_time - prev_time);
		if(fps < FPS_THRESHOLD_FOR_AUDIO_CRASH) {
			LOG_E(TAG, "mic is not giving frames, do error callback: count = %d, fps = %f", audio_frame_count, fps);
			audio_error_cb();
		}
		prev_time = curr_time;
		audio_frame_count = 0;
		pthread_mutex_unlock(&audio_count_mutex);
	}
}


static gboolean create_audio_elements()
{
    GstCaps *caps = NULL;

    alsacaps = NULL;
    alsasrc = gst_element_factory_make(GSTREAMER_ALSASRC_ELEMENT, NULL);
    if (NULL == alsasrc) {
        LOG_E(TAG, "Element %s creation failed \n",GSTREAMER_ALSASRC_ELEMENT);
        return false;
    }

    if(eBagheera_3 == nd_device_obj->getDeviceType()) {
	    g_object_set (alsasrc, "device", "hw:tegrasndt186ref,1", NULL);
    } else {
	    g_object_set (alsasrc, "device", "hw:1,0", NULL);
    }

    if (alsacaps == NULL) {

        alsacaps = gst_element_factory_make(GSTREAMER_CAPSFILTER_ELEMENT,NULL);

        if (NULL == alsacaps) {
            LOG_E(TAG,"Element %s creation failed \n",GSTREAMER_CAPSFILTER_ELEMENT);
            return false;
        }

        caps = gst_caps_new_simple("audio/x-raw",
                       "rate", G_TYPE_INT , AUDIO_SAMPLING_RATE,
                       "channels", G_TYPE_INT , 1,
                       NULL);

        if (NULL == caps) {
            LOG_E(TAG,"Creation of audio caps failed \n");
            return false;
        }

        g_object_set (alsacaps, "caps",caps, NULL);
        gst_caps_unref(caps);
    }

    audio_queue1 = gst_element_factory_make(GSTREAMER_QUEUE_ELEMENT,NULL);
    if (NULL == audio_queue1) {
        LOG_E(TAG,"Element %s creation failed for audio \n",GSTREAMER_QUEUE_ELEMENT);
        return false;
    }
    g_object_set (audio_queue1, "max-size-buffers",0, NULL); //Disabled
    g_object_set (audio_queue1, "max-size-bytes",0, NULL); //Disabled
    g_object_set (audio_queue1, "max-size-time",AUDIO_QUEUE_BUFF_TIME*1000000000, NULL);

    appsink = gst_element_factory_make(GSTREAMER_APPSINK_ELEMENT,NULL);
    if (NULL == appsink) {
        LOG_E(TAG,"Element %s creation failed for audio \n",GSTREAMER_APPSINK_ELEMENT);
        return false;
    }
    g_object_set (G_OBJECT (appsink), "emit-signals", TRUE, "max-buffers", 3, "async", FALSE, NULL);
    //g_object_set(appsink, "location", "/home/ubuntu/audioenc.aac", NULL);

    return true;
}

static void appsink_audio_cb(GstAppSink *object, gpointer user_data)
{
    uint64_t curr_time = get_system_time();

    pthread_mutex_lock(&audio_count_mutex);
    audio_frame_count++;
    pthread_mutex_unlock(&audio_count_mutex);

    GstAppSink* app_sink = (GstAppSink*) object;
    GstSample *sample = gst_app_sink_pull_sample(app_sink);
    GstBuffer *buffer = gst_sample_get_buffer(sample);
    GstMapInfo map;
    gst_buffer_map (buffer, &map, GST_MAP_READ);

    LOG_D(TAG, "data received size = %d, time = %lu", map.size, curr_time);
    if (audio_cb)
    {
      audio_cb(curr_time, map.size, (char *)map.data);
    }

    gst_sample_unref(sample);
    gst_buffer_unmap(buffer, &map);

    return;
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



bool audio_start_recorder() {

 GstBus *bus;
  GstMessage *msg;
  
  GMainLoop *loop;
  GError *err = NULL;

  LOG_I(TAG,"Audio pipeline start \n");
  /* Initialize GStreamer */
  //gst_init (&argc, &argv);
   if (!gst_init_check(NULL,NULL,&err)) {
        LOG_C(TAG,"gstreamer init failed %s \n",err->message);
        return false;
   }
  loop = g_main_loop_new (NULL, FALSE);

  pipeline = gst_pipeline_new ("my-pipeline");

  if(!create_audio_elements()) {
        LOG_C(TAG,"create_audio_elements failed. Exiting \n");
        return false;
  }
      gst_bin_add_many(GST_BIN (pipeline),alsasrc,
                                     alsacaps,
                                     audio_queue1,
                                     appsink,
                                     NULL);

	    if (!gst_element_link_many (alsasrc,
	    			        alsacaps,
					audio_queue1,
					appsink,
					NULL)) {
		    LOG_E(TAG,"Linking audio pipeline elements failed");
		    return false;
	    }

  /* Conecting to the new-buffer signal emited by the appsink */
  g_signal_connect (appsink, "new-sample",  G_CALLBACK (appsink_audio_cb), NULL);

    if(audio_check_thread_created == false) {
        if (!pthread_create (&audio_check, NULL, audio_check_thread, NULL))
        {
            audio_check_thread_created = true;
        }
        else {
            LOG_E (TAG, "audio_check_thread creation failed");
        }
    }


  /* Start playing */

      gst_element_set_state (pipeline, GST_STATE_PLAYING);

  return true;
}

bool audio_stop_recorder() {
      LOG_E(TAG,"Stopping audio\n");
      gst_element_set_state (pipeline, GST_STATE_NULL);
      gst_object_unref (pipeline);
      pipeline = NULL;
      return true;
}
