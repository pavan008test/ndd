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
#include "MediaRecorder.h"

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

typedef void (*frame_write_callback_t)(uint64_t frame_number, uint64_t raw_time_us, uint64_t epoch_time_us, void *app);

typedef void (*live_stream_frame_drop_cb_t)( void *app, int cam_num);

typedef bool (*qr_scan_cb_t)(void *app, unordered_map<string, vector<string>> qr_scan_out, int num_qr_codes_detected, uint64_t qr_frame_ts, qr_scan_status qrscan_status);

//Blocking call to create camera record session
void *init_camera_record_platform(native_camera_config_t native_cam_config, realtime_camera_config_t* rt_config);

//Blocking call waits until pipeline is created and set to PAUSED
int init_record_session_platform(void *hndl);

//Non-blocking call to start record session as PLAYING , callback for record_file_event
//to notify actual start
int start_record_session_platform(void *ptr);                                     

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

bool device_set_privacy(int cam_num, int privacy);

bool device_start_QR_scan(char tags[kQRScanMaxTagPatterns][kQRScanMaxTagPatternDataLen]);

bool device_stop_QR_scan();

//Set live stream frame drop callback
int set_live_stream_frame_drop_callback_platform(void *ptr, live_stream_frame_drop_cb_t live_stream_frame_drop_cb);

int set_qr_scan_callback_platform(void *ptr, qr_scan_cb_t qr_scan_cb);

void recreate_rt_shared_memory(void *ptr);

#ifdef __cplusplus
//}
#endif


#endif

