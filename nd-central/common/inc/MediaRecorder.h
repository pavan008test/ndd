/* Copyright (C) 2016 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Pawan Kumar <pawan.kumar@netradyne.com>, October 2016
 */

#ifndef MEDIA_H
#define MEDIA_H

#include <vector>
#include <string>
#include <stdint.h>
#include <system_utils.h>

#include "component.h"
#include "error.h"
#include "nd_shared_mem_utils.h"
#include "nd_msg_types.h"

#include <unordered_map>

#define SPLIT_MUX
using namespace std;


/* Camera position to pass in create media recorder */
typedef enum cam_pos_t {
   DEVICE_CAMERA_POSITION_FRONT = 0,
   DEVICE_CAMERA_POSITION_BACK,
   DEVICE_CAMERA_POSITION_LEFT,
   DEVICE_CAMERA_POSITION_RIGHT,
   DEVICE_CAMERA_POSITION_MAX,
   EXT_CAMERA_POSITION_CH1 = DEVICE_CAMERA_POSITION_MAX,
   EXT_CAMERA_POSITION_CH2,
   EXT_CAMERA_POSITION_CH3,
   EXT_CAMERA_POSITION_CH4,
   EXT_CAMERA_POSITION_MAX,
   DEVICE_CAMERA_POSITION_DMS = EXT_CAMERA_POSITION_MAX,
   CAMERA_POSITION_MAXIMUM
}cam_pos_t;

#define KEY_MEDIA_PARAM_ENCODER_FORMAT            "enc-fmt"
#define KEY_MEDIA_PARAM_SENSOR_WIDTH              "sens-width"
#define KEY_MEDIA_PARAM_SENSOR_HEIGHT             "sens-height"
#define KEY_MEDIA_PARAM_SENSOR_COLOR_FORMAT       "sens-form"
#define KEY_MEDIA_PARAM_CONTAINER_FORMAT          "cont-form"

#define VALUE_MEDIA_PARAM_ENCODER_FORMAT_H264 "h264"
#define VALUE_MEDIA_PARAM_ENCODER_FORMAT_H265 "h265"

#define VALUE_MEDIA_PARAM_SENSOR_WIDTH_FULLHD "1920"
#define VALUE_MEDIA_PARAM_SENSOR_HEIGHT_FULLHD "1080"
#define VALUE_MEDIA_PARAM_SENSOR_WIDTH_HD "1280"
#define VALUE_MEDIA_PARAM_SENSOR_HEIGHT_HD "720"

#define VALUE_MEDIA_PARAM_SENSOR_COLOR_FORMAT_I420 "I420"
#define VALUE_MEDIA_PARAM_SENSOR_COLOR_FORMAT_NV12 "NV12"
#define VALUE_MEDIA_PARAM_SENSOR_COLOR_FORMAT_NV21 "NV21"

#define VALUE_MEDIA_PARAM_CONTAINER_FORMAT_MP4 "mp4"

typedef enum ColorFormat
{
   COLOR_SPACE_I420 = 100,
   COLOR_SPACE_NV12 ,
   COLOR_SPACE_NV21
} ColorFormat;

struct cam_buffer_t {
       void *app_data;
       int   width;
       int   height;
       unsigned char *data;
       long size;
       ColorFormat fmt;                                         		 
};

typedef enum RecordState
{
    RECORD_START = 100,
    RECORD_STOP 
}RecordState;

typedef enum SessionState
{
    SESSION_STATE_UNINITIALIZED = 200,
    SESSION_STATE_CREATED,
    SESSION_STATE_INIT,
    SESSION_STATE_PLAYING,
    SESSION_STATE_NEW,
    SESSION_STATE_STOP,
    SESSION_STATE_ERROR,
    SESSION_STATE_EXIT

}SessionState;

#define MAX_SOCKET_NAME_LEN 128
typedef struct
{
  bool enable_streaming;
  char socket[MAX_SOCKET_NAME_LEN];
  int fps;
  int width;
  int height;
}realtime_camera_config_t;

typedef struct
{
  int fps;
  int width;
  int height;
  int bitrate;
}nrt_out_camera_config_t;

typedef struct
{
  cam_pos_t cam_pos;
  string name;
}native_camera_config_t;

typedef struct
{
  /* 1 added for supporting DMS camera */
  bool status[CAMERA_POSITION_MAXIMUM];
} cam_crash_status_t;

typedef enum qr_scan_status {
  QR_SCAN_NO_QR,
  QR_SCAN_QR_DETECTED,
  QR_SCAN_QR_DECODED,
  QR_SCAN_TIMEDOUT
} qr_scan_status;

typedef bool (*buffer_callback_t) (cam_buffer_t *buff);

typedef void (*first_frame_notify_t) (void *);
typedef string (fname_cb_t) (void *, uint64_t session_first_frame_epoch_time_ms);
typedef void (appsink_cb_t) (int64_t cb_pts, int cb_cam_pos, uint64_t raw_time_ns,
              uint64_t epoch_time_ns, uint64_t raw_frame_sent_time_ms, int64_t data_size, int smb_id, int64_t uid);
typedef void (*frame_write_cb_t)(uint64_t frame_number, uint64_t raw_time_ns, uint64_t epoch_time_ns, void *app);
typedef void (timestamp_cb_t) (uint64_t time_ld, uint64_t pts_time_ld, void *app);
typedef void (data_prod_cb_t) (uint64_t epoch_time_dp, uint64_t pts_time_dp, void *app);

typedef void (*record_state_callback_t)(RecordState state, uint64_t raw_time_us, uint64_t epoch_time_us,
                                        uint64_t pts_time, void *app, uint64_t session_frame_count, bool is_ld, void* filename);

typedef void (livestream_framedrop_cb_t) (int cam_num);

typedef bool (qrscan_cb_t) (unordered_map<string, vector<string>> qr_scan_out, int num_qr_codes_detected, uint64_t qr_frame_ts, qr_scan_status qrscan_status);

class MediaRecorder: public Component {
public:

	/** Inherited**/

	bool register_alive_callback( component_alive_cb_t *cb , void *app);

	bool register_error_callback( component_error_cb_t *cb , void *app);

	bool register_fname_callback( fname_cb_t *cb, void *app);

	bool register_appsink_callback( appsink_cb_t *cb, void *app); 

	bool register_frame_write_callback (frame_write_cb_t cb, void *app);

	bool register_timestamp_callback( timestamp_cb_t *cb, void *app);

	bool register_data_prod_callback( data_prod_cb_t *cb, void *app);

	bool register_livestream_framedrop_callback( livestream_framedrop_cb_t cb, void *app);

	bool register_qrscan_callback( qrscan_cb_t cb, void *app);

	vector< pair< string, string > > get_capabilities();

	vector< pair< string, string > > dump_stats();

	bool configure(vector< pair< string, string > > &config);

	bool configure(string key, string value);

	string get_config(string key);	

	bool set_record_state_cb(record_state_callback_t state_cb, void *app);

	static MediaRecorder * create_media_recorder(native_camera_config_t native_cam_config, realtime_camera_config_t rt_config);

	bool   init_camera_record_session ( );

	bool   start_record_session ( );

	bool   start_kinesis_session ( req_livestreaming_data_t *start_kinesis_msg );

	bool   stop_kinesis_session ( req_livestreaming_data_t *stop_kinesis_msg );

	static bool release_media_recorder(MediaRecorder *media);
	
	bool   stop_record_session ( );

	void   end_record_session(bool restart_pipeline_required);

	void   recreate_rt_shared_mem();

	bool   register_first_camera_buffer_notify_callback(first_frame_notify_t cb,void *app_cb);
 
	bool   register_camera_buffer_callback(void *cb);

	bool   release_camera_buffer(cam_buffer_t *buff);

	bool   isInit() ;

	bool   set_privacy(int cam_num, int privacy);

  	bool   start_QR_scan(char tags[kQRScanMaxTagPatterns][kQRScanMaxTagPatternDataLen]);

  	bool   stop_QR_scan();

private:

	MediaRecorder (native_camera_config_t native_cam_config, realtime_camera_config_t rt_config);

	~MediaRecorder();

    bool isActive; 

    cam_pos_t cam_pos;

    void *plat_handle;

    vector< pair <string,string> > param;

    string comptag;

};

typedef struct obj_context_t {
  MediaRecorder             *mobj;
  record_state_callback_t   record_cb;
  first_frame_notify_t      first_cb;
  component_error_cb_t      *error_cb;
  fname_cb_t                *fname_cb;
  appsink_cb_t              *appsink_cb;
  component_alive_cb_t      *alive_cb;
  frame_write_cb_t          frame_write_cb;
  timestamp_cb_t            *timestamp_cb;
  data_prod_cb_t            *data_prod_cb;
  livestream_framedrop_cb_t *livestream_framedrop_cb;
  qrscan_cb_t               *qrscan_cb;
  void                      *app_record_ctxt;
  void                      *app_error_ctxt;
  void                      *app_fname_ctxt;
  void                      *app_alive_ctxt;
  void                      *app_first_ctxt;
  void                      *frame_write_cb_ctxt;
  void                      *app_timestamp_ctxt;
  void                      *app_data_prod_ctxt;
} obj_context_t;

#endif







