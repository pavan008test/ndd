#ifndef CAM_RECORDER_H
#define CAM_RECORDER_H

#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <glib.h>
#include <string>
#include "config_parser.h"

#ifdef DMS_CAMERA_SUPPORTED
#ifdef __cplusplus
extern "C" {
#endif

#include "dms_api.h"
#include <nvbuf_utils.h>

#ifdef __cplusplus
}
#endif
#endif

using namespace std;

#define GST_OTHER_CAMERA_WIDTH (1280)
#define GST_OTHER_CAMERA_HEIGHT (720)

#define GST_CAMERA_WIDTH_MODE_A (1920)
#define GST_CAMERA_HEIGHT_MODE_A (1080)

#define GST_CAMERA_WIDTH_MODE_B (1600)
#define GST_CAMERA_HEIGHT_MODE_B (900)

#define BAGHEERACONFIG_INI "/home/ubuntu/.nddevice/latest/bagheera_config.ini"
#define ND_CONFIG_ANALYTICS "/home/ubuntu/.nddevice/latest/nd_config.ini"
#define CAMERA_OVERRIDE_INI "/home/ubuntu/config/cam_override.ini"

#define ZMQ_SOCKET_IN_RT "ipc:///dev/shm/MSGQ/6360"
#define ZMQ_SOCKET_DMS_RT "ipc:///dev/shm/MSGQ/6361"

static const int inward_cam_width = 1280;
static const int inward_cam_height = 720;

static const string log_dir = "/home/ubuntu/.nddevice/log/cam_rec";

typedef enum {
   STATE_UNINITIALIZED = 0,
   STATE_CREATED ,
   STATE_INIT,
   STATE_PLAYING,
   STATE_STOP,
   STATE_ERROR,
   STATE_EXIT
} pipeline_state;

typedef enum camera_pos {
  CAMERA_POSITION_FRONT = 0,
  CAMERA_POSITION_BACK,
  CAMERA_POSITION_LEFT,
  CAMERA_POSITION_RIGHT,
  CAMERA_POSITION_DMS = 8,
  CAMERA_POSITION_MAX
} camera_pos;

#define MAX_SOCKET_NAME_LEN 128
typedef struct {
  bool enable_streaming;
  char socket[MAX_SOCKET_NAME_LEN];
  int fps;
  int width;
  int height;
} realtime_camera_config_t;

typedef struct {
  int fps;
  int width;
  int height;
  int bitrate;
} nrt_in_camera_config_t;

typedef struct
{
  int fps;
  int width;
  int height;
  int bitrate;
  int crop;
}nrt_left_camera_config_t;

typedef struct
{
  int fps;
  int width;
  int height;
  int bitrate;
  int crop;
}nrt_right_camera_config_t;

typedef struct
{
  int  fps;
  int  width;
  int  height;
  int  bitrate;
}nrt_dms_camera_config_t;

typedef struct
{
  uint64_t pts;
  int64_t uid;
  int smb_id;
}rt_metadata_t;

#ifdef DMS_CAMERA_SUPPORTED
typedef struct
{
  uint64_t pts;
  NvBufferParamsEx paramsEx;
}dms_rt_metadata_t;
#endif

#endif

