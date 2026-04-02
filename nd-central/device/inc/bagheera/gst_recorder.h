/* Copyright (C) 2016 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Pawan Kumar <pawan.kumar@netradyne.com>, October 2016
 */

#ifndef GST_RECORDER_H
#define GST_RECORDER_H

#ifdef __cplusplus
//extern "C" {
#endif

#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <glib.h>
#include "MediaRecorder.h"

/*This width and height is for the encode pipeline*/
#define GST_MAIN_CAMERA_WIDTH (1920)
#define GST_MAIN_CAMERA_HEIGHT (1080)

#define GST_OTHER_CAMERA_WIDTH (1280)
#define GST_OTHER_CAMERA_HEIGHT (720)

typedef enum 
{
   STATE_UNINITIALIZED = 0,
   STATE_CREATED ,
   STATE_INIT,
   STATE_PLAYING,
   STATE_NEW_SESSION,
   STATE_STOP,
   STATE_ERROR,
   STATE_EXIT
} pipeline_state; 

typedef enum camera_pos
{
  CAMERA_POSITION_FRONT = 0,
  CAMERA_POSITION_BACK,
  CAMERA_POSITION_LEFT,
  CAMERA_POSITION_RIGHT,
  CAMERA_POSITION_MAX
}camera_pos;

typedef enum record_file_event {
  GST_RECORD_FILE_START = 100,
  GST_RECORD_FILE_STOP
} record_file_event;

typedef enum record_status_event
{
  GST_RECORD_EVENT_FIRST_FRAME_MARKER = 200,
  GST_RECORD_EVENT_KEEP_ALIVE,
  GST_RECORD_EVENT_PIPELINE_ERR,
  GST_RECORD_EVENT_INTERNAL_ERR,
  GST_RECORD_EVENT_REQ_NEW_FNAME,
  GST_RECORD_EVENT_SHM_FAILED
} record_status_event;


typedef void (*record_file_callback_t)(record_file_event event,
                                       uint64_t raw_time_us, uint64_t epoch_time_us, uint64_t pts_time,
                                       void *app, uint64_t session_frame_count, bool is_ld, void* filename);

typedef void (*record_timestamp_callback_t)(uint64_t time_ld, uint64_t pts_time_ld, void *app);

typedef void (*record_data_prod_callback_t)(uint64_t time_dp, uint64_t pts_time_dp, void *app);

typedef void (*record_status_callback_t)(record_status_event status, void *app);

typedef const char* (*record_fname_cb_t)(void *app, uint64_t session_first_frame_epoch_time_ms);

typedef void (*record_appsink_cb_t)(int64_t cb_pts, int cb_cam_pos, uint64_t raw_time_ns, uint64_t epoch_time_ns,
              uint64_t raw_frame_sent_time_ms, int64_t data_size, int smb_id, int64_t uid, void *app );

typedef void (*frame_write_callback_t)(gulong frame_number, uint64_t raw_time_us, uint64_t epoch_time_us, void *app);

typedef void (*live_stream_frame_drop_cb_t)( void *app, int cam_num);

//Blocking call to create camera record session
void *init_camera_record_platform(native_camera_config_t native_cam_config, realtime_camera_config_t* rt_config);

//Blocking call waits until pipeline is created and set to PAUSED
int init_record_session_platform(void *hndl);

bool stopkinesis (void *data, req_livestreaming_data_t *kinesis_req_msg);
bool startkinesis (void *data, req_livestreaming_data_t *req_msg);

//Non-blocking call to start record session as PLAYING , callback for record_file_event
//to notify actual start
int start_record_session_platform(void *ptr);                                     

bool restart_record_session_platform(void *ptr, bool restart_pipeline_required);

void recreate_rt_shared_memory(void *ptr);

bool device_set_privacy(int cam_num, int privacy);
//Blocking call to stop record session, callback for record_file_event
//to notify exact stop
int stop_record_session_platform(void *ptr);

//Blocking call that destroys camera record session
void destroy_camera_record_platform(void *ptr);

//Set asynchronous notification callbacks
int set_record_callback_platform(void *ptr,
                                 record_file_callback_t file_cb,
                                 record_status_callback_t status_cb,
                                 frame_write_callback_t frame_cb,
                                 record_timestamp_callback_t timestamp_cb,
								 record_data_prod_callback_t data_prod_cb,
								 void *app);

//Set file name callbacks
int set_fname_callback_platform(void *ptr, record_fname_cb_t fname_cb,
                                void *app);

//Set file name callbacks
int set_appsink_callback_platform(void *ptr, record_appsink_cb_t fname_cb/*, void *app*/);

//Set live stream frame drop callback
int set_live_stream_frame_drop_callback_platform(void *ptr, live_stream_frame_drop_cb_t live_stream_frame_drop_cb);

//Setting split-now signal
void emit_split_now_signal(void *ptr);

#ifdef __cplusplus
//}
#endif


#endif

