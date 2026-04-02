/* Copyright (C) 2016 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Pawan Kumar <pawan.kumar@netradyne.com>, October 2016
 * Written by Y.Suresh Kumar <suresh.kumar@netradyne.com>, October 2017
 */

#include <glib.h>
#include <gst/gst.h>
#include "gst_recorder.h"
#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <sched.h>
#include "nd_time.h"
#include "nd_map.h"
#include "log.h"
#include <stdint.h>
#include "gst/app/gstappsink.h"
#include "gst/app/gstappsrc.h"
#include <sstream>
#include <openssl/md5.h>
#include <sys/timerfd.h>
#include <sys/syscall.h>
#include <sys/sysinfo.h>
#include "nd_factory.h"

#include "file_helper.h"
#include "config_parser.h"
#include "nd_cam_utils.h"
#include "nd_file_utils.h"
#include <sstream>
#include "livestreaming_kinesis.h"
#include <nd_auth_openssl.h>
#include <nd_auth_utils.h>
#include <mutex>

#define DEVICE_CONFIG_INI "/home/ubuntu/config/deviceconfig.ini"
#define CLOUD_CONFIG_INI "/home/ubuntu/.nddevice/latest/cloudconfig.ini"
#define BAGHEERA_CONFIG_INI "/home/ubuntu/.nddevice/latest/bagheera_config.ini"
#define ND_CONFIG_INI "/home/ubuntu/.nddevice/latest/nd_config.ini"

#define GSTREAMER_NAME_LENGTH_MAX (512)

#define OUTWARD_FPS 30
#define MS_IN_SECONDS 1000
#define MS_TO_NS 1000000
#define EA_PREVIEW_OUTWARD_FIRST_SESSION_FRAME_SKIP_COUNT 60

#define fps_30_2_frames ((MS_IN_SECONDS/OUTWARD_FPS) * 2)

#define DROP_LIMIT (fps_30_2_frames * MS_TO_NS)

#define CPU_CORE_0 0 //refers to CPU Core 0 of device to set CPU affinity for Inward recording, Outward recording and RT threads

static bool exit_cam_check_thread = false;
static bool exit_session_change_thread = false;
int timer_fd;
static bool g_live_streaming = false;
static bool stream_encryption = true;
static bool g_enable_dp = false;

static const int i_interval_outcam = 30;
static int outward_rt_buf_size;

static uint64_t prev_pts_dp = 0;

static uint64_t prev_pts_ls = 0;
static uint64_t prev_pts_rt = 0;
static int buff_num_with_zero_pts_hd = 0;
static int buff_num_with_zero_pts_ld = 0;
static int buff_num_with_zero_pts_dp = 0;
static int buff_num_with_zero_pts_rt = 0;
static int buff_num_with_zero_pts_keep_alive = 0;
static int frame_count_rt = 0;
static bool first_cb_ls = true;
static bool first_cb_rt = true;
static bool first_cb_dp_vrconv = true;

static const char ld_extn[] = ".ld.mp4";
static const char dp_extn[] = ".dp.mp4";
static const char ea_extn[] = ".0_ea.jpeg";

static const int GST_PIPELINE_STATE_CHANGE_TIMEOUT = 20; //Timeout of 20 secs for gstreamer pipeline to change its state

int get_md5sum(void* buffer,
		unsigned long buffersize,
		char* checksum);

uint64_t previous_time = 0;

static bool usev4l2src = false;
static int interpolation_method = 0;

const char *new_session_filename = "";
volatile static bool fname_updated = false;
static bool first_cb_dp = true;
static bool first_cb_ld = true;
static bool first_cb_hd = true;

pthread_t session_th;

extern NDService *nd_service_obj; //nd service object, to detect crashes

static const int LS_FPS = 9;

static int OUTWARD_MIN_INTRA_FRAME_INTERVAL_NS;
static const int MIN_INTRA_FRAME_INTERVAL_LS = ((1000/LS_FPS) * 1000 * 1000) - ((1000/(30*2)) *1000 *1000);

volatile int bSendData = 0;
//a frame is changed to valid when it's the first iframe after continuous black frame sending in livestreaming
static const bool VALID_FRAME = false;
//a frame is changed to invalid when after black frames the next frames are not i-frames
static const bool INVALID_FRAME = true;

bool prevFrameState = VALID_FRAME;

#define VERSION_STRING_LENGTH (32)

#define CAM_CRASH_THREAD_INTERVAL_IN_SECS 5
#define INIT_CAM_CRASH_THREAD_INTERVAL_IN_SECS 10
#define FPS_THRESHOLD_FOR_CAM_CRASH 5.0

static const string black_frame_outward_LS = "/home/ubuntu/.nddevice/black_frame_outward_LS.avc";
typedef enum _sensor_map_version {
   SENSOR_MAP_VERSION_INVALID = -1,
   SENSOR_MAP_VERSION_0_1,
   SENSOR_MAP_VERSION_1_0,
   SENSOR_MAP_VERSION_MAX
} sensor_map_version;

sensor_map_version version_id = SENSOR_MAP_VERSION_INVALID;

typedef struct cam_record_ctxt {
  gint            width;
  gint            height;
  camera_pos      cam_pos;
  GCond           cond;
  GMutex          mutex;
  gint            cond_var;
  GstElement      *pipeline;
  GstElement      *live_streaming_pipeline;
  GstElement      *kinesis_app_src;

  GstElement      *data_product_appsrc_pipeline;
  GstElement      *data_product_app_src;

  GstElement      *event_access_pipeline;
  GstElement      *event_access_app_src;

  // realtime
  realtime_camera_config_t rt_config;
  int             outwardcam_yuvbuf_size;
  gint            analytics_width;
  gint            analytics_height;

  nrt_out_camera_config_t nrt_outward_config;

  // LD parameters
  nrt_out_camera_config_t nrt_ld_outward_config;

  GstPad          *keep_alive_pad;
  GstPad          *keep_alive_pad1;
  GstPad          *keep_alive_pad2;
  GstPad          *keep_alive_pad3;
  GstPad          *keep_alive_pad4;
  GstPad          *keep_alive_pad5;
  gulong          keep_alive_probe;
  gulong          keep_alive_probe1;
  gulong          keep_alive_probe2;
  gulong          keep_alive_probe3;
  gulong          keep_alive_probe4;
  gulong          keep_alive_probe5;
  GstPad          *queue_rt_pad;
  gulong          queue_rt_probe;
  gulong          total_frame_count;
  gulong          frame_number;
  gulong          total_frames_written;
  gulong          session_frame_count;
  gulong          cam_frame_count;
  GMutex          cam_frame_mutex;
  GMutex          session_change_mutex;
  GCond           session_change_cond;
  GThread         *thread;
  record_file_callback_t      record_cb;
  record_status_callback_t    event_cb;
  record_fname_cb_t           fname_cb;
  frame_write_callback_t      frame_write_cb;
  record_timestamp_callback_t timestamp_cb;
  record_data_prod_callback_t data_prod_cb;
  record_appsink_cb_t         appsink_cb;
  live_stream_frame_drop_cb_t live_stream_fram_drop_cb;
  void            *app_cb;
  void            *app_fname;
  gchar            cur_file_name[GSTREAMER_NAME_LENGTH_MAX];
  gchar            next_session_fname[GSTREAMER_NAME_LENGTH_MAX];
  gchar            next_session_fname_ld[GSTREAMER_NAME_LENGTH_MAX];
  gchar            next_session_fname_dp[GSTREAMER_NAME_LENGTH_MAX];
  gint             queue_msg;
  pipeline_state   state;
  GAsyncQueue      *queue;
  bool              privacy;

  volatile  uint64_t  mux_start_ts;
  volatile  uint64_t  mux_end_ts;
  
  volatile  uint64_t  session_start_pts;
  uint64_t abs_raw_time;
  uint64_t abs_epoch_time;
  
  volatile  uint64_t  mux_start_ts_ld;
  volatile  uint64_t  mux_end_ts_ld;
  volatile  uint64_t  session_start_pts_ld;

  volatile  uint64_t  mux_start_ts_dp;
  volatile  uint64_t  mux_end_ts_dp;
  volatile  uint64_t  session_start_pts_dp;

  short nd_map_buf_ref_count;

  //kinesis
  aws_kinesis_stream_info_t kinesis_stream_info;

  //shared memory
  NdSharedMemoryWriter *shm_writer;
  int                   fps;
  void                 *frame_data_ptr;
  int                  outward_shm_buffers;
  GMutex               shm_writer_mutex;
  string               shm_writer_name;
  bool                 ld_enabled;
} cam_record_ctxt;

struct data_product_params
{
	int fps;
	int bitrate;
    int iframeinterval;
};

struct data_product_params dp_params;

/* Event Access Specific Declarations Start */
struct event_access_image_params {
    int width;
    int height;
    int quality;
};
struct event_access_image_params ea_image_params;

static int ea_outward_enabled = 0;

int ea_images_per_hr;
int ea_freq_session_wise;

static bool first_forward_ea = true;
static bool first_cb_ea = true;
/* Event Access Specific Declarations End */

static gchar log_tag[CAMERA_POSITION_MAX][GSTREAMER_NAME_LENGTH_MAX] = {0};

static unsigned long int record_duration = (60*1000*1000);

static cam_record_ctxt gst_cam_ctxt[CAMERA_POSITION_MAX] = {0};

static bool streaming = true;
static int kinesis_req_cam_id = -1;
static int kinesis_req_fps = -1;

static void* gst_thread_func(void *ptr);

static volatile bool cam_check_thread_created = false;
pthread_t cam_check;

static volatile int livestream_framedrop_cnt = 0;
#define MAX_FRAME_DROP_ERROR_CNT            12
static const string TMP_KINESIS_STREAMING_OUTWARD = "/tmp/kinesis_streaming_out";
static const string TMP_KINESIS_STREAMING_INWARD = "/tmp/kinesis_streaming_in";
static bool kinesis_dual_streaming = false;
std::mutex dualStreamingFileMutex;

volatile static bool drop_rt_frames = false;
binary_data_t *black_frame_ls = NULL;
UINT32 drop_kinesis_frame_cb(UINT64 customData, STREAM_HANDLE streamHandle, UINT64 frameTimecode)
{
    UNUSED_PARAM(customData);
    UNUSED_PARAM(streamHandle);
    UNUSED_PARAM(frameTimecode);

    if(kinesis_req_cam_id == -1) {
        LOG_E("kinesis", "kinesis_req_cam_id is -1");
        return STATUS_SUCCESS;
    }
    cam_record_ctxt *context = (cam_record_ctxt *)(&gst_cam_ctxt[kinesis_req_cam_id]);
    if (context == NULL) {
        LOG_E("kinesis", "Context is NULL");
        return STATUS_SUCCESS;
    }
    livestream_framedrop_cnt++;
    if ((livestream_framedrop_cnt >= MAX_FRAME_DROP_ERROR_CNT) && (!streaming)) {
        if (context->live_stream_fram_drop_cb != NULL) {
            if (context->app_cb == NULL) {
                LOG_E("kinesis", "app_cb is NULL");
                return STATUS_SUCCESS;
            }
            context->live_stream_fram_drop_cb(context->app_cb, kinesis_req_cam_id);
            livestream_framedrop_cnt = 0;
            return STATUS_SUCCESS;
        }
    }
    LOG_I("kinesis", "Reported droppedFrame callback");
    return STATUS_SUCCESS;
}

bool device_set_privacy(int cam_num, int privacy) {
    cam_record_ctxt *context = (cam_record_ctxt *)(&gst_cam_ctxt[cam_num]);
    context->privacy = privacy;
    return true;
}

bool stopkinesis (void *data, req_livestreaming_data_t *kinesis_req_msg)
{
    //Deleting both files because stopkinesis will not be called for the camera which is disabled or privacy is enabled
    file_delete(TMP_KINESIS_STREAMING_INWARD);
    file_delete(TMP_KINESIS_STREAMING_OUTWARD);
    prevFrameState = VALID_FRAME;
    LOG_I("kinesis", "Enter stop kinesis for outward camera");
    if ( streaming == false ) {
      cam_record_ctxt *context = (cam_record_ctxt *)data;
      streaming = true;
    }
    cam_record_ctxt *context = (cam_record_ctxt *)data;

    // Stop streaming
    context->kinesis_stream_info.first_time = false;

    /* free the frame buffer */
    if(context->kinesis_stream_info.frame.frameData != NULL)
      free(context->kinesis_stream_info.frame.frameData);

    stopKinesisVideoStream(context->kinesis_stream_info.streamHandle);
    livestream_framedrop_cnt = 0;
    /* Deininitialize and free API calls */
    if (context->kinesis_stream_info.pDeviceInfo != NULL) {
        freeDeviceInfo(&context->kinesis_stream_info.pDeviceInfo);
    }

    if (context->kinesis_stream_info.pStreamInfo != NULL) {
        freeStreamInfoProvider(&context->kinesis_stream_info.pStreamInfo);
    }

    if (IS_VALID_STREAM_HANDLE(context->kinesis_stream_info.streamHandle)) {
        freeKinesisVideoStream(&context->kinesis_stream_info.streamHandle);
    }

    if (IS_VALID_CLIENT_HANDLE(context->kinesis_stream_info.clientHandle)) {
        freeKinesisVideoClient(&context->kinesis_stream_info.clientHandle);
    }

    if (context->kinesis_stream_info.pClientCallbacks != NULL) {
        freeCallbacksProvider(&context->kinesis_stream_info.pClientCallbacks);
    }

    if (black_frame_ls != NULL) {
        free(black_frame_ls);
    }

    LOG_I("kinesis", "Exit stop kinesis for outward camera");
    return true;
}


bool read_config_params(cam_record_ctxt *context)
{
    Config_parser *bagheera_config = new Config_parser(BAGHEERA_CONFIG_INI);
    if (bagheera_config->getParseStatus() != true) {
        LOG_E(log_tag[context->cam_pos], "Cannot allocate bagheera Config");
        return false;
    }
    string bagheera_read_field = "live_streaming";
    // Read width
    if (!string_to_integer(bagheera_config->getConfig(bagheera_read_field,"width",""), live_streaming_param.width)) {
        LOG_I(log_tag[context->cam_pos],"cannot find live streaming width of cam %d in bagheera_config", context->cam_pos);
        live_streaming_param.width = 640;
    }
    // Read height
    if (!string_to_integer(bagheera_config->getConfig(bagheera_read_field,"height",""), live_streaming_param.height)) {
        LOG_I(log_tag[context->cam_pos],"cannot find live streaming height of cam %d in bagheera_config", context->cam_pos);
        live_streaming_param.height = 360;
    }
    // Read framerate
    if (!string_to_integer(bagheera_config->getConfig(bagheera_read_field,"fps",""), live_streaming_param.fps)) {
        LOG_I(log_tag[context->cam_pos],"cannot find live streaming fps of cam %d in bagheera_config", context->cam_pos);
        live_streaming_param.fps = 10;
    }
    // Read iframe interval
    if (!string_to_integer(bagheera_config->getConfig(bagheera_read_field,"iframeinterval",""), live_streaming_param.iframeinterval)) {
        LOG_I(log_tag[context->cam_pos],"cannot find live streaming iframeinterval of cam %d in bagheera_config", context->cam_pos);
        live_streaming_param.iframeinterval = 10;
    }
    // Read bitrate
    if (!string_to_integer(bagheera_config->getConfig(bagheera_read_field,"bitrate",""), live_streaming_param.bitrate)) {
        LOG_I(log_tag[context->cam_pos],"cannot find live streaming bitrate of cam %d in bagheera_config", context->cam_pos);
        live_streaming_param.bitrate = 512000;
    }

    bool get_override_val = true, is_val_overridden = false;

    // Read interpolation method
    string temp = bagheera_config->getConfig("camera","interpolation_method", "3", get_override_val, is_val_overridden);
    if (temp == "") {
	    LOG_I(log_tag[context->cam_pos],"interpolation method is not found in conf file");
	    interpolation_method = 3;
    } else {
	    string_to_integer(temp, interpolation_method);
    }
    LOG_I(log_tag[context->cam_pos],"interpolation method is %d",interpolation_method);

    temp = bagheera_config->getConfig("camera","video_encryption", "true", get_override_val, is_val_overridden);
    if (temp == "false") {
        stream_encryption = false;
    } else {
        stream_encryption = true;
    }
    LOG_I(log_tag[context->cam_pos],"Stream Encryption is %d",stream_encryption);

    // check the LS feature
    string ls_enable_str = bagheera_config->getConfig("live_streaming","enabled", "false", get_override_val, is_val_overridden);
    if (ls_enable_str == "false") {
        g_live_streaming = false;
    } else {
        g_live_streaming = true;
    }
    LOG_I("kinesis", "Live streaming feature = %d", g_live_streaming);

    string nvv4l2_enable_str = bagheera_config->getConfig("camera","usev4l2src", "false", get_override_val, is_val_overridden);
    if (nvv4l2_enable_str == "false") {
      usev4l2src = false;
    } else {
      usev4l2src = true;
    }

    string ld_enabled_str = bagheera_config->getConfig("camera","outward_ld_enabled", "true", get_override_val, is_val_overridden);
    if (ld_enabled_str == "false") {
      context->ld_enabled = false;
    } else {
      context->ld_enabled = true;
    }
    LOG_I(log_tag[context->cam_pos], "LD feature = %d", context->ld_enabled);

    delete bagheera_config;
    return true;
}

// write string to file and return true if success, else false
bool write_string_to_file(string file_name, string str)
{
  ofstream ofile;
  ofile.open (file_name);
  if (ofile.is_open())
  {
    ofile << str;
    ofile.close();
    return true;
  }
  else
  {
    LOG_E("kinesis","Unable to open file %s", file_name.c_str());
    return false;
  }
}

bool startkinesis (void *data, req_livestreaming_data_t *kinesis_req_msg)
{
  if(!g_live_streaming){
    LOG_E("kinesis","LIVE stream is not enabled. Check config\n");
    return false;
  }

    int memfd = -1;
    unsigned char *decrypted_key_buf = nullptr;
    size_t decrypted_key_len = 0;
    bool dec_status = false;
    string priv_key_fd_path;
    int priv_key_fd = -1;

  cam_record_ctxt *context = (cam_record_ctxt *)data;
  string cloud_config_path = CLOUD_CONFIG_INI;
  string device_config_path = DEVICE_CONFIG_INI;
  string device_id, cloud_server;
  std::stringstream iot_thing_name_ss;
  string iot_thing_name;
  unsigned int ret = -1;
  STATUS retStatus = STATUS_SUCCESS;
  int attempt = 1;
  black_frame_ls = read_file(black_frame_outward_LS.c_str());
  LOG_I("kinesis","black_frame_ls size for outward camera = %d", black_frame_ls->size);
  LOG_I("kinesis","LIVE stream Enter start kinesis in gst_recorder\n");
  Config_parser cloud_config_parser(cloud_config_path);
  Config_parser device_config_parser(device_config_path);
  if (cloud_config_parser.getParseStatus() && device_config_parser.getParseStatus())
  {
        device_id = device_config_parser.getConfig("identity","deviceId","");
        if( device_id == "" ) {
          device_id = device_config_parser.getConfig("identity","deviceid","");
        }

        cloud_server = cloud_config_parser.getConfig("cloud","server","prod");
        if ( cloud_server == "prod")
          cloud_server = "production";
        iot_thing_name_ss << cloud_server.c_str() << "-" << device_id.c_str();
        iot_thing_name = iot_thing_name_ss.str();
        nd_strncpy(context->kinesis_stream_info.iot_str, iot_thing_name.c_str(), sizeof(context->kinesis_stream_info.iot_str));
        LOG_I("*********** kinesis","#*#*#*#*#*# LIVE stream iot_thing_name = %s\n", context->kinesis_stream_info.iot_str);
  }
  else
  {
        LOG_E("kinesis","Parsing of config files failed. Exiting!!");
        kinesis_req_msg->error = LS_ERR_UNKNOWN;
        return false;
  }

  /* copy the requested changes */
  context->kinesis_stream_info.req_stream.duration = kinesis_req_msg->duration * HUNDREDS_OF_NANOS_IN_A_SECOND;
  // context->kinesis_stream_info.req_stream.bitrate = 512000;
  // context->kinesis_stream_info.req_stream.fps = 10;
  context->kinesis_stream_info.req_stream.bitrate = kinesis_req_msg->bitrate;
  context->kinesis_stream_info.req_stream.fps = kinesis_req_msg->fps;
  nd_strncpy(context->kinesis_stream_info.req_stream.resolution, kinesis_req_msg->resolution, sizeof(char) * 10);
  nd_strncpy(context->kinesis_stream_info.req_stream.endpoint, kinesis_req_msg->endpoint, sizeof(char) * 100);
  context->kinesis_stream_info.req_stream.camera = kinesis_req_msg->camera;
  context->kinesis_stream_info.req_stream.req_id = kinesis_req_msg->req_id;
  context->kinesis_stream_info.req_stream.id = kinesis_req_msg->id;
  context->kinesis_stream_info.frameIndex = 0;
  context->kinesis_stream_info.frame.frameData = NULL;
  kinesis_req_cam_id = kinesis_req_msg->camera;

  // TODO: debug... Print the contents of set params

  LOG_I("kinesis","LIVE stream starting for kinesis_req_cam_id = %d \n", kinesis_req_cam_id);
  // Create default device info, default storage size is 128MB.
  CHK_STATUS(createDefaultDeviceInfo(&context->kinesis_stream_info.pDeviceInfo));
  context->kinesis_stream_info.pDeviceInfo->clientInfo.loggerLogLevel = LOG_LEVEL_DEBUG;
  context->kinesis_stream_info.pDeviceInfo->storageInfo.storageSize = DEFAULT_STORAGE_SIZE;

    LOG_I("kinesis","createRealtimeVideoStreamInfoProvider");

    /* create and initialize the StreamInfo object */
  LOG_I("kinesis","req_livestreaming_data_t duration: %llu", context->kinesis_stream_info.req_stream.duration);
  LOG_I("kinesis","buffer duration: %llu", DEFAULT_BUFFER_DURATION);
  LOG_I("kinesis","bitrate: %d", context->kinesis_stream_info.req_stream.bitrate);
  LOG_I("kinesis","fps: %d", context->kinesis_stream_info.req_stream.fps);
  LOG_I("kinesis","resolution: %s", context->kinesis_stream_info.req_stream.resolution);
  LOG_I("kinesis","endpoint: %s", context->kinesis_stream_info.req_stream.endpoint);
  LOG_I("kinesis","camera: %d", context->kinesis_stream_info.req_stream.camera);
  LOG_I("kinesis","req_id: %d", context->kinesis_stream_info.req_stream.req_id);
  LOG_I("kinesis","id: %d", context->kinesis_stream_info.req_stream.id);
  if(kinesis_req_msg->dual_streaming) {
      LOG_I("kinesis","Dual Live streaming mode, stream_name:%s\n", kinesis_req_msg->stream_name);
      CHK_STATUS(createRealtimeVideoStreamInfoProvider(kinesis_req_msg->stream_name, DEFAULT_RETENTION_PERIOD, DEFAULT_BUFFER_DURATION, &context->kinesis_stream_info.pStreamInfo));
  }
  else{
      CHK_STATUS(createRealtimeVideoStreamInfoProvider(context->kinesis_stream_info.iot_str, DEFAULT_RETENTION_PERIOD, DEFAULT_BUFFER_DURATION, &context->kinesis_stream_info.pStreamInfo));
  }


  LOG_I("kinesis","setStreamInfoBasedOnStorageSize");
  CHK_STATUS(setStreamInfoBasedOnStorageSize(DEFAULT_STORAGE_SIZE, live_streaming_param.bitrate, 1, context->kinesis_stream_info.pStreamInfo));

    /* Iot certificate validation */
    LOG_I("kinesis","LIVE stream createDefaultCallbacksProviderWithIotCertificate calling\n");
    LOG_I("kinesis","endpoint: %s", context->kinesis_stream_info.req_stream.endpoint);
    LOG_I("kinesis","IOT_CERT_PATH: %s", IOT_CERT_PATH);
    LOG_I("kinesis","IOT_CERT_PRIVATE_KEY_PATH: %s", IOT_CERT_PRIVATE_KEY_PATH);
    LOG_I("kinesis","CA_CERT_PATH: %s", CA_CERT_PATH);
    LOG_I("kinesis","IOT_ROLE_ALIAS: %s", IOT_ROLE_ALIAS);
    LOG_I("kinesis","iot_str: %s -- %d", context->kinesis_stream_info.iot_str, strlen(context->kinesis_stream_info.iot_str));
    LOG_I("kinesis","DEFAULT_AWS_REGION: %s", DEFAULT_AWS_REGION);
// // -----------TODO: temp change. Remove
  //context->kinesis_stream_info.first_time = true;
  /*CHK_STATUS(createDefaultCallbacksProviderWithAwsCredentials((PCHAR)"AKIAVJAW2AD2ZG5GI5PW",
                                                                (PCHAR)"jndKoc2e22XZm8eGOx5pIbfc0yeVJyWZlq49tAdA",
                                                                NULL,
                                                                MAX_UINT64,
                                                                DEFAULT_AWS_REGION,
                                                                CA_CERT_PATH,
                                                                NULL,
                                                                NULL,
                                                                &(context->kinesis_stream_info.pClientCallbacks)));*/

    // Decrypt the private key into a buffer
    decrypted_key_len = nd_file_reoperate_to_buffer(IOT_CERT_PRIVATE_KEY_PATH, &decrypted_key_buf);
    if(decrypted_key_len <=0 || decrypted_key_buf == NULL) {   
        LOG_E("kinesis", "Failed to decrypt IOT_CERT_PRIVATE_KEY_PATH to buffer");
        return false;
    }
    
    try {
        std::string keyData(reinterpret_cast<const char*>(decrypted_key_buf), decrypted_key_len);

        bool memfd_status = createMemfdFromBuffer(keyData, priv_key_fd, "gst_recorder_bagh_priv_key");

        if (memfd_status == false || priv_key_fd == -1) {
            cerr << "Failed to create in-memory file from decoded buffer" << endl;
            free(decrypted_key_buf);
            return false;
        }
        
        lseek(priv_key_fd, 0, SEEK_SET);
        std::ostringstream oss;
        oss << "/proc/self/fd/" << priv_key_fd;
        priv_key_fd_path = oss.str();
    } catch (const std::exception& ex) {
        LOG_E("kinesis", "memfd error: %s", ex.what());
        free(decrypted_key_buf);
        return false;
    }
    free(decrypted_key_buf);

  while(attempt <= 3)
  {

    ret = createDefaultCallbacksProviderWithIotCertificate(context->kinesis_stream_info.req_stream.endpoint, \
        IOT_CERT_PATH, (PCHAR)priv_key_fd_path.c_str(), CA_CERT_PATH, IOT_ROLE_ALIAS, context->kinesis_stream_info.iot_str, DEFAULT_AWS_REGION, NULL, NULL, &context->kinesis_stream_info.pClientCallbacks);

    if(STATUS_SUCCESS != ret)
    {
      LOG_E("kinesis","*** LIVE stream Failed to createDefaultCallbacksProviderWithIotCertificate err - %d", ret);
      if(attempt == 3){
                  kinesis_req_msg->error = LS_ERR_BAD_LTE;
                  close(priv_key_fd);
                  CHK(FALSE, ret);
          //return false;
              }
      attempt++;
    }
    else
    {
      break;
    }
  }
  if (priv_key_fd != -1) close(priv_key_fd);

#ifdef DEBUG
  addFileLoggerPlatformCallbacksProvider(context->kinesis_stream_info.pClientCallbacks, FILE_LOGGING_BUFFER_SIZE, MAX_NUMBER_OF_LOG_FILES, \
                                                                 (PCHAR) QMMF_FILE_LOGGER_LOG_FILE_DIRECTORY_PATH, \
                                                                 TRUE);
#endif
  LOG_I("kinesis", "createStreamCallbacks calling");
  ret = createStreamCallbacks(&context->kinesis_stream_info.pStreamCallbacks);
  if(STATUS_SUCCESS != ret){
    LOG_E("kinesis", "LIVE stream Failed createStreamCallbacks");
    kinesis_req_msg->error = LS_ERR_BAD_LTE;
    CHK(FALSE, ret);
  }

  /* set framedrop call back */
  context->kinesis_stream_info.pStreamCallbacks->droppedFrameReportFn = drop_kinesis_frame_cb;

  LOG_I("kinesis", "addStreamCallbacks calling");
  ret = addStreamCallbacks(context->kinesis_stream_info.pClientCallbacks, context->kinesis_stream_info.pStreamCallbacks);
    if(STATUS_SUCCESS != ret){
        LOG_E("kinesis", "LIVE stream Failed addStreamCallbacks");
        kinesis_req_msg->error = LS_ERR_BAD_LTE;
        CHK(FALSE, ret);
    }

  LOG_I("kinesis", "createKinesisVideoClient calling");
  /* Initializing and configuring KinesisVideoClient and KinesisVideoStream for the pipeline */
  ret = createKinesisVideoClient(context->kinesis_stream_info.pDeviceInfo, context->kinesis_stream_info.pClientCallbacks, &context->kinesis_stream_info.clientHandle);
    if(STATUS_SUCCESS != ret){
        LOG_E("kinesis", "LIVE stream Failed createKinesisVideoClient");
        kinesis_req_msg->error = LS_ERR_BAD_LTE;
        CHK(FALSE, ret);
    }

  LOG_I("kinesis", "createKinesisVideoStreamSync calling");
  ret = createKinesisVideoStreamSync(context->kinesis_stream_info.clientHandle, context->kinesis_stream_info.pStreamInfo, &context->kinesis_stream_info.streamHandle);
    if(STATUS_SUCCESS != ret){
        LOG_E("kinesis", "LIVE stream Failed createKinesisVideoStreamSync");
        kinesis_req_msg->error = LS_ERR_BAD_LTE;
        CHK(FALSE, ret);
    }

  /* allocate memory for frame */
  if(context->kinesis_stream_info.frame.frameData == NULL)
        context->kinesis_stream_info.frame.frameData = (BYTE *) calloc(sizeof(char), MAX_FRAME_BUFFER_SIZE);

    //Enable streaming
  context->kinesis_stream_info.first_time = true;
  streaming = false;
  kinesis_dual_streaming = kinesis_req_msg->dual_streaming;
  if(kinesis_req_msg->dual_streaming){
    kinesis_dual_streaming = true;
    file_touch(TMP_KINESIS_STREAMING_OUTWARD);
    write_string_to_file(TMP_KINESIS_STREAMING_OUTWARD, "1");
  }
CleanUp:

    if (STATUS_FAILED(retStatus)) {
        if(kinesis_req_msg->error != LS_ERR_BAD_LTE){
            kinesis_req_msg->error = LS_ERR_UNKNOWN;
        }
        LOG_E("kinesis","Failed with status 0x%08x\n", retStatus);
        stopkinesis (data, kinesis_req_msg);
        return false;
    }

    LOG_I("kinesis","LIVE stream Exit start kinesis\n");
    return true;
}

/*Description:
 *This is the callback registered with appsink which feeds raw frames to appsrc for
 *supporting live streaming functionality at 10 fps
 */
static void ls_vrconv_appsink_cb(GstAppSink *object, gpointer user_data)
{
    // check if context camera position is matching with kinesis req; change camera
    unsigned int kinesis_ret = -1;
    uint64_t pts_diff = 0;
    uint64_t curr_pts = 0;
    GstFlowReturn ret;

    cam_record_ctxt *context = (cam_record_ctxt *)user_data;
    GstSample *sample = gst_app_sink_pull_sample(object);
    GstBuffer *buffer = gst_sample_get_buffer(sample);
    GstMapInfo map;
    gst_buffer_map (buffer, &map, GST_MAP_READ);

    curr_pts = GST_BUFFER_PTS (buffer);
    pts_diff = curr_pts - prev_pts_ls;

    if (!streaming) {
        if (kinesis_req_cam_id == context->cam_pos) {
            LOG_D(log_tag[context->cam_pos], "Time interval is: %llu, pts diff: %llu, prev pts: %llu", MIN_INTRA_FRAME_INTERVAL_LS, pts_diff, prev_pts_ls);
            if (first_cb_ls || (pts_diff > MIN_INTRA_FRAME_INTERVAL_LS)) {
                first_cb_ls = false;
                prev_pts_ls = curr_pts;
                GstBuffer *app_buffer = gst_buffer_copy_deep(buffer);
                ret = gst_app_src_push_buffer((GstAppSrc*)context->kinesis_app_src, app_buffer);
            }
        }
    }

    if (map.data != nullptr) {
        gst_buffer_unmap(buffer, &map);
    }
    if (sample != nullptr) {
        gst_sample_unref(sample);
    }
}

static void livestreaming_cb(GstAppSink *object, gpointer user_data)
{
    // check if context camera position is matching with kinesis req; change camera
    unsigned int kinesis_ret = -1;
    cam_record_ctxt *context = (cam_record_ctxt *)user_data;
    GstSample *sample = gst_app_sink_pull_sample(object);
    GstBuffer *buffer = gst_sample_get_buffer(sample);
    GstMapInfo map;
    gst_buffer_map (buffer, &map, GST_MAP_READ);

#if 0
	FILE *fptr1 = NULL;
	fptr1 = fopen("/home/iriscli/files/aws_kvs_0001.h264", "a+");
	if (fptr1 != NULL) {
		fwrite((BYTE *)map.data, sizeof(char), map.size, fptr1);
		fclose(fptr1);
	}
	LOG_I(log_tag[context->cam_pos], "Calling putKinesisVideoFrame\n");
#endif
    // if cam pos == outward or inward, need to check
    // if(g_track_id_live[track_info_.camera_id] == track_id) {
      // When streaming enabled, fill kinesis stream info
      if(!streaming)
      {
        if(kinesis_req_cam_id == context->cam_pos)
        {
          if(map.data != NULL)
          {
            if(context->kinesis_stream_info.frame.frameData == NULL)
              context->kinesis_stream_info.frame.frameData = (BYTE *) calloc(sizeof(char), MAX_FRAME_BUFFER_SIZE);

            if(map.size > MAX_FRAME_BUFFER_SIZE)
            {
              free(context->kinesis_stream_info.frame.frameData);
              context->kinesis_stream_info.frame.frameData = (BYTE *) calloc(sizeof(char), map.size);
            }
            if (context->kinesis_stream_info.frame.frameData != NULL) {
                if (context->privacy) {
                    // we are sending black iframe whenever privacy is enabled
                    // but once privacy is disabled, we have to wait for the next key frame for the normal frame
                    // so prevFrameState helps us to check when do we need to wait.
                    prevFrameState = INVALID_FRAME;
                    memcpy((context->kinesis_stream_info.frame.frameData), (BYTE *)black_frame_ls->data, black_frame_ls->size);
                    context->kinesis_stream_info.frame.size = black_frame_ls->size;
                }
                else {
                    //privacy is disabled, so we can send the actual frame but still have to wauit for the next key frame 
                    //so till then we will send black frame
                    if (prevFrameState == INVALID_FRAME && GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT)) {
                        memcpy((context->kinesis_stream_info.frame.frameData), (BYTE *)black_frame_ls->data, black_frame_ls->size);
                        context->kinesis_stream_info.frame.size = black_frame_ls->size;
                    }
                    else {
                        //sending the actual frame
                        prevFrameState = VALID_FRAME;
                        memcpy(context->kinesis_stream_info.frame.frameData, (BYTE *)map.data, map.size);
                        context->kinesis_stream_info.frame.size = map.size;
                    }
                }
            }
            else {
                LOG_E(log_tag[context->cam_pos], "Failed to allocate the heap memory\n");
            }
          }
          if (context->kinesis_stream_info.first_time == true) {
              context->kinesis_stream_info.frame.version = FRAME_CURRENT_VERSION;
              context->kinesis_stream_info.frame.trackId = DEFAULT_VIDEO_TRACK_ID;
              context->kinesis_stream_info.frame.duration = HUNDREDS_OF_NANOS_IN_A_SECOND / live_streaming_param.fps;
              LOG_I(log_tag[context->cam_pos], "setting stream info params fps %d\n", live_streaming_param.fps);
              context->kinesis_stream_info.frame.index = 0;
              context->kinesis_stream_info.first_time = false;
          }

          bool delta = GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT);

          FRAME_FLAGS kinesis_video_flags = delta ? FRAME_FLAG_NONE : FRAME_FLAG_KEY_FRAME;

          if(CHECK_FRAME_FLAG_KEY_FRAME(kinesis_video_flags)) {
            // LOG_I(log_tag[context->cam_pos], ">>>>> Key frame is available\n");
            context->kinesis_stream_info.frame.flags = FRAME_FLAG_KEY_FRAME;
          }
          else {
            context->kinesis_stream_info.frame.flags = FRAME_FLAG_NONE;
          }
#if 0
          if(context->cam_pos == CAMERA_POSITION_FRONT){
            FILE *fptr = NULL;
            fptr = fopen("/home/iriscli/files/aws_kvs_0000.h264", "a+");
            if(fptr != NULL)
            {
                fwrite(context->kinesis_stream_info.frame.frameData, sizeof(char), map.size, fptr);
                fclose(fptr);
            }
          }
          else if(context->cam_pos == CAMERA_POSITION_BACK){
            FILE *fptr = NULL;
            fptr = fopen("/home/iriscli/files/aws_kvs_0001.h264", "a+");
            if(fptr != NULL)
            {
                fwrite(context->kinesis_stream_info.frame.frameData, sizeof(char), map.size, fptr);
                fclose(fptr);
            }
          }
        //   FILE *fptr = NULL;
        //   fptr = fopen("/home/iriscli/files/aws_kvs_0001.h264", "a+");
        //   if(fptr != NULL)
        //   {
        //     fwrite(context->kinesis_stream_info.frame.frameData, sizeof(char), map.size, fptr);
        //     fclose(fptr);
        //   }
          LOG_I(log_tag[context->cam_pos], "Calling putKinesisVideoFrame\n");
#endif
          context->kinesis_stream_info.frame.decodingTs = static_cast<UINT64>(buffer->dts) / DEFAULT_TIME_UNIT_IN_NANOS;
          context->kinesis_stream_info.frame.presentationTs = static_cast<UINT64>(buffer->pts) / DEFAULT_TIME_UNIT_IN_NANOS;
          if ((kinesis_dual_streaming && file_is_present(TMP_KINESIS_STREAMING_INWARD)) || !kinesis_dual_streaming) {
              kinesis_ret = putKinesisVideoFrame(context->kinesis_stream_info.streamHandle, &context->kinesis_stream_info.frame);
              if (STATUS_SUCCESS != kinesis_ret) {
                  LOG_I(log_tag[context->cam_pos], "putKinesisVideoFrame status %d\n",kinesis_ret);
              }
          }
          context->kinesis_stream_info.frame.index++;
        }
      }
      else
      {
        context->kinesis_stream_info.frameIndex = 0;
        context->kinesis_stream_info.frame.flags = FRAME_FLAG_NONE;
        context->kinesis_stream_info.first_time = false;
      }

    // }
    if (map.data != nullptr) {
        gst_buffer_unmap(buffer, &map);
    }
    if (sample != nullptr) {
        gst_sample_unref(sample);
    }
}

void *cam_check_thread(void *arg)
{
    unsigned long int curr_time = g_get_monotonic_time ();
    unsigned long int prev_time = g_get_monotonic_time ();
    float fps;

    // Initial cam check interval is more to accommodate delay in starting the camera pipelines
    int cam_check_interval = INIT_CAM_CRASH_THREAD_INTERVAL_IN_SECS;
    while (1) {
        sleep(cam_check_interval);
        if (exit_cam_check_thread == true) {
            LOG_I(log_tag[CAMERA_POSITION_FRONT], "Exiting cam_check_thread.......");
            cam_check_thread_created = false;
            exit_cam_check_thread = false;
            return NULL;
        }
        cam_check_interval = CAM_CRASH_THREAD_INTERVAL_IN_SECS;
        curr_time = g_get_monotonic_time ();
        obj_context_t *obj = NULL;
        cam_crash_status_t *error_status = NULL;

        if (gst_cam_ctxt[CAMERA_POSITION_FRONT].app_cb != NULL) {
            obj = (obj_context_t *)gst_cam_ctxt[CAMERA_POSITION_FRONT].app_cb;
            if (obj != NULL)
                error_status = (cam_crash_status_t *)obj->app_error_ctxt;

            g_mutex_lock(&gst_cam_ctxt[CAMERA_POSITION_FRONT].cam_frame_mutex);

            if (gst_cam_ctxt[CAMERA_POSITION_FRONT].cam_frame_count == 0) {
                LOG_E(log_tag[CAMERA_POSITION_FRONT], "No frames generated from camera, do error callback");
                if (error_status != NULL)
                    error_status->status[CAMERA_POSITION_FRONT] = true;
            } else if (gst_cam_ctxt[CAMERA_POSITION_FRONT].cam_frame_count != 0) {
                LOG_I(log_tag[CAMERA_POSITION_FRONT], "Frame count recieved: %d ",gst_cam_ctxt[CAMERA_POSITION_FRONT].cam_frame_count);

                // time diff is in micro seconds
                fps = ( gst_cam_ctxt[CAMERA_POSITION_FRONT].cam_frame_count * 1000 * 1000.0 ) / (curr_time - prev_time);
                if ((fps < FPS_THRESHOLD_FOR_CAM_CRASH) && (error_status != NULL)) {
                    if (error_status != NULL)
                      error_status->status[CAMERA_POSITION_FRONT] = true;

                    LOG_I(log_tag[CAMERA_POSITION_FRONT], "cam is not giving frames, do error callback: count = %d, fps = %f", gst_cam_ctxt[CAMERA_POSITION_FRONT].cam_frame_count, fps);
                }
                gst_cam_ctxt[CAMERA_POSITION_FRONT].cam_frame_count = 0;
            }

            g_mutex_unlock(&gst_cam_ctxt[CAMERA_POSITION_FRONT].cam_frame_mutex);
        }

        if (error_status->status[CAMERA_POSITION_FRONT] == true) {
            if (NULL != gst_cam_ctxt[CAMERA_POSITION_FRONT].event_cb) {
                gst_cam_ctxt[CAMERA_POSITION_FRONT].event_cb(GST_RECORD_EVENT_PIPELINE_ERR, gst_cam_ctxt[CAMERA_POSITION_FRONT].app_cb);
            }
        }
        prev_time = curr_time;
    }
}

void overrun_handler_queue_rt (void *queue, gpointer user_data)
{
    cam_record_ctxt *context = (cam_record_ctxt *)(user_data);
    LOG_E(log_tag[context->cam_pos], "Overrun for RT Queue");
}

void overrun_handler_queue_appsink_rt (void *queue, gpointer user_data)
{
    cam_record_ctxt *context = (cam_record_ctxt *)(user_data);
    LOG_E(log_tag[context->cam_pos], "Overrun for RT appsink queue");
}

static bool monotonic_raw_ns_to_epoch_ns (uint64_t raw_time_ns, uint64_t *epoch_ns) {
    uint64_t cur_raw_ns = get_system_monotonic_time_ns ();
    uint64_t cur_epoch_ns = get_system_time_ns ();
    int64_t raw_diff_ns = 0;

    raw_diff_ns = cur_raw_ns - raw_time_ns;
    //LOG_D (TAG, "RAW diff ns : %lld", raw_diff_ns);
    *epoch_ns = cur_epoch_ns - raw_diff_ns;
    return true;
}

int32_t file_fd;
static int fcount = 0;
int32_t file_fd_ld;
int32_t file_fd_dp;
int32_t file_fd_ea;

volatile static bool session_change = false;
volatile static bool session_change_ld = false;
volatile static bool session_change_dp = false;
volatile static bool session_change_ea = false;

volatile static int64_t session_count = 0;

/* Reference:
 * https://github.com/csimmonds/periodic-threads/blob/master/timerfd.c
 */
void* session_change_thread(void *args) {
    int ret;
    uint32_t sec;
    struct itimerspec itval;
    uint64_t missed;

    if (!exit_session_change_thread) {
        session_change = false;
        session_change_ld = false;
        session_change_dp = false;
        session_change_ea = false;
    } else {
        exit_session_change_thread = false;
    }

    /* Create the timer */
    timer_fd = timerfd_create(CLOCK_MONOTONIC, 0);
    if (timer_fd == -1) {
        LOG_E(log_tag[0], "%s: timer not created", __func__);
        return NULL;
    }

    /* Make the timer periodic */
    sec = 60;
    itval.it_interval.tv_sec = sec;
    itval.it_interval.tv_nsec = 0;
    itval.it_value.tv_sec = sec;
    itval.it_value.tv_nsec = 0;
    ret = timerfd_settime(timer_fd, 0, &itval, NULL);

    while (!exit_session_change_thread) {
        /* Wait for the next timer event. If we have missed any the
           number is written to "missed" */
        ret = read(timer_fd, &missed, sizeof(missed));
        LOG_I(log_tag[0], "%s: hitting timer \n", __func__);

        session_change = true;
        fname_updated = false;
    }

    return NULL;
}

static void appsink_nrt_cb(GstElement *appsink, gpointer user_data)
{
    uint64_t buf_pts = 0;
    uint64_t epoch_time_ns = 0;
    uint64_t raw_time_ns = 0;
    uint64_t frame_num;
    size_t encrypted_buffer_len=0;
    unsigned char* encrypted_buffer = NULL;

    cam_record_ctxt *context = (cam_record_ctxt *)user_data;

    GstAppSink* app_sink = (GstAppSink*) appsink;
    GstSample *sample = gst_app_sink_pull_sample(app_sink);
    GstBuffer *buffer = gst_sample_get_buffer(sample);
    GstMapInfo map;
    gst_buffer_map (buffer, &map, GST_MAP_READ);

    //Proceed further only if both PTS and DTS are valid
    if (!((GST_BUFFER_PTS_IS_VALID(buffer)) && (GST_BUFFER_DTS_IS_VALID(buffer)))) {
        LOG_E(log_tag[context->cam_pos], "PTS or DTS not valid. pts(%lld) dts(%lld)", GST_BUFFER_PTS(buffer), GST_BUFFER_DTS(buffer));
        gst_buffer_unmap(buffer, &map);
        gst_sample_unref(sample);
        return;
    }

    gchar filename[GSTREAMER_NAME_LENGTH_MAX];

    buf_pts = GST_BUFFER_PTS (buffer);

    context->total_frames_written++;
    uint64_t pts_lookup = buf_pts;
    if (buf_pts == 0) {
        buff_num_with_zero_pts_hd++;
        pts_lookup = buff_num_with_zero_pts_hd;
        LOG_I (log_tag[context->cam_pos], "appsink_nrt_cb: Look for PTS 0 in the map with lookup_index: %llu", pts_lookup);
    }

    int ret = get_timestamps_from_pts(pts_lookup, &raw_time_ns, &epoch_time_ns, &frame_num);

    // If failed to get timestamp from pts
    if (ret == -1)
        LOG_E(log_tag[context->cam_pos], "appsink_nrt_cb: PTS %llu for framenumber %lu not found in map", pts_lookup, context->total_frames_written);

    if (buf_pts == 0)
        LOG_I (log_tag[context->cam_pos], "appsink_nrt_cb: PTS 0 found in map with lookup_index: %llu, raw_time_ns: %llu, epoch_time_ns: %llu", pts_lookup, raw_time_ns, epoch_time_ns);

    context->abs_raw_time = raw_time_ns / 1000; //abs_raw_time in microseconds
    if (epoch_time_ns != 0)
        context->abs_epoch_time = epoch_time_ns / 1000; //abs_epoch_time in microseconds
    else
        context->abs_epoch_time = get_system_time_ns() / 1000; //abs_epoch_time in microseconds

    if (context->frame_write_cb) {
        //abs_raw_time is in nano seconds.
        if (context->total_frames_written % 150 == 2) {
            LOG_I (log_tag[context->cam_pos],"MUX: BUFFER PTS:%llu\t BUFFER RAWTS: %llu\t BUFFER EPOCH TS: %llu", buf_pts, context->abs_raw_time, context->abs_epoch_time);
        }
        context->frame_write_cb(frame_num, context->abs_raw_time, context->abs_epoch_time, context->app_cb);
    }

    if (first_cb_hd) {
        first_cb_hd = false;

        g_mutex_lock(&context->session_change_mutex);
        if (!fname_updated) {
            uint64_t session_first_frame_epoch_time_ms = context->abs_epoch_time / 1000;
            LOG_I(log_tag[context->cam_pos], "session_first_frame_epoch_time_ms: %llu", session_first_frame_epoch_time_ms);
            new_session_filename = context->fname_cb(context->app_fname, session_first_frame_epoch_time_ms);
            fname_updated = true;
            g_cond_signal(&(context->session_change_cond));
        }
        g_mutex_unlock(&context->session_change_mutex);

        if (file_fd > 0) {
            close(file_fd);
            file_fd = -1;
            LOG_I(log_tag[context->cam_pos], "file_fd closed");
        }
        file_fd = open(new_session_filename, O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (file_fd <= 0) {
            LOG_I(log_tag[context->cam_pos], "appsink_nrt_cb: File open failed! %s", new_session_filename);
            return;
        }

        memset(context->next_session_fname, 0x0, GSTREAMER_NAME_LENGTH_MAX);
        nd_strncpy(context->next_session_fname, new_session_filename, GSTREAMER_NAME_LENGTH_MAX);

        LOG_I(log_tag[context->cam_pos], "DEBUG_ANNOTATION: Filename HD: %s", context->next_session_fname);

        pthread_create(&session_th, NULL, &session_change_thread, NULL);

        //Give Record start callback
        context->session_start_pts = buf_pts;
        context->mux_start_ts = get_system_time();
        LOG_I (log_tag[context->cam_pos], "DEBUG_ANNOTATION: session_start_pts = %lld, raw_time_us: %ld, epoch_time_us: %ld", context->session_start_pts, context->abs_raw_time, context->abs_epoch_time);
        context->record_cb(GST_RECORD_FILE_START,
                           context->abs_raw_time, context->abs_epoch_time,
                           context->session_start_pts, context->app_cb, 0, false, NULL);
    }

    if ((session_change == true) && !GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT)) {
        session_change = false;
        context->mux_end_ts = get_system_time();
        LOG_I(log_tag[context->cam_pos], "DEBUG_ANNOTATION: record duration: %ld", (context->mux_end_ts - context->mux_start_ts));
        context->record_cb(GST_RECORD_FILE_STOP,
                context->abs_raw_time, context->abs_epoch_time, context->session_start_pts,
                context->app_cb, context->session_frame_count, false, context->next_session_fname);

        g_mutex_lock(&context->cam_frame_mutex);
        context->session_frame_count = 0;
        g_mutex_unlock(&context->cam_frame_mutex);

        if (!fname_updated) {
            uint64_t session_first_frame_epoch_time_ms = context->abs_epoch_time / 1000;
            LOG_I(log_tag[context->cam_pos], "session_first_frame_epoch_time_ms: %llu", session_first_frame_epoch_time_ms);
            new_session_filename = context->fname_cb(context->app_fname, session_first_frame_epoch_time_ms);
            fname_updated = true;
        }

        session_change_ld = true;
        session_change_dp = true;

        if (ea_outward_enabled == 1) {
            session_count++;
            if ((session_count % ea_freq_session_wise) == 0) {
                LOG_I(log_tag[context->cam_pos], "New session for Event Access Preview Feature");
                session_change_ea = true;
            }
        }

        if (file_fd > 0) {
            close(file_fd);
            file_fd = -1;
            LOG_I(log_tag[context->cam_pos], "file_fd closed");
        }
        file_fd = open(new_session_filename, O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (file_fd <= 0) {
            LOG_I(log_tag[context->cam_pos], "appsink_nrt_cb: File open failed! %s", new_session_filename);
            return;
        }

        memset(context->next_session_fname, 0x0, GSTREAMER_NAME_LENGTH_MAX);
        strncpy(context->next_session_fname, new_session_filename, GSTREAMER_NAME_LENGTH_MAX);

        LOG_I(log_tag[context->cam_pos], "DEBUG_ANNOTATION: Filename HD: %s", context->next_session_fname);

        if (exit_session_change_thread) {
            pthread_create(&session_th, nullptr, &session_change_thread, nullptr);
        }

        //Give Record start callback
        context->session_start_pts = buf_pts;
        context->mux_start_ts = get_system_time();
        LOG_I (log_tag[context->cam_pos], "DEBUG_ANNOTATION: session_start_pts = %lld, raw_time_us: %ld, epoch_time_us: %ld", context->session_start_pts, context->abs_raw_time, context->abs_epoch_time);
        context->record_cb(GST_RECORD_FILE_START,
                           context->abs_raw_time, context->abs_epoch_time,
                           context->session_start_pts, context->app_cb, 0, false, NULL);
    }
    //write(file_fd, map.data, map.size);

    if (stream_encryption) {
        size_t file_len1 =  nd_stream_encryption(map.data, map.size, &encrypted_buffer,&encrypted_buffer_len);
        LOG_D(log_tag[context->cam_pos],"%s:%d nd_stream_encryption return val:%d",__func__,__LINE__,file_len1);
        if (encrypted_buffer) {
            write(file_fd, encrypted_buffer, encrypted_buffer_len);
            free(encrypted_buffer);
            LOG_D(log_tag[context->cam_pos],"%s:%d Freeing the encrypted buffer memory",__func__,__LINE__);
        }
    } else {
        write(file_fd, map.data, map.size);
    }

    gst_buffer_unmap(buffer, &map);
    gst_sample_unref(sample);
}

static void appsink_tc_cb(GstElement *appsink, gpointer user_data)
{
    uint64_t buf_pts_ld = 0;
    uint64_t epoch_time_ns_ld = 0;
    uint64_t epoch_time_ms_ld = 0;
    uint64_t raw_time_ns_ld = 0;
    uint64_t frame_num_ld;
    size_t encrypted_buffer_len=0;
    unsigned char* encrypted_buffer = NULL;

    cam_record_ctxt *context = (cam_record_ctxt *)user_data;

    GstAppSink* app_sink = (GstAppSink*) appsink;
    GstSample *sample = gst_app_sink_pull_sample(app_sink);
    GstBuffer *buffer = gst_sample_get_buffer(sample);
    GstMapInfo map;
    gst_buffer_map (buffer, &map, GST_MAP_READ);

    //Proceed further only if both PTS and DTS are valid
    if (!((GST_BUFFER_PTS_IS_VALID(buffer)) && (GST_BUFFER_DTS_IS_VALID(buffer)))) {
        LOG_E(log_tag[context->cam_pos], "PTS or DTS not valid. pts_ld(%lld) dts_ld(%lld)", GST_BUFFER_PTS(buffer), GST_BUFFER_DTS(buffer));
        gst_buffer_unmap(buffer, &map);
        gst_sample_unref(sample);
        return;
    }

    gchar filename[GSTREAMER_NAME_LENGTH_MAX];

    buf_pts_ld = GST_BUFFER_PTS (buffer);

    uint64_t pts_lookup_ld = buf_pts_ld;
    if (buf_pts_ld == 0) {
        buff_num_with_zero_pts_ld++;
        pts_lookup_ld = buff_num_with_zero_pts_ld;
        LOG_I (log_tag[context->cam_pos], "appsink_tc_cb: Look for PTS 0 in the map with lookup_index: %llu", pts_lookup_ld);
    }

    int ret = get_timestamps_from_pts(pts_lookup_ld, &raw_time_ns_ld, &epoch_time_ns_ld, &frame_num_ld);
    if (ret == -1)
        LOG_E(log_tag[context->cam_pos], "appsink_tc_cb: PTS %llu not found in map", pts_lookup_ld);

    if (buf_pts_ld == 0)
        LOG_I (log_tag[context->cam_pos], "appsink_tc_cb: PTS 0 found in map with lookup_index: %llu, raw_time_ns: %llu, epoch_time_ns: %llu", pts_lookup_ld, raw_time_ns_ld, epoch_time_ns_ld);

    if (first_cb_ld) {
        first_cb_ld = false;
        while (!fname_updated) {
            // Wait here till next session filename gets updated in HD callback.
            usleep(10000); //Sleep for 10 ms

        }
        if (file_fd_ld > 0) {
            close(file_fd_ld);
            file_fd_ld = -1;
            LOG_I(log_tag[context->cam_pos], "file_fd_ld closed");
        }
        nd_strncpy(filename, new_session_filename, GSTREAMER_NAME_LENGTH_MAX);
        strcat(filename, ld_extn);
        file_fd_ld = open(filename, O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (file_fd_ld <= 0) {
            LOG_I(log_tag[context->cam_pos], "appsink_tc_cb: File open failed! %s", filename);
            return;
        }

        memset(context->next_session_fname_ld, 0x0, GSTREAMER_NAME_LENGTH_MAX);
        strncpy(context->next_session_fname_ld, new_session_filename, GSTREAMER_NAME_LENGTH_MAX);
        strcat(context->next_session_fname_ld, ld_extn);

        LOG_I(log_tag[context->cam_pos], "DEBUG_ANNOTATION: Filename LD: %s", context->next_session_fname_ld);

        context->mux_start_ts_ld = get_system_time();
        context->session_start_pts_ld = buf_pts_ld;
        LOG_I (log_tag[context->cam_pos], "DEBUG_ANNOTATION: session_start_pts_ld: %lld, raw_time_ns_ld: %ld, epoch_time_ns_ld: %ld", context->session_start_pts_ld, raw_time_ns_ld, epoch_time_ns_ld);

        epoch_time_ms_ld = get_system_time();
        if (epoch_time_ns_ld != 0) {
            epoch_time_ms_ld = epoch_time_ns_ld / (1000 * 1000);
        }
        //Give timestamp callback
        context->timestamp_cb(epoch_time_ms_ld, context->session_start_pts_ld, context->app_cb);
    }

    if ((session_change_ld == true) && !GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT)) {

        session_change_ld = false;
        context->mux_end_ts_ld = get_system_time();
        LOG_I(log_tag[context->cam_pos], "DEBUG_ANNOTATION: LD record duration: %ld", (context->mux_end_ts_ld - context->mux_start_ts_ld));
        LOG_I(log_tag[context->cam_pos], "GST_RECORD_FILE_STOP called with session_fname: %s", context->next_session_fname_ld);
        //Sending RECORD_STOP callback for LD files
        context->record_cb(GST_RECORD_FILE_STOP,
                context->abs_raw_time, context->abs_epoch_time, context->session_start_pts_ld,
                context->app_cb, context->session_frame_count, true, context->next_session_fname_ld);

        if (file_fd_ld > 0) {
            close(file_fd_ld);
            file_fd_ld = -1;
            LOG_I(log_tag[context->cam_pos], "file_fd_ld closed");
        }
        nd_strncpy(filename, new_session_filename, GSTREAMER_NAME_LENGTH_MAX);
        strcat(filename, ld_extn);
        file_fd_ld = open(filename, O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (file_fd_ld <= 0) {
            LOG_I(log_tag[context->cam_pos], "appsink_tc_cb: File open failed! %s", filename);
            return;
        }

        memset(context->next_session_fname_ld, 0x0, GSTREAMER_NAME_LENGTH_MAX);
        nd_strncpy(context->next_session_fname_ld, new_session_filename, GSTREAMER_NAME_LENGTH_MAX);
        strcat(context->next_session_fname_ld, ld_extn);

        LOG_I(log_tag[context->cam_pos], "DEBUG_ANNOTATION: Filename LD: %s", context->next_session_fname_ld);

        context->mux_start_ts_ld = get_system_time();
        context->session_start_pts_ld = buf_pts_ld;
        LOG_I (log_tag[context->cam_pos], "DEBUG_ANNOTATION: session_start_pts_ld: %lld, raw_time_ns_ld: %ld, epoch_time_ns_ld: %ld", context->session_start_pts_ld, raw_time_ns_ld, epoch_time_ns_ld);

        epoch_time_ms_ld = get_system_time();
        if (epoch_time_ns_ld != 0) {
            epoch_time_ms_ld = epoch_time_ns_ld / (1000 * 1000);
        }
        //Give timestamp callback
        context->timestamp_cb(epoch_time_ms_ld, context->session_start_pts_ld, context->app_cb);
    }

    if (stream_encryption) {
        //write(file_fd_ld, map.data, map.size);
        size_t file_len1 =  nd_stream_encryption(map.data, map.size, &encrypted_buffer,&encrypted_buffer_len);
        LOG_D(log_tag[context->cam_pos],"%s:%d nd_stream_encryption return val:%d",__func__,__LINE__,file_len1);

        if (encrypted_buffer) {
            write(file_fd_ld, encrypted_buffer, encrypted_buffer_len);
            free(encrypted_buffer);
            LOG_D(log_tag[context->cam_pos],"%s:%d Freeing the encrypted buffer memory",__func__,__LINE__);
        }
    } else {
        write(file_fd_ld, map.data, map.size);
    }
    gst_buffer_unmap(buffer, &map);
    gst_sample_unref(sample);
}

static void appsink_dp_cb(GstElement *appsink, gpointer user_data)
{
    uint64_t buf_pts_dp = 0;
    uint64_t epoch_time_ns_dp = 0;
    uint64_t epoch_time_ms_dp = 0;
    uint64_t raw_time_ns_dp = 0;
    uint64_t frame_num_dp;
    size_t encrypted_buffer_len=0;
    unsigned char* encrypted_buffer = NULL;

    cam_record_ctxt *context = (cam_record_ctxt *)user_data;

    GstAppSink* app_sink = (GstAppSink*) appsink;
    GstSample *sample = gst_app_sink_pull_sample(app_sink);
    GstBuffer *buffer = gst_sample_get_buffer(sample);
    GstMapInfo map;
    gst_buffer_map (buffer, &map, GST_MAP_READ);

    gchar filename[GSTREAMER_NAME_LENGTH_MAX];

    buf_pts_dp = GST_BUFFER_PTS (buffer);

    uint64_t pts_lookup_dp = buf_pts_dp;
    if (buf_pts_dp == 0) {
        buff_num_with_zero_pts_dp++;
        pts_lookup_dp = buff_num_with_zero_pts_dp;
        LOG_I (log_tag[context->cam_pos], "appsink_dp_cb: Look for PTS 0 in the map with lookup_index: %llu", pts_lookup_dp);
    }

    int ret = get_timestamps_from_pts(pts_lookup_dp, &raw_time_ns_dp, &epoch_time_ns_dp, &frame_num_dp);
    if (ret == -1)
        LOG_E(log_tag[context->cam_pos], "appsink_dp_cb: PTS %llu not found in map", pts_lookup_dp);

    if (buf_pts_dp == 0)
        LOG_I (log_tag[context->cam_pos], "appsink_dp_cb: PTS 0 found in map with lookup_index: %llu, raw_time_ns: %llu, epoch_time_ns: %llu", pts_lookup_dp, raw_time_ns_dp, epoch_time_ns_dp);

    if (first_cb_dp) {
        first_cb_dp = false;
        while (!fname_updated) {
            // Wait here till next session filename gets updated in HD callback.
            usleep(10000); //Sleep for 10 ms
        }
        if (file_fd_dp > 0) {
            close(file_fd_dp);
            file_fd_dp = -1;
            LOG_I(log_tag[context->cam_pos], "file_fd_dp closed");
        }
        nd_strncpy(filename, new_session_filename, GSTREAMER_NAME_LENGTH_MAX);
        strcat(filename, dp_extn);
        file_fd_dp = open(filename, O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (file_fd_dp <= 0) {
            LOG_I(log_tag[context->cam_pos], "appsink_dp_cb: Failed to open the file %s", filename);
            return;
        }

        memset(context->next_session_fname_dp, 0x0, GSTREAMER_NAME_LENGTH_MAX);
        strncpy(context->next_session_fname_dp, new_session_filename, GSTREAMER_NAME_LENGTH_MAX);
        strcat(context->next_session_fname_dp, dp_extn);

        LOG_I(log_tag[context->cam_pos], "DEBUG_ANNOTATION: Filename DP: %s", context->next_session_fname_dp);

        context->mux_start_ts_dp = get_system_time();
        context->session_start_pts_dp = buf_pts_dp;
        LOG_I (log_tag[context->cam_pos], "DEBUG_ANNOTATION: session_start_pts_dp: %lld, raw_time_ns_dp: %ld, epoch_time_ns_dp: %ld", context->session_start_pts_dp, raw_time_ns_dp, epoch_time_ns_dp);

        epoch_time_ms_dp = get_system_time();
        if (epoch_time_ns_dp != 0) {
            epoch_time_ms_dp = epoch_time_ns_dp / (1000 * 1000);
        }
        //Give data product callback
        context->data_prod_cb(epoch_time_ms_dp, context->session_start_pts_ld, context->app_cb);
    }


    if ((session_change_dp == true) && !GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT)) {

        session_change_dp = false;
        context->mux_end_ts_dp = get_system_time();
        LOG_I(log_tag[context->cam_pos], "DEBUG_ANNOTATION: DP record duration: %ld", (context->mux_end_ts_dp - context->mux_start_ts_dp));

        if (file_fd_dp > 0) {
            close(file_fd_dp);
            file_fd_dp = -1;
            LOG_I(log_tag[context->cam_pos], "file_fd_dp closed");
        }
        nd_strncpy(filename, new_session_filename, GSTREAMER_NAME_LENGTH_MAX);
        strcat(filename, dp_extn);
        file_fd_dp = open(filename, O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (file_fd_dp <= 0) {
            LOG_I(log_tag[context->cam_pos], "appsink_dp_cb: Failed to open the file: %s", filename);
            return;
        }

        memset(context->next_session_fname_dp, 0x0, GSTREAMER_NAME_LENGTH_MAX);
        nd_strncpy(context->next_session_fname_dp, new_session_filename, GSTREAMER_NAME_LENGTH_MAX);
        strcat(context->next_session_fname_dp, dp_extn);

        LOG_I(log_tag[context->cam_pos], "DEBUG_ANNOTATION: Filename DP: %s", context->next_session_fname_dp);

        context->mux_start_ts_dp = get_system_time();
        context->session_start_pts_dp = buf_pts_dp;
        LOG_I (log_tag[context->cam_pos], "DEBUG_ANNOTATION: session_start_pts_dp: %lld, raw_time_ns_dp: %ld, epoch_time_ns_dp: %ld", context->session_start_pts_dp, raw_time_ns_dp, epoch_time_ns_dp);

        epoch_time_ms_dp = get_system_time();
        if (epoch_time_ns_dp != 0) {
            epoch_time_ms_dp = epoch_time_ns_dp / (1000 * 1000);
        }
        //Give data product callback
        context->data_prod_cb(epoch_time_ms_dp, context->session_start_pts_dp, context->app_cb);
    }

    if (stream_encryption) {
        //write(file_fd_ld, map.data, map.size);
        size_t file_len1 = nd_stream_encryption(map.data, map.size, &encrypted_buffer, &encrypted_buffer_len);
        LOG_D(log_tag[context->cam_pos], "%s:%d nd_stream_encryption return val: %d", __func__, __LINE__, file_len1);

        if (encrypted_buffer) {
            write(file_fd_dp, encrypted_buffer, encrypted_buffer_len);
            free(encrypted_buffer);
            LOG_D(log_tag[context->cam_pos], "%s:%d Freeing the encrypted buffer memory", __func__, __LINE__);
        }
    } else {
        write(file_fd_dp, map.data, map.size);
    }
    gst_buffer_unmap(buffer, &map);
    gst_sample_unref(sample);
}

static void appsink_ea_cb(GstElement *appsink, gpointer user_data)
{
    size_t encrypted_buffer_len=0;
    unsigned char* encrypted_buffer = NULL;

    cam_record_ctxt *context = (cam_record_ctxt *)user_data;

    GstAppSink* app_sink = (GstAppSink*) appsink;
    GstSample *sample = gst_app_sink_pull_sample(app_sink);
    GstBuffer *buffer = gst_sample_get_buffer(sample);
    GstMapInfo map;
    gst_buffer_map (buffer, &map, GST_MAP_READ);

    gchar filename_ea[GSTREAMER_NAME_LENGTH_MAX];

    if (first_cb_ea) {
        first_cb_ea = false;

        g_mutex_lock(&context->session_change_mutex);
        while (!fname_updated) {
            LOG_I(log_tag[context->cam_pos], "appsink_ea_cb: First call, waiting for session filename to get generated");
            g_cond_wait (&(context->session_change_cond), &(context->session_change_mutex));
        }
        LOG_I(log_tag[context->cam_pos], "appsink_ea_cb: First session filename got generated");
        g_mutex_unlock(&context->session_change_mutex);
    }

    if (file_fd_ea > 0) {
        close(file_fd_ea);
        file_fd_ea = -1;
        LOG_I(log_tag[context->cam_pos], "appsink_ea_cb: file_fd_ea closed");
    }
    string fname = new_session_filename;
    /* Remove .mp4 from video filename as ea_image filename does not contain .mp4 */
    int dot = fname.find(".mp4");
    if (dot != string::npos)
        fname.resize(dot);

    nd_strncpy(filename_ea, fname.c_str(), sizeof(filename_ea));
    strcat(filename_ea, ea_extn);

    file_fd_ea = open(filename_ea, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (file_fd_ea <= 0) {
        LOG_I(log_tag[context->cam_pos], "appsink_ea_cb: Failed to open the file %s", filename_ea);
        gst_buffer_unmap(buffer, &map);
        gst_sample_unref(sample);
        return;
    }

    LOG_I(log_tag[context->cam_pos], "appsink_ea_cb: Filename EA: %s", filename_ea);

    if (stream_encryption) {
        int encryption_status = nd_stream_encryption(map.data, map.size, &encrypted_buffer, &encrypted_buffer_len);
        LOG_D(log_tag[context->cam_pos], "appsink_ea_cb: EA buffer encryption status: %d", encryption_status);

        if (encrypted_buffer) {
            write(file_fd_ea, encrypted_buffer, encrypted_buffer_len);
            free(encrypted_buffer);
        } else {
            LOG_E(log_tag[context->cam_pos], "appsink_ea_cb: EA buffer encryption has failed, writing in file without encryption");
            write(file_fd_ea, map.data, map.size);
        }
    } else {
        write(file_fd_ea, map.data, map.size);
    }
    gst_buffer_unmap(buffer, &map);
    gst_sample_unref(sample);
}

static void appsink_roadfacing_cb(GstAppSink *object, gpointer user_data)
{
    uint64_t curr_pts = 0;

    uint64_t epoch_time_ns = 0;
    uint64_t raw_time_ns = 0;

    cam_record_ctxt *context = (cam_record_ctxt *)user_data;

    GstAppSink* app_sink = (GstAppSink*) object;
    GstSample *sample = gst_app_sink_pull_sample(app_sink);
    GstBuffer *buffer = gst_sample_get_buffer(sample);
    GstMapInfo map;
    gst_buffer_map (buffer, &map, GST_MAP_READ);

    curr_pts = GST_BUFFER_PTS (buffer);

    uint64_t pts_lookup = curr_pts;
    if (curr_pts == 0) {
        buff_num_with_zero_pts_rt ++;
        pts_lookup = buff_num_with_zero_pts_rt;
        LOG_I (log_tag[context->cam_pos], "appsink_roadfacing_cb: Look for PTS 0 in the map with lookup_index: %llu", pts_lookup);
    }

    // It is necessary to get timestamp from map even before checking to decide
    // whether to drop the frame or not because each entry in PTS-TS map has a refcount which needs to
    // become zero inorder to clear that entry off map.
    uint64_t frame_num;
    //LOG_I(log_tag[context->cam_pos], "%s: getting ts from pts: %lld", __func__, pts_lookup);
    if (get_timestamps_from_pts (pts_lookup, &raw_time_ns, &epoch_time_ns, &frame_num) == -1) {
        raw_time_ns = 0;
        LOG_E (log_tag[context->cam_pos],"appsink_roadfacing_cb: PTS %llu not found in map", pts_lookup);
    }

    if (ea_outward_enabled) {
        // In case of first session, first few frames are skipped as camera may not be stable at the start hence may generate green/bright/dark images
        if ((first_forward_ea == true && context->cam_frame_count >= EA_PREVIEW_OUTWARD_FIRST_SESSION_FRAME_SKIP_COUNT) || (session_change_ea == true)) {
            LOG_I(log_tag[context->cam_pos], "appsink_roadfacing_cb: Forwarding frame with PTS: %llu for event access processing", curr_pts);
            first_forward_ea = false;
            session_change_ea = false;
            GstBuffer *app_buffer = gst_buffer_copy_deep(buffer);
            GstFlowReturn gst_flow_ret = gst_app_src_push_buffer((GstAppSrc*)context->event_access_app_src, app_buffer);
            LOG_I(log_tag[context->cam_pos], "appsink_roadfacing_cb: Forwarding frame for event access processing returned %d", gst_flow_ret);
        }
    }

    if (drop_rt_frames == true) {
        LOG_E (log_tag[context->cam_pos], "Dropping outward cam RT frames because shared memory will be recreated.");
        gst_sample_unref(sample);
        gst_buffer_unmap(buffer, &map);
        return;
    }

    LOG_D(log_tag[context->cam_pos], "SHM_DEBUG :: context->outwardcam_yuvbuf_size = %d, map.size = %d", context->outwardcam_yuvbuf_size, map.size);
    if (map.size == context->outwardcam_yuvbuf_size) {
#if 1
        static int count = 0;
        int smb_id = -1;
        int64_t uid = -1;
        int64_t data_len = -1;
        bool bIsExit = false;
        g_mutex_lock(&context->shm_writer_mutex);
        if (context->shm_writer) {
            context->frame_data_ptr = map.data;
            outward_rt_buf_size = map.size;
            count++;
            LOG_D(log_tag[context->cam_pos], "SHM_DEBUG :: calling get_free_smb count = %d", count);
            if (!context->shm_writer->get_free_smb(context, smb_id, uid, data_len, context->cam_pos, bIsExit)) {
                LOG_E(log_tag[context->cam_pos], "failed to write data to smb, dropping frame");

                /* commenting the following lines as there is a possibility of replenishing
                 * the shared memory buffer once the analytics service restarts.
                 */
#if 0
                if (bIsExit) {
                    obj_context_t *obj = NULL;
                    cam_crash_status_t *error_status = NULL;
                    obj = (obj_context_t *)gst_cam_ctxt[CAMERA_POSITION_FRONT].app_cb;
                    if (obj != NULL) {
                        error_status = (cam_crash_status_t *)obj->app_error_ctxt;
                    }
                    error_status->status[CAMERA_POSITION_FRONT] = true;
                    context->event_cb(GST_RECORD_EVENT_SHM_FAILED, context->app_cb);
                }
#endif
            } else {
                LOG_D(log_tag[context->cam_pos], "SHM_DEBUG :: smbid = %d", smb_id);
                uint64_t raw_frame_sent_time_ms = get_system_time();
                context->appsink_cb(GST_BUFFER_PTS(buffer), context->cam_pos, raw_time_ns, epoch_time_ns, raw_frame_sent_time_ms, (int64_t)map.size, smb_id, uid, context->app_cb);
            }
        } else {
            LOG_E(log_tag[context->cam_pos], "shm writer null. so not triggering shm write callback");
        }
        g_mutex_unlock(&context->shm_writer_mutex);
#else
        context->appsink_cb(GST_BUFFER_PTS(buffer), context->cam_pos, raw_time_ns, epoch_time_ns, map.size, (char *)map.data, context->app_cb);
#endif
    } else {
        LOG_E(log_tag[context->cam_pos], "Data buffer received inaccurate.");
    }

    frame_count_rt++;
    if (frame_count_rt >= 150) {
        unsigned long curr_time = g_get_monotonic_time() * 1000;
        LOG_E(log_tag[context->cam_pos], "Delay in after sending to analytics: %llu", curr_time - GST_BUFFER_PTS (buffer));
        frame_count_rt = 0;
    }

    gst_buffer_unmap(buffer, &map);
    gst_sample_unref(sample);

    return;
}

#ifdef PROFILING
static GstPadProbeReturn keep_alive_callback5(GstPad *pad,GstPadProbeInfo *probe,
                                            gpointer user_data)
{

    static int buff_num_with_zero_pts = 0;
    cam_record_ctxt *context = NULL;
    GstBuffer *buffer = NULL;
    static int frame_num5 = 0;
    
    if (user_data == NULL || probe == NULL || pad == NULL) {
        LOG_E("ERROR","invalid param recieved \n");
        return GST_PAD_PROBE_REMOVE;
    }

    context = (cam_record_ctxt *)user_data;

    if (pad != context->keep_alive_pad5) {
        LOG_E(log_tag[context->cam_pos],"Wrong probe id blocked queue source pad \n");
        return GST_PAD_PROBE_REMOVE;
    }

    buffer = GST_PAD_PROBE_INFO_BUFFER (probe);
    if (buffer == NULL) {
        LOG_E (log_tag[context->cam_pos], "Buffer NULL in keep_alive_callback5");
        return GST_PAD_PROBE_REMOVE;
    }

    //If PTS is not valid, skip this call back and wait for next
    if (!GST_BUFFER_PTS_IS_VALID(buffer)) {
        return GST_PAD_PROBE_OK;
    }

    if  (context->cam_pos == CAMERA_POSITION_FRONT)
        frame_num5++;
        
    if  (context->cam_pos == CAMERA_POSITION_FRONT && frame_num5 >= 150)
    {
        unsigned long curr_time = 0;
        curr_time = g_get_monotonic_time()*1000;
        LOG_E(log_tag[context->cam_pos],"Delay in queue appsink:%llu", curr_time-GST_BUFFER_PTS (buffer));
        frame_num5 = 0;
    }

    return GST_PAD_PROBE_OK;
    
}

static GstPadProbeReturn keep_alive_callback4(GstPad *pad,GstPadProbeInfo *probe,
                                            gpointer user_data)
{

    static int buff_num_with_zero_pts = 0;
    cam_record_ctxt *context = NULL;
    GstBuffer *buffer = NULL;
    static int frame_num4 = 0;

    if (user_data == NULL || probe == NULL || pad == NULL) {
        LOG_E("ERROR","invalid param recieved \n");
        return GST_PAD_PROBE_REMOVE;
    }

    context = (cam_record_ctxt *)user_data;

    if (pad != context->keep_alive_pad4) {
        LOG_E(log_tag[context->cam_pos],"Wrong probe id blocked queue source pad \n");
        return GST_PAD_PROBE_REMOVE;
    }

    buffer = GST_PAD_PROBE_INFO_BUFFER (probe);
    if (buffer == NULL) {
        LOG_E (log_tag[context->cam_pos], "Buffer NULL in keep_alive_callback4");
        return GST_PAD_PROBE_REMOVE;
    }

    //If PTS is not valid, skip this call back and wait for next
    if (!GST_BUFFER_PTS_IS_VALID(buffer)) {
        return GST_PAD_PROBE_OK;
    }

    if  (context->cam_pos == CAMERA_POSITION_FRONT)
        frame_num4++;
        
    if  (context->cam_pos == CAMERA_POSITION_FRONT && frame_num4 >= 150)
    {
        unsigned long curr_time = 0;
        curr_time = g_get_monotonic_time()*1000;
        LOG_E(log_tag[context->cam_pos],"Delay in vidconv_rt:%llu", curr_time-GST_BUFFER_PTS (buffer));
        frame_num4 = 0;
    }

    return GST_PAD_PROBE_OK;
    
}

static GstPadProbeReturn keep_alive_callback3(GstPad *pad,GstPadProbeInfo *probe,
                                            gpointer user_data)
{

    static int buff_num_with_zero_pts = 0;
    cam_record_ctxt *context = NULL;
    GstBuffer *buffer = NULL;
    static int frame_num3 = 0;

    if (user_data == NULL || probe == NULL || pad == NULL) {
        LOG_E("ERROR","invalid param recieved \n");
        return GST_PAD_PROBE_REMOVE;
    }

    context = (cam_record_ctxt *)user_data;

    if (pad != context->keep_alive_pad3) {
        LOG_E(log_tag[context->cam_pos],"Wrong probe id blocked queue source pad \n");
        return GST_PAD_PROBE_REMOVE;
    }

    buffer = GST_PAD_PROBE_INFO_BUFFER (probe);
    if (buffer == NULL) {
        LOG_E (log_tag[context->cam_pos], "Buffer NULL in keep_alive_callback3");
        return GST_PAD_PROBE_REMOVE;
    }

    //If PTS is not valid, skip this call back and wait for next
    if (!GST_BUFFER_PTS_IS_VALID(buffer)) {
        return GST_PAD_PROBE_OK;
    }
    
    if  (context->cam_pos == CAMERA_POSITION_FRONT)
        frame_num3++;
        
    if  (context->cam_pos == CAMERA_POSITION_FRONT && frame_num3 >= 150)
    {
        unsigned long curr_time = 0;
        curr_time = g_get_monotonic_time()*1000;
        LOG_E(log_tag[context->cam_pos],"Delay in queue_rt:%llu", curr_time-GST_BUFFER_PTS (buffer));
        frame_num3 = 0;
    }
    
    return GST_PAD_PROBE_OK;
}

static GstPadProbeReturn keep_alive_callback2(GstPad *pad,GstPadProbeInfo *probe,
                                            gpointer user_data)
{

    static int buff_num_with_zero_pts = 0;
    cam_record_ctxt *context = NULL;
    GstBuffer *buffer = NULL;
    static int frame_num2 = 0;

    if (user_data == NULL || probe == NULL || pad == NULL) {
        LOG_E("ERROR","invalid param recieved \n");
        return GST_PAD_PROBE_REMOVE;
    }

    context = (cam_record_ctxt *)user_data;

    if (pad != context->keep_alive_pad2) {
        LOG_E(log_tag[context->cam_pos],"Wrong probe id blocked queue source pad \n");
        return GST_PAD_PROBE_REMOVE;
    }

    buffer = GST_PAD_PROBE_INFO_BUFFER (probe);
    if (buffer == NULL) {
        LOG_E (log_tag[context->cam_pos], "Buffer NULL in keep_alive_callback2");
        return GST_PAD_PROBE_REMOVE;
    }

    //If PTS is not valid, skip this call back and wait for next
    if (!GST_BUFFER_PTS_IS_VALID(buffer)) {
        return GST_PAD_PROBE_OK;
    }
    
    if  (context->cam_pos == CAMERA_POSITION_FRONT)
        frame_num2++;

    if  (context->cam_pos == CAMERA_POSITION_FRONT && frame_num2 >= 150)
    {
        unsigned long curr_time = 0;
        curr_time = g_get_monotonic_time()*1000;
        LOG_E(log_tag[context->cam_pos],"Delay in vidconv_common:%llu", curr_time-GST_BUFFER_PTS (buffer));
        frame_num2 = 0;
    }

    return GST_PAD_PROBE_OK;
}

static GstPadProbeReturn keep_alive_callback1(GstPad *pad,GstPadProbeInfo *probe,
                                            gpointer user_data)
{

    static int buff_num_with_zero_pts = 0;
    cam_record_ctxt *context = NULL;
    GstBuffer *buffer = NULL;
    static int frame_num1 = 0;

    if (user_data == NULL || probe == NULL || pad == NULL) {
        LOG_E("ERROR","invalid param recieved \n");
        return GST_PAD_PROBE_REMOVE;
    }

    context = (cam_record_ctxt *)user_data;

    if (pad != context->keep_alive_pad1) {
        LOG_E(log_tag[context->cam_pos],"Wrong probe id blocked queue source pad \n");
        return GST_PAD_PROBE_REMOVE;
    }

    buffer = GST_PAD_PROBE_INFO_BUFFER (probe);
    if (buffer == NULL) {
        LOG_E (log_tag[context->cam_pos], "Buffer NULL in keep_alive_callback1");
        return GST_PAD_PROBE_REMOVE;
    }

    //If PTS is not valid, skip this call back and wait for next
    if (!GST_BUFFER_PTS_IS_VALID(buffer)) {
        return GST_PAD_PROBE_OK;
    }

    if  (context->cam_pos == CAMERA_POSITION_FRONT)
        frame_num1++;

    if  (context->cam_pos == CAMERA_POSITION_FRONT && frame_num1 >= 150)
    {
        unsigned long curr_time = 0;
        curr_time = g_get_monotonic_time()*1000;
        LOG_E(log_tag[context->cam_pos],"Delay in outward_camera:%llu", curr_time-GST_BUFFER_PTS (buffer));
        frame_num1 = 0;
    }
    return GST_PAD_PROBE_OK;
}
#endif

static GstPadProbeReturn queue_rt_callback(GstPad *pad,GstPadProbeInfo *probe,
                                            gpointer user_data)
{
    GstBuffer *buffer = NULL;
    static volatile uint64_t prev_pts = 0;
    uint64_t pts_diff = 0;
    uint64_t curr_pts = 0;

    cam_record_ctxt *context = (cam_record_ctxt *)user_data;
    buffer = GST_PAD_PROBE_INFO_BUFFER (probe);
    if (buffer == NULL) {
        LOG_E(log_tag[context->cam_pos], "Buffer NULL in queue_rt__callback");
        return GST_PAD_PROBE_REMOVE;
    }

    GstMapInfo map;
    gst_buffer_map (buffer, &map, GST_MAP_READ);

    curr_pts = GST_BUFFER_PTS (buffer);

    gst_buffer_unmap(buffer, &map);

    if (curr_pts < prev_pts) {
        LOG_E(log_tag[context->cam_pos], "Unexpected! Current PTS: %llu Prev PTS: %llu", curr_pts, prev_pts);
        return GST_PAD_PROBE_DROP;
    }

    pts_diff = curr_pts - prev_pts;
    LOG_D(log_tag[context->cam_pos],"curr_pts = %ld, prev_pts = %ld, pts_diff = %ld", curr_pts, prev_pts, pts_diff);
    if (first_cb_rt || (pts_diff > OUTWARD_MIN_INTRA_FRAME_INTERVAL_NS)) {
        first_cb_rt = false;
        LOG_D(log_tag[context->cam_pos], "forwarding frame");
        prev_pts = curr_pts;
        return GST_PAD_PROBE_OK;
    }

    uint64_t pts_lookup = curr_pts;
    if (curr_pts == 0) {
        buff_num_with_zero_pts_rt ++;
        pts_lookup = buff_num_with_zero_pts_rt;
        LOG_I (log_tag[context->cam_pos], "queue_rt_callback: Look for PTS 0 in the map with lookup_index: %llu", pts_lookup);
    }

    // It is necessary to get timestamp from map even before checking to decide
    // whether to drop the frame or not because each entry in PTS-TS map has a refcount which needs to
    // become zero inorder to clear that entry off map.
    uint64_t frame_num;
    uint64_t raw_time_ns, epoch_time_ns;
    //LOG_I(log_tag[context->cam_pos], "%s: getting ts from pts: %lld", __func__, pts_lookup);
    if (get_timestamps_from_pts (pts_lookup, &raw_time_ns, &epoch_time_ns, &frame_num) == -1) {
        raw_time_ns = 0;
        LOG_E (log_tag[context->cam_pos],"queue_rt_callback: PTS %llu not found in map", pts_lookup);
    }

    LOG_D(log_tag[context->cam_pos], "dropping frame");
    return GST_PAD_PROBE_DROP;
}

static GstPadProbeReturn keep_alive_callback(GstPad *pad,GstPadProbeInfo *probe,
                                            gpointer user_data)
{
    cam_record_ctxt *context = NULL;
    GstBuffer *buffer = NULL;
    uint64_t pts_diff;

    if (user_data == NULL || probe == NULL || pad == NULL) {
        LOG_E("ERROR","invalid param recieved \n");
        return GST_PAD_PROBE_REMOVE;
    }

    context = (cam_record_ctxt *)user_data;

    if (cam_check_thread_created == false) {
        if (!pthread_create (&cam_check, NULL, cam_check_thread, NULL)) {
            LOG_I(log_tag[context->cam_pos], "cam_check_thread created with thread_id: %d", cam_check);
            cam_check_thread_created = true;
        } else {
            LOG_E (log_tag[context->cam_pos], "cam_check_thread creation failed");
        }
    }

    if (pad != context->keep_alive_pad) {
        LOG_E(log_tag[context->cam_pos],"Wrong probe id blocked queue source pad \n");
        return GST_PAD_PROBE_REMOVE;
    }

    buffer = GST_PAD_PROBE_INFO_BUFFER (probe);
    if (buffer == NULL) {
        LOG_E (log_tag[context->cam_pos], "Buffer NULL in keep_alive_callback");
        return GST_PAD_PROBE_REMOVE;
    }

    //If PTS is not valid, skip this call back and wait for next
    if (!((GST_BUFFER_PTS_IS_VALID(buffer)) && (GST_BUFFER_DTS_IS_VALID(buffer)))) {
    	LOG_E(log_tag[context->cam_pos], "Invalid PTS or DTS in keep_alive_callback, pts(%lld) dts(%lld)", GST_BUFFER_PTS(buffer), GST_BUFFER_DTS(buffer));
    	return GST_PAD_PROBE_DROP;
    }

    pts_diff = buffer->pts - previous_time;
    if (pts_diff > DROP_LIMIT)
        LOG_E(log_tag[context->cam_pos], "Time difference between frames: %llu", pts_diff);

    previous_time = buffer->pts;

    //LOG_I(log_tag[context->cam_pos],"Keep alive callback \n");
    context->total_frame_count++;
    g_mutex_lock(&context->cam_frame_mutex);
    context->session_frame_count++;
    context->cam_frame_count++;
    g_mutex_unlock(&context->cam_frame_mutex);

    if (context->total_frame_count >= 150) {
        unsigned long curr_time = 0;
        curr_time = g_get_monotonic_time() * 1000;
        //LOG_E(log_tag[context->cam_pos], "Delay in keep_alive_callback: %llu", curr_time - GST_BUFFER_PTS (buffer));
    }

    if (context->total_frame_count == 1) {
        LOG_I(log_tag[context->cam_pos], "First camera frame recieved");

        context->event_cb(GST_RECORD_EVENT_FIRST_FRAME_MARKER, context->app_cb);

#ifdef DISABLE_KEEP_ALIVE
        gst_pad_remove_probe (context->keep_alive_pad,context->keep_alive_probe);
        context->keep_alive_probe = 0;
#endif

    }
    uint64_t buff_pts = GST_BUFFER_PTS (buffer);
    uint64_t pts_idx = buff_pts;
    
    //LOG_I(log_tag[context->cam_pos], "before change: pts = %lld, dts = %lld", pts_idx, buffer->dts);
    if (buff_pts == 0) {
        buff_num_with_zero_pts_keep_alive ++;
        pts_idx = buff_num_with_zero_pts_keep_alive;
        LOG_I (log_tag[context->cam_pos], "insert_pts_and_timestamp: Buffer with Zero PTS. Assigned idx: %llu", pts_idx);
    }

    // Insert PTS and the timestamp values into map
    int ret = insert_pts_and_timestamp(pts_idx, buffer->dts, context->total_frame_count, context->nd_map_buf_ref_count);
    //LOG_I(log_tag[context->cam_pos], "after change: pts = %lld, dts = %lld, frame_count = %lld", pts_idx, buffer->dts, context->total_frame_count);
    if (ret == -1) {
        LOG_E(log_tag[context->cam_pos], "PTS-DTS Map insert error: frame_number: %lu", context->total_frame_count);
        context->event_cb(GST_RECORD_EVENT_PIPELINE_ERR, context->app_cb);
    }

    buffer->dts = -1;

    return GST_PAD_PROBE_OK;
}

int get_md5sum(void* buffer,
		unsigned long buffersize,
		char* checksum){

	MD5_CTX ctx;
	int rc,i;
	unsigned char digest[MD5_DIGEST_LENGTH];

	rc = MD5_Init(&ctx);
	if(rc != 1) {
		fprintf(stderr,"error in get_md5sum : MD5_Init\n");
		return 1;
	}

	rc =  MD5_Update(&ctx,buffer,sizeof(char)*buffersize);
	if(rc != 1) {
		fprintf(stderr,"error in get_md5sum : MD5_Update\n");
		return 1;
	}

	rc = MD5_Final(digest,&ctx);
	if(rc != 1) {
		fprintf(stderr,"error in get_md5sum : MD5_Final\n");
		return 1;
	}

	for (i = 0; i < MD5_DIGEST_LENGTH; i++) {
		snprintf(&(checksum[i*2]), 16*2, "%02x", (unsigned int)digest[i]);
	}

	checksum[2*MD5_DIGEST_LENGTH+1] = '\0';

	return 0;
}

bool frame_shm_write_cb_func(int smb_id, int64_t uid, void *data_ptr, int64_t data_len, void *user_data)
{
    cam_record_ctxt *context = (cam_record_ctxt *)user_data;
    int camera_num = 0;
    FILE *fp;
    char str[] = "/home/ubuntu/.nddevice/log/ndcentral/gst_rec_writer.yuv";

    LOG_D(log_tag[context->cam_pos], "data_len: %d, outward_rt_buf_size: %d", data_len, outward_rt_buf_size);

    unsigned long int before_time = g_get_monotonic_time ();
    if (data_len)
        memcpy(data_ptr, (uint8_t *)context->frame_data_ptr, outward_rt_buf_size);
    unsigned long int after_time = g_get_monotonic_time ();

    LOG_D(log_tag[context->cam_pos], "Memcpy time taken is: %ld", (after_time - before_time));
#if 0
    char out1[33];
    LOG_I(log_tag[context->cam_pos],"smbid:%d, uid:%d",smb_id,uid);
    fp = fopen( str , "a+" );
    if (fp != NULL)
    {
        LOG_I(log_tag[context->cam_pos], "gst_rec data_ptr:%p",data_ptr);
        LOG_I(log_tag[context->cam_pos], "gst_rec data_len:%d",data_len);
        LOG_I(log_tag[context->cam_pos], "gst_rec data_len:%d",size[1]);
        fwrite(data_ptr, 1 , data_len , fp );
        //fflush( fp );
        fclose(fp);
    }


    get_md5sum(data_ptr, data_len, out1);
    LOG_E(log_tag[context->cam_pos], "Md5sum at write callback %s", out1);
#endif

    return true;
}

static void* init_context_default_params(native_camera_config_t native_cam_config, realtime_camera_config_t* rt_config)
{
    bool get_override_val = true, is_val_overridden = false;
    camera_pos cam_pos = (camera_pos)native_cam_config.cam_pos;
    cam_record_ctxt *context = &gst_cam_ctxt[cam_pos];
    memset(context, 0, sizeof(cam_record_ctxt));

    Config_parser *bagheera_config = new Config_parser(BAGHEERA_CONFIG_INI);
    if (bagheera_config->getParseStatus() != true) {
        LOG_E(log_tag[context->cam_pos], "Cannot allocate bagheera Config");
    }

    Config_parser *nd_config = new Config_parser(ND_CONFIG_INI);
    if (nd_config->getParseStatus() != true) {
        LOG_E(log_tag[context->cam_pos], "Cannot allocate nd Config");
    }

    LOG_I(log_tag[context->cam_pos], "Set the context for camera %d", cam_pos);

    string temp = bagheera_config->getConfig("camera","outward_nrt_width", "1920", get_override_val, is_val_overridden);
    string_to_integer(temp.c_str(), context->nrt_outward_config.width);

    temp = bagheera_config->getConfig("camera","outward_nrt_height", "1080", get_override_val, is_val_overridden);
    string_to_integer(temp.c_str(), context->nrt_outward_config.height);

    temp = bagheera_config->getConfig("camera","outward_nrt_fps", "30", get_override_val, is_val_overridden);
    string_to_integer(temp.c_str(), context->nrt_outward_config.fps);

    temp = bagheera_config->getConfig("camera","outward_nrt_bitrate", "6000000", get_override_val, is_val_overridden);
    string_to_integer(temp.c_str(), context->nrt_outward_config.bitrate);

    temp = bagheera_config->getConfig("camera","outward_nrt_ld_width", "854", get_override_val, is_val_overridden);
    string_to_integer(temp.c_str(), context->nrt_ld_outward_config.width);

    temp = bagheera_config->getConfig("camera","outward_nrt_ld_height", "480", get_override_val, is_val_overridden);
    string_to_integer(temp.c_str(), context->nrt_ld_outward_config.height);

    temp = bagheera_config->getConfig("camera","outward_nrt_ld_bitrate", "1000000", get_override_val, is_val_overridden);
    string_to_integer(temp.c_str(), context->nrt_ld_outward_config.bitrate);

    LOG_I(log_tag[context->cam_pos], "LD config: width=%d, height=%d, bitrate=%d",
      context->nrt_ld_outward_config.width, context->nrt_ld_outward_config.height, context->nrt_ld_outward_config.bitrate);

    temp = nd_config->getConfig("autocam", "shm_buffers", "5", get_override_val, is_val_overridden);
    if (!string_to_integer(temp.c_str(), context->outward_shm_buffers))
        context->outward_shm_buffers = 5;

#ifdef LIVE_SREAMING_EN
    // reset kinesis stream info
    memset(&cam_record_ctxt[cam_pos].kinesis_stream_info, 0 , sizeof(cam_record_ctxt[cam_pos].kinesis_stream_info));
#endif

    context->nd_map_buf_ref_count = 2;

    if (rt_config->enable_streaming == true) {
        memcpy(&context->rt_config, rt_config, sizeof(realtime_camera_config_t));
        context->outwardcam_yuvbuf_size = (context->rt_config.height * context->rt_config.width * 3) / 2;
        OUTWARD_MIN_INTRA_FRAME_INTERVAL_NS = ((1000 / context->rt_config.fps) * 1000 * 1000) - ((1000 / (30 * 2)) * 1000 * 1000);

        LOG_I(log_tag[context->cam_pos], "outwardcam_yuvbuf_size: %d, OUTWARD_MIN_INTRA_FRAME_INTERVAL_NS: %d", context->outwardcam_yuvbuf_size, OUTWARD_MIN_INTRA_FRAME_INTERVAL_NS);

        context->nd_map_buf_ref_count++;
    }

    /* Check support for data products */
    temp = bagheera_config->getConfig("data_products", "enabled", "false", get_override_val, is_val_overridden);
    if (temp == "false") {
        LOG_I(log_tag[context->cam_pos], "Data Product low_fps implementation is disabled");
        g_enable_dp = false;
    } else {
        LOG_I(log_tag[context->cam_pos], "Data Product low_fps implementation is enabled");
        g_enable_dp = true;
    }
    if (g_enable_dp == true) {
        temp = bagheera_config->getConfig("data_products", "bitrate", "1000000", get_override_val, is_val_overridden);
        string_to_integer(temp.c_str(), dp_params.bitrate);

        temp = bagheera_config->getConfig("data_products", "fps", "5", get_override_val, is_val_overridden);
        string_to_integer(temp.c_str(), dp_params.fps);

        temp = bagheera_config->getConfig("data_products", "iframeinterval", "5", get_override_val, is_val_overridden);
        string_to_integer(temp.c_str(), dp_params.iframeinterval);

        LOG_I(log_tag[context->cam_pos], "DP params: bitrate %d, fps %d, iframeinterval %d", dp_params.bitrate, dp_params.fps, dp_params.iframeinterval);

        context->nd_map_buf_ref_count++;
    }

    /* Check support for event access preview feature */
    int ea_enabled = 0;
    temp = bagheera_config->getConfig("ea_config", "enabled", "0", get_override_val, is_val_overridden);
    string_to_integer(temp.c_str(), ea_enabled);
    if (ea_enabled == 0) {
        LOG_I(log_tag[context->cam_pos], "Event Access Preview Feature is disabled");
    } else if (ea_enabled == 1) {
        LOG_I(log_tag[context->cam_pos], "Event Access Preview Feature is enabled");

        temp = bagheera_config->getConfig("ea_config", "outward", "0", get_override_val, is_val_overridden);
        string_to_integer(temp.c_str(), ea_outward_enabled);

        if (ea_outward_enabled == 0) {
            LOG_I(log_tag[context->cam_pos], "Event Access Image capture disabled for outward camera");
        } else if (ea_outward_enabled == 1) {
            LOG_I(log_tag[context->cam_pos], "Event Access Image capture enabled for outward camera, read image specific params from bagheera config");

            temp = bagheera_config->getConfig("ea_config", "width", "206", get_override_val, is_val_overridden);
            string_to_integer(temp.c_str(), ea_image_params.width);
            if (ea_image_params.width != 206) {
                LOG_I(log_tag[context->cam_pos], "Invalid value %d provided for width field under ea_config section, forcing image width to default value (206)", ea_image_params.width);
                ea_image_params.width = 206;
            }

            temp = bagheera_config->getConfig("ea_config", "height", "112", get_override_val, is_val_overridden);
            string_to_integer(temp.c_str(), ea_image_params.height);
            if (ea_image_params.height != 112) {
                LOG_I(log_tag[context->cam_pos], "Invalid value %d provided for height field under ea_config section, forcing image height to default value (112)", ea_image_params.height);
                ea_image_params.height = 112;
            }

            temp = bagheera_config->getConfig("ea_config", "quality", "70", get_override_val, is_val_overridden);
            string_to_integer(temp.c_str(), ea_image_params.quality);
            if ((ea_image_params.quality < 0) || (ea_image_params.quality > 100)) {
                LOG_I(log_tag[context->cam_pos], "Invalid value %d provided for quality field under ea_config section, forcing image quality to default value (70)", ea_image_params.quality);
                ea_image_params.quality = 70;
            }

            LOG_I(log_tag[context->cam_pos], "ea_image_params: width %d, height %d, quality %d", ea_image_params.width, ea_image_params.height, ea_image_params.quality);

            temp = bagheera_config->getConfig("ea_config", "num_images_per_hour", "12", get_override_val, is_val_overridden);
            string_to_integer(temp.c_str(), ea_images_per_hr);
            if ((ea_images_per_hr != 12) && (ea_images_per_hr != 20) && (ea_images_per_hr != 30) && (ea_images_per_hr != 60)) {
                LOG_I(log_tag[context->cam_pos], "Invalid value %d provided for num_images_per_hour field under ea_config section, forcing ea_images_per_hr to default value (12)", ea_images_per_hr);
                ea_images_per_hr = 12;
            }
            ea_freq_session_wise = (int)(60 / ea_images_per_hr);

            LOG_I(log_tag[context->cam_pos], "ea_config: ea_images_per_hr %d", ea_images_per_hr);
        } else {
            LOG_I(log_tag[context->cam_pos], "Invalid value %d provided for outward field under ea_config section, forcing ea_outward_enabled to default value (0) thereby disabling Event Access Image capture for outward camera", ea_outward_enabled);
            ea_outward_enabled = 0;
        }
    } else {
        LOG_I(log_tag[context->cam_pos], "Invalid value %d provided for enabled field under ea_config section, forcing ea_enabled to default value (0) thereby disabling Event Access Preview Feature", ea_enabled);
        ea_enabled = 0;
    }

    LOG_I(log_tag[context->cam_pos], "context->nd_map_buf_ref_count: %d", context->nd_map_buf_ref_count);

	context->state = STATE_CREATED;
	context->queue_msg = 0;
	context->keep_alive_probe = 0;
	context->cam_pos = cam_pos;
	context->total_frame_count = 0; //used by keep_alive_callback
	context->total_frames_written = 0; //used by cb_data_mux
	context->session_frame_count = 0;
	g_cond_init (&context->cond);
	context->cond_var = 0;
	g_mutex_init (&context->mutex);
	g_mutex_init (&context->cam_frame_mutex);
	g_mutex_init (&context->session_change_mutex);
	g_cond_init (&context->session_change_cond);
	context->queue = g_async_queue_new();

	context->width = context->nrt_outward_config.width;
	context->height = context->nrt_outward_config.height;
	context->analytics_width = context->rt_config.width;
	context->analytics_height = context->rt_config.height;

    LOG_I(log_tag[context->cam_pos], "SHM_DEBUG :: context->rt_config.enable_streaming %d", context->rt_config.enable_streaming);
    if (rt_config->enable_streaming) {
        // fill shm writer object into context and register write cb
        LOG_I(log_tag[context->cam_pos], "SHM_DEBUG :: rt_outward_width: %d", context->rt_config.width);
        LOG_I(log_tag[context->cam_pos], "SHM_DEBUG :: rt_outward_height: %d", context->rt_config.height);
        LOG_I(log_tag[context->cam_pos], "SHM_DEBUG :: rt_outward_fps: %d", context->rt_config.fps);
        LOG_I(log_tag[context->cam_pos], "SHM_DEBUG :: outward_shm_size: %ld", (rt_config->width * rt_config->height * 3));
        LOG_I(log_tag[context->cam_pos], "SHM_DEBUG :: outward_shm_buffers: %d", context->outward_shm_buffers);

        //frame size we are dividing by 2 (3/2) because the frame size will be 1.5 times of width * height
        int64_t smb_data_size = (int64_t)(rt_config->width * rt_config->height * 3/2);
        context->shm_writer = new NdSharedMemoryWriter(native_cam_config.name, smb_data_size, context->outward_shm_buffers);
        if (context->shm_writer) {
            LOG_I(log_tag[context->cam_pos],"SHM_DEBUG :: Shared Memory Writer 0x%x created successfully for outward camera with smb_data_size: %lld and shm_buffers: %d",
                    context->shm_writer, smb_data_size, context->outward_shm_buffers);

            context->shm_writer->set_log_frequency(100);
            context->shm_writer->register_write_callback(frame_shm_write_cb_func);
            context->shm_writer_name = native_cam_config.name;
        }
        g_mutex_init (&context->shm_writer_mutex);
    }

    return (void *)context;
}

int init_record_session_platform(void *hndl )
{
   int ret = TRUE;
   cam_record_ctxt *context = NULL;
   gint64 end_time = 0;

   context = (cam_record_ctxt *) hndl;

   if (NULL == context )
       return FALSE;

   g_mutex_lock(&context->mutex);

   if (context->state == STATE_CREATED) {

       LOG_I(log_tag[context->cam_pos],"creating thread for record session \n");

       context->thread = g_thread_new (log_tag[context->cam_pos],
                                       gst_thread_func,
                                       context);

       memset(context->cur_file_name, 0x00, GSTREAMER_NAME_LENGTH_MAX);

       context->queue_msg = (int)STATE_INIT;

       LOG_D(log_tag[context->cam_pos],"Posting INIT msg to queue \n");   

       g_async_queue_push (context->queue,&(context->queue_msg)); 

       while (!(context->cond_var))
       {
           g_cond_wait (&(context->cond), &(context->mutex));
       }
       context->cond_var = 0;
 
       ret = (context->state == STATE_INIT) ? TRUE : FALSE;
   }

   g_mutex_unlock(&context->mutex);

   return ret;    
}

static gboolean get_sensor_map_version(const char *tag)
{
     FILE *fp = NULL ;
     gchar *tmp = NULL;
     gchar buffer[VERSION_STRING_LENGTH] = {0};
     gint count = 0;
     gboolean ret = FALSE;

     LOG_I(tag,"get_sensor_map_version \n");

     if (tag == NULL) {
         LOG_I(tag,"TAG is NULL \n");
         return FALSE;
     }

     fp = fopen("/etc/vendor/version_ntdi.txt","r");
     
     if (fp == NULL) {
         LOG_E(tag,"File open for version_icdc failed \n");
         return FALSE;
     }

     if ((count = fread((void *)buffer, 1 ,VERSION_STRING_LENGTH, fp)) <= 0) {
         LOG_E(tag,"File read for version number failed \n");
         fclose(fp);
         return FALSE;
     }
     fclose(fp);

     tmp = buffer;

     while (count > 0)
     {
         if (*tmp >= '0' && *tmp <= '9')
               break;
         tmp++;
         count--;
     }

     if (count > 0)
     {
        if (!strncasecmp(tmp,"3.0.3",5)) {
             LOG_I(tag,"Version %s , Older than 1.3.0.3 found \n",tmp);
             version_id = SENSOR_MAP_VERSION_0_1;
        }
        else {
             LOG_I(tag,"Version 1.3.0.3 or above %s found ,sensor id changed \n",tmp);
             version_id = SENSOR_MAP_VERSION_1_0;
         }

         ret = TRUE;
     }
     else
     {
        LOG_E(tag,"Version number not mentioned \n");

     }

     return ret;
}

void *init_camera_record_platform(native_camera_config_t native_cam_config, realtime_camera_config_t* rt_config)
{
     void *plat_handle = NULL;
     camera_pos cam_pos = (camera_pos)native_cam_config.cam_pos;
     const char *tag = native_cam_config.name.c_str();
     int len = native_cam_config.name.length();

     if (version_id == SENSOR_MAP_VERSION_INVALID) {
         if (get_sensor_map_version(tag) != TRUE) {
             LOG_E(tag,"get sensor map version failed \n" );
             return NULL;
         }
     }
 
     if (cam_pos < CAMERA_POSITION_FRONT || cam_pos >= CAMERA_POSITION_MAX ||
         tag == NULL || len <= 0 || len >= GSTREAMER_NAME_LENGTH_MAX)
         return NULL;

     LOG_I(tag,"init context for camera %d\n",cam_pos);

     strncpy(log_tag[cam_pos],tag,len); 

     plat_handle = init_context_default_params(native_cam_config, rt_config);

     return plat_handle;
}

void overrun_handler_queue_common (void *queue, gpointer  user_data)
{
    cam_record_ctxt *context = (cam_record_ctxt *)(user_data);
    LOG_I(log_tag[context->cam_pos], "Overrun for COMMON Queue");
}

void overrun_handler_queue_nrt (void *queue, gpointer  user_data)
{
    cam_record_ctxt *context = (cam_record_ctxt *)(user_data);
    LOG_I(log_tag[context->cam_pos], "Overrun for NRT Queue");
}

void overrun_handler_queue_tc (void *queue, gpointer user_data)
{
    cam_record_ctxt *context = (cam_record_ctxt *)(user_data);
    LOG_I(log_tag[context->cam_pos], "Overrun for TC Queue");
}

/* Description:
 * This is the callback registered with dp_vrconv_appsink which feeds raw frames
 * to dp_vrconv_appsrc for supporting low fps functionality for data products
 */
static void dp_vrconv_appsink_cb(GstAppSink *object, gpointer user_data)
{
    uint64_t pts_diff = 0;
    uint64_t curr_pts = 0;
    GstFlowReturn ret;
    static const int min_intra_frame_interval_ns_dp = ((1000 / dp_params.fps) * 1000 * 1000) - (1000/30/2 * 1000 * 1000);

    uint64_t frame_num;

    cam_record_ctxt *context = (cam_record_ctxt *)user_data;
    GstSample *sample = gst_app_sink_pull_sample(object);
    GstBuffer *buffer = gst_sample_get_buffer(sample);
    GstMapInfo map;
    gst_buffer_map (buffer, &map, GST_MAP_READ);

    curr_pts = GST_BUFFER_PTS (buffer);
    pts_diff = curr_pts - prev_pts_dp;

	LOG_D(log_tag[context->cam_pos], "min_intra_frame_interval_ns_dp: %llu, pts_diff: %llu, prev_pts_dp: %llu", min_intra_frame_interval_ns_dp, pts_diff, prev_pts_dp);

	if (first_cb_dp_vrconv || (pts_diff > min_intra_frame_interval_ns_dp)) {
        LOG_D (log_tag[context->cam_pos], "Pushing the buffer with PTS %llu to dp_vrconv_appsrc", curr_pts);
		first_cb_dp_vrconv = false;
		prev_pts_dp = curr_pts;
		GstBuffer *app_buffer = gst_buffer_copy_deep(buffer);
		ret = gst_app_src_push_buffer((GstAppSrc*)context->data_product_app_src, app_buffer);
	} else {
        LOG_D (log_tag[context->cam_pos], "Dropping the buffer with PTS %llu from the DP pipeline", curr_pts);

        uint64_t pts_lookup = curr_pts;
        if (curr_pts == 0) {
            buff_num_with_zero_pts_dp ++;
            pts_lookup = buff_num_with_zero_pts_dp;
        }

        // It is necessary to get the timestamp from map even for the dropped frames
        // because each entry in PTS-TS map has a refcount which needs to
        // become zero inorder to clear that entry off map.
        uint64_t raw_time_ns, epoch_time_ns;
        if (get_timestamps_from_pts (pts_lookup, &raw_time_ns, &epoch_time_ns, &frame_num) == -1) {
            LOG_E (log_tag[context->cam_pos], "dp_vrconv_appsink_cb: PTS %llu not found in map", pts_lookup);
        }
	}

    if (map.data != nullptr)
        gst_buffer_unmap(buffer, &map);

    if (sample != nullptr)
        gst_sample_unref(sample);
}

void configure_RT_pipeline(cam_record_ctxt *context)
{
    GstElement *elem = NULL;

#ifdef PROFILING
    elem = gst_bin_get_by_name(GST_BIN(context->pipeline), "outward_camera");
    context->keep_alive_pad1 = gst_element_get_static_pad (elem, "src");
    context->keep_alive_probe1 = gst_pad_add_probe(context->keep_alive_pad1, GST_PAD_PROBE_TYPE_BUFFER,
		    (GstPadProbeCallback) keep_alive_callback1, context, NULL);
    g_object_unref (elem);

    elem = gst_bin_get_by_name(GST_BIN(context->pipeline), "vidconv_common");
    context->keep_alive_pad2 = gst_element_get_static_pad (elem, "src");
    context->keep_alive_probe2 = gst_pad_add_probe(context->keep_alive_pad2, GST_PAD_PROBE_TYPE_BUFFER,
		    (GstPadProbeCallback) keep_alive_callback2, context, NULL);
    g_object_unref (elem);


    elem = gst_bin_get_by_name(GST_BIN(context->pipeline), "vidconv_rt");
    context->keep_alive_pad4 = gst_element_get_static_pad (elem, "src");
    context->keep_alive_probe4 = gst_pad_add_probe(context->keep_alive_pad4, GST_PAD_PROBE_TYPE_BUFFER,
		    (GstPadProbeCallback) keep_alive_callback4, context, NULL);
    g_object_unref (elem);
#endif

    if (context->cam_pos == CAMERA_POSITION_FRONT) {
        elem = gst_bin_get_by_name(GST_BIN(context->pipeline), "queue_rt");
        context->queue_rt_pad = gst_element_get_static_pad (elem, "sink");
        context->queue_rt_probe = gst_pad_add_probe(context->queue_rt_pad, GST_PAD_PROBE_TYPE_BUFFER,
                (GstPadProbeCallback) queue_rt_callback, context, NULL);
        g_object_unref (elem);
    }

    elem = gst_bin_get_by_name(GST_BIN(context->pipeline), "queue_rt");
#ifdef PROFILING
    context->keep_alive_pad3 = gst_element_get_static_pad (elem, "src");
    context->keep_alive_probe3 = gst_pad_add_probe(context->keep_alive_pad3, GST_PAD_PROBE_TYPE_BUFFER,
		    (GstPadProbeCallback) keep_alive_callback3, context, NULL);
#endif
    g_signal_connect (elem, "overrun", G_CALLBACK (overrun_handler_queue_rt), context);
    g_object_unref (elem);

    elem = gst_bin_get_by_name(GST_BIN(context->pipeline), "queue_appsink_rt");
#ifdef PROFILING
    context->keep_alive_pad5 = gst_element_get_static_pad (elem, "sink");
    context->keep_alive_probe5 = gst_pad_add_probe(context->keep_alive_pad5, GST_PAD_PROBE_TYPE_BUFFER,
		    (GstPadProbeCallback) keep_alive_callback5, context, NULL);
#endif
    g_signal_connect (elem, "overrun", G_CALLBACK (overrun_handler_queue_appsink_rt), context);
    g_object_unref (elem);

    elem = gst_bin_get_by_name(GST_BIN(context->pipeline), "appsink_rt");
    g_signal_connect (elem, "new-sample", G_CALLBACK (appsink_roadfacing_cb), context);
    g_object_unref (elem);
}

void configure_common_pipeline(cam_record_ctxt *context)
{
    GstElement *elem = NULL;
    GstElement *muxer = NULL;

    elem = gst_bin_get_by_name(GST_BIN(context->pipeline), "queue_common");
	context->keep_alive_pad = gst_element_get_static_pad(elem, "sink");
	context->keep_alive_probe = gst_pad_add_probe(context->keep_alive_pad, GST_PAD_PROBE_TYPE_BUFFER,
			(GstPadProbeCallback) keep_alive_callback, context, NULL);

	g_signal_connect(elem, "overrun", G_CALLBACK (overrun_handler_queue_common), context);
	g_object_unref(elem);
}

void configure_NRT_pipeline(cam_record_ctxt *context)
{
    GstElement *elem = NULL;

    elem = gst_bin_get_by_name(GST_BIN(context->pipeline), "queue_nrt");
    g_signal_connect (elem, "overrun", G_CALLBACK (overrun_handler_queue_nrt), context);
    g_object_unref (elem);

    elem = gst_bin_get_by_name(GST_BIN(context->pipeline), "appsink_nrt");
    g_signal_connect(elem, "new-sample", G_CALLBACK (appsink_nrt_cb), context);
    g_object_unref (elem);
}

void configure_transcode_pipeline(cam_record_ctxt *context)
{
   GstElement *elem = NULL;

   elem = gst_bin_get_by_name(GST_BIN(context->pipeline), "queue_tc");
   g_signal_connect (elem, "overrun", G_CALLBACK (overrun_handler_queue_tc), context);
   g_object_unref (elem);

   elem = gst_bin_get_by_name(GST_BIN(context->pipeline), "appsink_tc");
   g_signal_connect(elem, "new-sample", G_CALLBACK (appsink_tc_cb), context);
   g_object_unref (elem);
}

/* Configure Live Streaming Pipeline */
void configure_LS_pipeline(cam_record_ctxt *context)
{
	GstElement *elem = NULL;

    elem = gst_bin_get_by_name(GST_BIN(context->pipeline), "ls_vrconv_appsink");
    g_signal_connect(elem, "new-sample", G_CALLBACK (ls_vrconv_appsink_cb), context);
    g_object_unref(elem);

    context->kinesis_app_src = gst_bin_get_by_name (GST_BIN (context->live_streaming_pipeline), "kinesis_appsrc");
    g_object_set (G_OBJECT (context->kinesis_app_src), "caps",
            gst_caps_new_simple ("video/x-raw(memory:NVMM)",
                "format", G_TYPE_STRING, "NV12",
                "width", G_TYPE_INT, 640,
                "height", G_TYPE_INT, 360,
                NULL), NULL);

    /* configure appsink for live stream */
    elem = gst_bin_get_by_name(GST_BIN(context->live_streaming_pipeline), "appsink_kinesis");
    g_signal_connect(elem, "new-sample", G_CALLBACK (livestreaming_cb), context);
    g_object_unref(elem);
}

/* Configure Data Product Pipeline */
void configure_DP_pipeline(cam_record_ctxt *context)
{
   GstElement *elem = NULL;
   GstPad     *pad = NULL;

   elem = gst_bin_get_by_name(GST_BIN(context->pipeline), "dp_vrconv_appsink");
   if (elem) {
	   g_signal_connect(elem, "new-sample", G_CALLBACK (dp_vrconv_appsink_cb), context);
       g_object_unref (elem);
   }

   context->data_product_app_src = gst_bin_get_by_name (GST_BIN (context->data_product_appsrc_pipeline), "dp_vrconv_appsrc");
   g_object_set (G_OBJECT (context->data_product_app_src), "caps",
           gst_caps_new_simple ("video/x-raw(memory:NVMM)",
               "format", G_TYPE_STRING, "I420",
               "width", G_TYPE_INT, context->width,
               "height", G_TYPE_INT, context->height,
               NULL), NULL);

   elem = gst_bin_get_by_name(GST_BIN(context->data_product_appsrc_pipeline), "appsink_dp");
   if (elem) {
	    g_signal_connect(elem, "new-sample", G_CALLBACK (appsink_dp_cb), context);
	    g_object_unref (elem);
   }
}

/* Configure Event Access Preview Pipeline */
void configure_ea_pipeline(cam_record_ctxt *context)
{
   GstElement *elem = NULL;

   context->event_access_app_src = gst_bin_get_by_name(GST_BIN (context->event_access_pipeline), "ea_appsrc");
   g_object_set (G_OBJECT (context->event_access_app_src), "caps",
           gst_caps_new_simple ("video/x-raw",
               "format", G_TYPE_STRING, "NV12",
               "width", G_TYPE_INT, context->rt_config.width,
               "height", G_TYPE_INT, context->rt_config.height,
               "framerate", GST_TYPE_FRACTION, 1, (3600 / ea_images_per_hr),
               NULL), NULL);

   elem = gst_bin_get_by_name(GST_BIN(context->event_access_pipeline), "appsink_ea");
   if (elem) {
        g_signal_connect(elem, "new-sample", G_CALLBACK (appsink_ea_cb), context);
        g_object_unref (elem);
   }
}

static gboolean create_main_camera_pipeline(cam_record_ctxt *context)
{
   GError *error = NULL;
   GstElement *elem;
   stringstream pipeline;
   stringstream common_pipeline, nrt_pipeline;
   stringstream rt_pipeline, nrt_ld_pipeline;
   stringstream kinesis_appsrc_pipeline, kinesis_appsink_pipeline;
   stringstream nrt_dp_pipeline, dp_appsrc_pipeline;
   stringstream ea_pipeline;

   if (!context)
       return FALSE;

   // This is a temporary fix to reduce the instances of camera crashes on service start
   LOG_I(log_tag[context->cam_pos], "sleeping for 2 seconds before creating main camera pipeline");
   sleep(2);
   LOG_I(log_tag[context->cam_pos], "slept for 2 seconds, will create main camera pipeline");
   if (context->pipeline == NULL) {
       if (!usev4l2src) {
           common_pipeline
               << "nvv4l2camerasrc name=outward_camera device=/dev/video0 ! "
               << "video/x-raw(memory:NVMM), format=UYVY, width=" << context->width << ", height=" << context->height << " ! "
               << "nvvidconv name=vidconv_common ! video/x-raw(memory:NVMM), format=I420, width=" << context->width << ", height=" << context->height << " ! "
               << "queue name=queue_common leaky=2 max-size-buffers=0 max-size-bytes=0 max-size-time=4000000000 !";
       } else {
           common_pipeline
               << "v4l2src name=outward_camera device=/dev/video0 ! "
               << "video/x-raw, format=UYVY, width=" << context->width << ", height=" << context->height << " ! "
               << "nvvidconv name=vidconv_common ! video/x-raw(memory:NVMM), format=I420, width=" << context->width << ", height=" << context->height << " ! "
               << "queue name=queue_common leaky=2 max-size-buffers=0 max-size-bytes=0 max-size-time=4000000000 !";
       }

       nrt_pipeline
           << " tee name=t1 ! queue name=queue_nrt leaky=2 max-size-buffers=0 max-size-bytes=0 max-size-time=4000000000 ! "
           << "nvv4l2h265enc name=h265enc control-rate=0 insert-sps-pps=true insert-vui=true bitrate=" << context->nrt_outward_config.bitrate << " peak-bitrate=" << context->nrt_outward_config.bitrate << " "
           << "preset-level=3 maxperf-enable=1 idrinterval=0 iframeinterval=" << i_interval_outcam << " ! "
           << "h265parse ! video/x-h265, stream-format=byte-stream ! "
           << "queue name=queue_appsink_nrt leaky=2 max-size-buffers=0 max-size-bytes=0 max-size-time=1000000000 ! "
           << "appsink name=appsink_nrt emit-signals=TRUE sync=FALSE";

       if (context->ld_enabled) {
           nrt_ld_pipeline
               << " t1. ! queue name=queue_tc leaky=2 max-size-buffers=0 max-size-bytes=0 max-size-time=4000000000 ! "
               << "nvvidconv name=vidconv_tc ! video/x-raw(memory:NVMM), format=I420, width=" << context->nrt_ld_outward_config.width << ", height=" << context->nrt_ld_outward_config.height << " ! "
               << "nvv4l2h265enc name=h265enc_tc control-rate=0 insert-sps-pps=true insert-vui=true bitrate=" << context->nrt_ld_outward_config.bitrate << " peak-bitrate=" << context->nrt_ld_outward_config.bitrate << " preset-level=3 maxperf-enable=1 idrinterval=0 iframeinterval=30 ! "
               << "h265parse ! video/x-h265, stream-format=byte-stream ! "
               << "queue name=queue_appsink_tc leaky=2 max-size-buffers=0 max-size-bytes=0 max-size-time=1000000000 ! "
               << "appsink name=appsink_tc emit-signals=TRUE sync=FALSE";

           pipeline << common_pipeline.str() << nrt_pipeline.str() << nrt_ld_pipeline.str();

        } else {
           pipeline << common_pipeline.str() << nrt_pipeline.str();
        }

       if (context->rt_config.enable_streaming == true) {
           rt_pipeline
               << " t1. ! queue name=queue_rt leaky=2 max-size-buffers=0 max-size-bytes=0 max-size-time=3000000000 ! "
               << "nvvidconv name=vidconv_rt interpolation-method=" << interpolation_method << " output-buffers=20 ! "
               << "video/x-raw, format=NV12, width=" << context->analytics_width << ", height=" << context->analytics_height << ", framerate=" << context->nrt_outward_config.fps << "/1 ! "
               << "queue name=queue_appsink_rt leaky=2 max-size-buffers=0 max-size-bytes=0 max-size-time=1000000000 ! "
               << "appsink name=appsink_rt emit-signals=TRUE max-buffers=3 async=FALSE drop=TRUE sync=FALSE";

           pipeline << rt_pipeline.str();
       }

       if (g_live_streaming == true) {
           kinesis_appsink_pipeline
               << " t1. ! queue name=queue_ls leaky=2 max-size-buffers=0 max-size-bytes=0 max-size-time=1000000000 ! "
               << "nvvidconv name=vidconv_ls ! video/x-raw, format=NV12, width=" << live_streaming_param.width << ", height=" << live_streaming_param.height << " ! "
               << "queue name=queue_live_sink leaky=2 max-size-buffers=0 max-size-bytes=0 max-size-time=1000000000 ! "
               << "appsink name=ls_vrconv_appsink emit-signals=TRUE sync=FALSE";

           kinesis_appsrc_pipeline
               << "appsrc name=kinesis_appsrc ! "
               << "queue name=queue_kinesis_appsrc ! video/x-raw, format=NV12, width=640, height=360, framerate=10/1 ! "
               << "nvvidconv name=vidconv_kinesis ! video/x-raw(memory:NVMM), format=I420, width=" << live_streaming_param.width << ", height=" << live_streaming_param.height << " ! "
               << "nvv4l2h264enc name=h264enc_kinesis control-rate=0 bitrate=" << live_streaming_param.bitrate << " peak-bitrate=" << live_streaming_param.bitrate << " preset-level=3 maxperf-enable=1 insert-sps-pps=true "
               << "iframeinterval=" << live_streaming_param.iframeinterval << " idrinterval=" << live_streaming_param.iframeinterval << " ! "
               << "h264parse ! video/x-h264, stream-format=byte-stream ! "
               << "queue name=queue_appsink_kinesis leaky=2 max-size-buffers=0 max-size-bytes=0 max-size-time=1000000000 ! "
               << "appsink name=appsink_kinesis emit-signals=TRUE sync=FALSE";

           printf("For kinesis appsrc pipeline, using launch string: %s\n", kinesis_appsrc_pipeline.str().c_str());

           context->live_streaming_pipeline = gst_parse_launch (kinesis_appsrc_pipeline.str().c_str(), &error);

           if (context->live_streaming_pipeline) {
               pipeline << kinesis_appsink_pipeline.str();
           } else {
               LOG_C(log_tag[context->cam_pos], "kinesis appsrc pipeline creation failed with parse error: %s", error->message);
               nd_service_obj->send_err_msg(SM_E_NDC_APPSRC_FAIL, context->cam_pos, "appsrc pipeline failure" );
           }
       }

       if (g_enable_dp) {
           nrt_dp_pipeline
               << " t1. ! queue name=queue_dp leaky=2 max-size-buffers=0 max-size-bytes=0 max-size-time=1000000000 ! "
               << "appsink name=dp_vrconv_appsink emit-signals=TRUE sync=FALSE";

           dp_appsrc_pipeline
               << "appsrc name=dp_vrconv_appsrc ! "
               << "queue name=queue_dp_vrconv_appsrc ! video/x-raw(memory:NVMM), format=I420, width=" << context->width << ", height=" << context->height << ", framerate=" << dp_params.fps << "/1 ! "
               << "nvv4l2h265enc name=h265enc_dp control-rate=0 insert-sps-pps=true insert-vui=true bitrate=" << dp_params.bitrate << " peak-bitrate=" << dp_params.bitrate << " "
               << "preset-level=3 maxperf-enable=1 idrinterval=0 iframeinterval=" << dp_params.iframeinterval << " ! "
               << "h265parse ! video/x-h265, stream-format=byte-stream ! "
               << "queue name=queue_appsink_dp leaky=2 max-size-buffers=0 max-size-bytes=0 max-size-time=1000000000 ! "
               << "appsink name=appsink_dp emit-signals=TRUE sync=FALSE";

           printf("For DP appsrc pipeline, using launch string: %s\n", dp_appsrc_pipeline.str().c_str());

           context->data_product_appsrc_pipeline = gst_parse_launch (dp_appsrc_pipeline.str().c_str(), &error);

           pipeline << nrt_dp_pipeline.str();
       }

       if (ea_outward_enabled) {
           ea_pipeline
               << "appsrc name=ea_appsrc ! "
               << "nvvidconv ! video/x-raw(memory:NVMM), format=NV12, width=" << ea_image_params.width << ", height=" << ea_image_params.height << " ! "
               << "nvjpegenc quality=" << ea_image_params.quality << " ! "
               << "appsink name=appsink_ea emit-signals=TRUE sync=FALSE";

           printf("For event access pipeline, using launch string: %s\n", ea_pipeline.str().c_str());

           context->event_access_pipeline = gst_parse_launch(ea_pipeline.str().c_str(), &error);
           if (context->event_access_pipeline == NULL) {
               LOG_C(log_tag[context->cam_pos], "Event Access pipeline creation failed with parse error: %s", error->message);
           }
       }

       printf("For outward_camera, using launch string: %s\n", pipeline.str().c_str());

       context->pipeline = gst_parse_launch (pipeline.str().c_str(), &error);

       if (!context->pipeline) {
           LOG_E(log_tag[context->cam_pos], "Pipeline creation failed with parse error: %s\n", error->message);
           return FALSE;
       }
   }

   configure_common_pipeline(context);
   configure_NRT_pipeline(context);
   if (context->ld_enabled)
       configure_transcode_pipeline(context);

   if (context->rt_config.enable_streaming == true) {
       guint64 interpol_method = 0;
       configure_RT_pipeline(context);

       elem = gst_bin_get_by_name(GST_BIN(context->pipeline), "vidconv_rt");
       g_object_get (G_OBJECT (elem), "interpolation-method", &interpol_method, NULL);
       LOG_I(log_tag[context->cam_pos], "For outward_camera, interpolation method for vidconv_rt is: %d", interpol_method);
       g_object_unref (elem);
   }

   if (g_live_streaming)
	   configure_LS_pipeline(context);

   if (g_enable_dp)
       configure_DP_pipeline(context);

   if (ea_outward_enabled)
       configure_ea_pipeline(context);

   return TRUE;
}

static void cleanup_gstreamer_context(cam_record_ctxt *context)
{
    GstStateChangeReturn ret;
    if (NULL == context)
         return;

    if (NULL != context->pipeline) {
        ret = gst_element_set_state(GST_ELEMENT(context->pipeline),GST_STATE_NULL);
        if (ret == GST_STATE_CHANGE_FAILURE) {
            LOG_E (log_tag[context->cam_pos],"Changing pipeline state to NULL failed");
        } 
        context->pipeline = NULL;
    }

    return;
}

/* This function is called when any message is posted on the bus */
static void cb_bus_message (GstBus *bus, GstMessage *msg, void *data)
{
    cam_record_ctxt *context = (cam_record_ctxt *)data;

    switch( GST_MESSAGE_TYPE (msg)) {

        case GST_MESSAGE_ERROR:
            GError *err;
            gchar *debug_info;
            /* Print error details on the screen */
            gst_message_parse_error(msg, &err, &debug_info);

            LOG_E (log_tag[context->cam_pos], "Error received from element %s: %s\n",  GST_OBJECT_NAME (msg->src), err->message);
            LOG_E (log_tag[context->cam_pos], "Debugging information: %s\n", debug_info ? debug_info : "none");

            g_clear_error (&err);
            g_free (debug_info);

            break;

        case GST_MESSAGE_EOS:
            LOG_I (log_tag[context->cam_pos],"EOS received from element %s", GST_OBJECT_NAME (msg->src));
            break;

       default:
           LOG_D(log_tag[context->cam_pos], "Unknown message with message type %d received from element %s", GST_MESSAGE_TYPE (msg), GST_OBJECT_NAME (msg->src));
            break;
    }
}

static int create_record_pipeline_initialize(cam_record_ctxt *context)
{
   int result = FALSE;
   GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
   GstBus *bus;

   if (NULL == context)
       goto END;

   // Read the live streaming params
   read_config_params(context);

   if (!create_main_camera_pipeline(context))
	   goto END;

   bus = gst_pipeline_get_bus (GST_PIPELINE (context->pipeline));
   gst_bus_add_signal_watch (bus);
   gst_bus_set_sync_handler (bus, gst_bus_sync_signal_handler, context, NULL);
   g_object_connect (bus, "signal::sync-message", G_CALLBACK (cb_bus_message), context, NULL);
   gst_object_unref (bus);

   LOG_I(log_tag[context->cam_pos], "Setting pipeline to PAUSED ...\n");

   ret = gst_element_set_state (context->pipeline, GST_STATE_PAUSED);

   switch (ret) {
    case GST_STATE_CHANGE_FAILURE:
        LOG_C(log_tag[context->cam_pos], "ERROR: Pipeline doesn't want to pause.\n");
        break;

    case GST_STATE_CHANGE_ASYNC:
        LOG_C(log_tag[context->cam_pos], "Pipeline is PREROLLING ...\n");
        break;

    case GST_STATE_CHANGE_NO_PREROLL:
        LOG_I(log_tag[context->cam_pos], "Pipeline is live and does not need PREROLL ...\n");
        result = TRUE;
        break;

    case GST_STATE_CHANGE_SUCCESS:
        LOG_I(log_tag[context->cam_pos], "Pipeline is PREROLLED ...\n");
        result = TRUE;
        break;
   }

END:
  return result;
}

static void* gst_thread_func(void *ptr)
{
   int result = FALSE, isExit = FALSE;
   sigset_t set ; 
   GError *err = NULL;
   pipeline_state *msg = NULL;
   GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

   cam_record_ctxt *context = (cam_record_ctxt *)ptr;  
  
   sigemptyset(&set);
   sigaddset(&set,SIGINT);
   pthread_sigmask(SIG_BLOCK,&set,NULL);

   if (NULL == context)
       return NULL;

   if (!gst_init_check(NULL,NULL,&err)) {
        LOG_C(log_tag[context->cam_pos],"gstreamer init failed %s \n",err->message);
        return NULL;
   }
   NDDeviceTypeT type = ND_DeviceFactory::getBuildDeviceType();
   if((NDDeviceTypeT::bagheera2 == type) || (NDDeviceTypeT::bagheera3 == type)) {
       int n_cpus = get_nprocs();
       int n_cpus_conf = get_nprocs_conf();
       LOG_I(log_tag[context->cam_pos], "n_cpus : %d , n_cpus_conf : %d", n_cpus, n_cpus_conf);
       if(n_cpus == n_cpus_conf) {
           if(CAMERA_POSITION_FRONT == context->cam_pos){
               cpu_set_t cpuset;
               CPU_ZERO(&cpuset);
               vector<int> cores_to_be_set = {CPU_CORE_0};
               for(int i = 0; i < cores_to_be_set.size(); i++){
                   CPU_SET(cores_to_be_set[i], &cpuset);
               }
               pid_t threadID = syscall(SYS_gettid);
               sched_setaffinity(threadID, sizeof(cpuset), &cpuset);
               cpu_set_t get_cpuset;
               CPU_ZERO(&get_cpuset);
               vector<int> confirm_set;
               sched_getaffinity(threadID, sizeof(get_cpuset), &get_cpuset);
               for (int i = 0; i < CPU_SETSIZE; i++) {
                   if (CPU_ISSET(i, &get_cpuset)) {
                       confirm_set.push_back(i);
                       LOG_I(log_tag[context->cam_pos], "CPU set for current thread: %d", i);
                   }
               }
               if(cores_to_be_set != confirm_set){
                   nd_service_obj->send_err_msg(SM_E_CPU_CORE_ERROR, context->cam_pos, "Failed to set affinity for camera");
               }
           }
       } else {
           string err_msg = "Cores available don't match cores online. n_cpus : " + to_string(n_cpus) + " n_cpus_conf : " + to_string(n_cpus_conf);
           nd_service_obj->send_err_msg(SM_E_CPU_CORE_ERROR, context->cam_pos, err_msg);
       }
   }

   while ((msg = (pipeline_state *)g_async_queue_pop((GAsyncQueue *)context->queue)) != NULL) {
        result = TRUE;
        isExit = FALSE;

        g_mutex_lock(&context->mutex);

        switch (*msg)
        {
             case STATE_INIT:
                  if (!isExit) {
		              LOG_I(log_tag[context->cam_pos],"Initialising pipeline ..\n");
		              if (create_record_pipeline_initialize(context)) {
		                  LOG_I(log_tag[context->cam_pos],"Succesfully Initialised pipeline ..\n");
		                  context->state = STATE_INIT;
		              } else {
		                  result = FALSE;
		                  LOG_E(log_tag[context->cam_pos],"Failed in initialising pipeline ..\n");
		              }
                  }
                  context->cond_var = 1;
                  g_cond_signal(&context->cond);
                  break;


             case STATE_PLAYING:
                  if (!isExit) {
                      LOG_I(log_tag[context->cam_pos],"PLAYING pipeline ..\n");

                      ret = gst_element_set_state (context->pipeline, GST_STATE_PLAYING); 

                      if (ret == GST_STATE_CHANGE_FAILURE) {
                          LOG_I(log_tag[context->cam_pos],"Failed to set PLAYING ..\n");
                          result = FALSE;
                          break;
                      }
                      if (g_live_streaming)
                          int ret = gst_element_set_state (context->live_streaming_pipeline, GST_STATE_PLAYING);

                      if (g_enable_dp)
                          int ret = gst_element_set_state (context->data_product_appsrc_pipeline, GST_STATE_PLAYING);

                      if (ea_outward_enabled) {
                          int ret = gst_element_set_state (context->event_access_pipeline, GST_STATE_PLAYING);
                          if ((ret == GST_STATE_CHANGE_SUCCESS) || (ret == GST_STATE_CHANGE_ASYNC)) {
                              LOG_I(log_tag[context->cam_pos], "Successfully changed Event Access pipeline state to PLAYING. ret: %d", ret);
                          } else {
                              LOG_E(log_tag[context->cam_pos], "Failed to set the Event Access pipeline to PLAYING state. ret: %d", ret);
                          }
                      }

                      context->state = STATE_PLAYING;  
                  }                              
                  break;

            case STATE_STOP:
                if (!isExit) {
                    LOG_I(log_tag[context->cam_pos],"Stopping pipeline ..\n");
                    context->state = STATE_STOP;
                 }
                 isExit = TRUE;
                 break;

            case STATE_ERROR:
                if (context->state != STATE_ERROR) {
                    LOG_I(log_tag[context->cam_pos],"Notifying pipeline error \n");
                    context->state = STATE_ERROR;

                    obj_context_t *obj = NULL;
                    cam_crash_status_t *error_status = NULL;
                    if (context->app_cb != NULL) {
                        obj_context_t *obj = (obj_context_t *)context->app_cb;
                        error_status = (cam_crash_status_t *)obj->app_error_ctxt;
                        error_status->status[context->cam_pos] = true;
                    }
                    if (NULL != context->event_cb)
                        context->event_cb(GST_RECORD_EVENT_PIPELINE_ERR, context->app_cb);
                }
                isExit = TRUE;
                break;
        }

        if (!result && (context->state != STATE_ERROR)) {
            LOG_E(log_tag[context->cam_pos],"Error state reached \n");
            context->state = STATE_ERROR;
            obj_context_t *obj = NULL;
            cam_crash_status_t *error_status = NULL;
            if (context->app_cb != NULL) {
                obj_context_t *obj = (obj_context_t *)context->app_cb;
                error_status = (cam_crash_status_t *)obj->app_error_ctxt;
                error_status->status[context->cam_pos] = true;
            }
 
            if (NULL != context->event_cb)
                context->event_cb(GST_RECORD_EVENT_INTERNAL_ERR, context->app_cb);

            isExit = TRUE;
        }

        if (isExit)
            context->state = STATE_EXIT;

        g_mutex_unlock(&context->mutex);

        if (isExit) {
            LOG_I(log_tag[context->cam_pos],"Breaking out of msg queue check \n");
            break;
        }
    }

    LOG_I(log_tag[context->cam_pos], "cleaning camera context");

    cleanup_gstreamer_context(context);

    LOG_I(log_tag[context->cam_pos], "Gst thread exiting");

    g_thread_exit(NULL);

    return NULL;
}

int start_record_session_platform(void *ptr)
{
    int ret = FALSE;

    cam_record_ctxt *context = (cam_record_ctxt *)ptr;    

    if (NULL == context) 
         return FALSE;

    g_mutex_lock(&context->mutex);

    if (STATE_INIT == context->state) {

        context->queue_msg = (int)STATE_PLAYING;

        LOG_I(log_tag[context->cam_pos],"Posting start PLAYING msg to queue");

        g_async_queue_push (context->queue,&(context->queue_msg)); 

        ret = TRUE;
    }

    g_mutex_unlock(&context->mutex);
    
    return ret;  
}

int stop_record_session_platform (void *ptr)
{
    int ret = FALSE;

    cam_record_ctxt *context = (cam_record_ctxt *)ptr;

    if (NULL == context)
         return FALSE;

    LOG_I(log_tag[context->cam_pos],"stop camera record  \n");

    g_mutex_lock(&context->mutex);

    if ((context->state != STATE_UNINITIALIZED) && (context->state != STATE_ERROR) 
        && (context->state != STATE_EXIT)) {

         context->queue_msg = (int)STATE_STOP;

         LOG_D(log_tag[context->cam_pos],"Posting STOP msg to queue \n");   

         g_async_queue_push (context->queue,&(context->queue_msg));  
    }

    g_mutex_unlock(&context->mutex);

    LOG_I(log_tag[context->cam_pos],"Waiting for main thread to exit \n");
   
    g_thread_join(context->thread);

    LOG_I(log_tag[context->cam_pos],"STOP msg handling done \n"); 

    return TRUE;
}

void destroy_camera_record_platform(void *ptr)
{
   cam_record_ctxt *context = (cam_record_ctxt *)ptr;

   if (NULL == context)
       return ;

   LOG_I(log_tag[context->cam_pos],"destroy camera record \n");

   stop_record_session_platform(context);

   memset(context,0,sizeof(cam_record_ctxt));

   return ;
}


int set_record_callback_platform(void *ptr,
		                         record_file_callback_t file_cb,
                                 record_status_callback_t event_cb,
                                 frame_write_callback_t frame_write_cb,
                                 record_timestamp_callback_t timestamp_cb,
								 record_data_prod_callback_t data_prod_cb,
								 void *app)
{
   cam_record_ctxt *context = (cam_record_ctxt *)ptr;

   if ((NULL == context) || (context->state != STATE_CREATED) || (NULL == file_cb) || (NULL == event_cb))
       return FALSE;   

   context->record_cb = file_cb;
   context->event_cb = event_cb;
   context->timestamp_cb = timestamp_cb;
   context->data_prod_cb = data_prod_cb;
   context->frame_write_cb = frame_write_cb;
   context->app_cb  = app;

   return TRUE;
}


int set_fname_callback_platform(void *ptr,record_fname_cb_t fname_cb,
                                 void *app)
{
   cam_record_ctxt *context = (cam_record_ctxt *)ptr;

   if (NULL == context || context->state != STATE_CREATED
       || NULL == fname_cb)
       return FALSE;   

   context->fname_cb = fname_cb;
   context->app_fname  = app;

   LOG_I(log_tag[context->cam_pos], "Registered fname callback");

   return TRUE;
}

//Set live stream frame drop callback
int set_live_stream_frame_drop_callback_platform(void *ptr, live_stream_frame_drop_cb_t live_stream_frame_drop_cb)
{
   cam_record_ctxt *context = (cam_record_ctxt *)ptr;

   if (NULL == context || NULL == live_stream_frame_drop_cb)
       return false;

   context->live_stream_fram_drop_cb = live_stream_frame_drop_cb;

   LOG_I(log_tag[context->cam_pos], "Registered live stream frame drop callback");

   return true;
}

int set_appsink_callback_platform(void *ptr, record_appsink_cb_t appsink_cb /*, void *app*/ )
{
   cam_record_ctxt *context = (cam_record_ctxt *)ptr; 

   if (NULL == context || context->state != STATE_CREATED
       || NULL == appsink_cb)
       return FALSE;   

   context->appsink_cb = appsink_cb;
   //context->app_fname  = app;

   LOG_I(log_tag[context->cam_pos], "Registered appsink callback");

   return TRUE;
}

bool gst_pipeline_state_change_tt(void *args)
{
    GstStateChangeReturn retValue;
    cam_record_ctxt *context = (cam_record_ctxt *)args;

    LOG_I(log_tag[context->cam_pos], "Before changing outward camera pipeline state to NULL");
    retValue = gst_element_set_state(context->pipeline, GST_STATE_NULL);
    if (GST_STATE_CHANGE_SUCCESS == retValue) {
        LOG_I(log_tag[context->cam_pos], "Outward camera pipeline state change to NULL success");
    } else {
        LOG_E(log_tag[context->cam_pos], "Outward camera pipeline state change to NULL failed with %d", retValue);
        return FALSE;
    }
    LOG_I(log_tag[context->cam_pos], "After changing outward camera pipeline state to NULL");

    if (g_enable_dp) {
        LOG_I(log_tag[context->cam_pos], "Before changing DP appsrc pipeline state to NULL");
        retValue = gst_element_set_state(context->data_product_appsrc_pipeline, GST_STATE_NULL);
        if (GST_STATE_CHANGE_SUCCESS == retValue) {
            LOG_I(log_tag[context->cam_pos], "DP appsrc pipeline state change to NULL success");
        } else {
            LOG_E(log_tag[context->cam_pos], "DP appsrc pipeline state change to NULL failed with %d", retValue);
            return FALSE;
        }
        LOG_I(log_tag[context->cam_pos], "After changing DP appsrc pipeline state to NULL");
    }

    if (ea_outward_enabled) {
        LOG_I(log_tag[context->cam_pos], "Before changing Event Access pipeline state to NULL");
        retValue = gst_element_set_state(context->event_access_pipeline, GST_STATE_NULL);
        if (GST_STATE_CHANGE_SUCCESS == retValue) {
            LOG_I(log_tag[context->cam_pos], "Event Access pipeline state change to NULL success");
        } else {
            LOG_E(log_tag[context->cam_pos], "Event Access pipeline state change to NULL failed with %d", retValue);
            return FALSE;
        }
        LOG_I(log_tag[context->cam_pos], "After changing Event Access pipeline state to NULL");
    }

    if (g_live_streaming) {
        LOG_I(log_tag[context->cam_pos], "Before changing live streaming appsrc pipeline state to NULL");
        retValue = gst_element_set_state(context->live_streaming_pipeline, GST_STATE_NULL);
        if (GST_STATE_CHANGE_SUCCESS == retValue) {
            LOG_I(log_tag[context->cam_pos], "Live streaming appsrc pipeline state change to NULL success");
        } else {
            LOG_E(log_tag[context->cam_pos], "Live streaming appsrc pipeline state change to NULL failed with %d", retValue);
            return FALSE;
        }
        LOG_I(log_tag[context->cam_pos], "After changing live streaming appsrc pipeline state to NULL");
    }

    return TRUE;
}

void restart_nvargus_daemon(cam_record_ctxt *context)
{
    FILE *fp = NULL;
    stringstream command;

    LOG_E(log_tag[context->cam_pos], "kill nvargus daemon");
    command.str("");
    command << "systemctl stop nvargus-daemon.service";
    LOG_E(log_tag[context->cam_pos], "nvargus stop command %s", command.str().c_str());

    fp = popen(command.str().c_str(), "r");
    if (fp == NULL) {
        LOG_E(log_tag[context->cam_pos], "Failed to run stop nvargus command :: %s", command.str().c_str() );
        return;
    }
    pclose(fp);
    LOG_E(log_tag[context->cam_pos], "killed nvargus-daemon.service");

    sleep(1);

    command.str("");
    command << "systemctl start nvargus-daemon.service";
    LOG_E(log_tag[context->cam_pos], "nvargus start command %s", command.str().c_str());
    fp = popen(command.str().c_str(), "r");
    if (fp == NULL) {
        LOG_E(log_tag[context->cam_pos], "Failed to run start nvargus command :: %s", command.str().c_str() );
        return;
    }
    pclose(fp);
    LOG_E(log_tag[context->cam_pos], "restarted nvargus");

    /* Create a file in the shared memory space to signal cam_rec service to proceed */
    file_touch("/dev/shm/nd_files_c/nvargus_daemon_restarted");
}

bool restart_record_session_platform(void *ptr, bool restart_pipeline_required)
{
    GstState pending, state;
    GstStateChangeReturn retValue;

    cam_record_ctxt *context = (cam_record_ctxt *)ptr;

    if (!((context->state == STATE_PLAYING) && first_cb_hd == false)) {
        LOG_I(log_tag[context->cam_pos], "Pipeline not in PLAYING state or first frame is not received, cannot restart record session");
        return FALSE;
    }

    if (NULL == context) {
        LOG_I(log_tag[context->cam_pos], "context is NULL");
        return FALSE;
    }

    if (!restart_pipeline_required) {
        LOG_I(log_tag[context->cam_pos], "Restart pipeline not required, just changing the session");
        exit_session_change_thread = true;

        itimerspec new_value = {};
        new_value.it_value.tv_sec = 0;
        new_value.it_value.tv_nsec = 1;  // Triggers almost immediately
        timerfd_settime(timer_fd, 0, &new_value, nullptr);

        pthread_join(session_th, nullptr);
        session_change = true;
        fname_updated = false;
        return TRUE;
    }

    g_mutex_lock(&context->mutex);

    if ((context->state != STATE_UNINITIALIZED) &&
        (context->state != STATE_ERROR) &&
        (context->state != STATE_EXIT) &&
        (context->pipeline != NULL)) {

        exit_cam_check_thread = true;
        exit_session_change_thread = true;

        task_result_t task_result = nd_timed_task(gst_pipeline_state_change_tt,
                GST_PIPELINE_STATE_CHANGE_TIMEOUT, (void *)context, "gstreamer pipeline state change");
        if (task_result == TASK_TIMEOUT) {
            LOG_E (log_tag[context->cam_pos], "nd_timed_task for gst_pipeline_state_change_tt timedout, hence exiting from the service");
            nd_service_obj->send_err_msg(SM_E_NDC_GST_PIPELINE_STATE_CHANGE_FAIL, 0, "gst pipeline state change to NULL TIMEDOUT");
            /* Restart nvargus daemon to unblock cam_rec service */
            restart_nvargus_daemon(context);

            _exit(0);
        } else if (task_result == TASK_FAIL) {
            LOG_E (log_tag[context->cam_pos], "Failed to change the gstreamer pipeline state to NULL, hence exiting from the service");
            nd_service_obj->send_err_msg(SM_E_NDC_GST_PIPELINE_STATE_CHANGE_FAIL, 0, "gst pipeline state change to NULL FAILED");
            /* Restart nvargus daemon to unblock cam_rec service */
            restart_nvargus_daemon(context);

            _exit(0);
        }
        LOG_I(log_tag[context->cam_pos], "gst_pipeline_state_change_tt return status %d", task_result);
    }
    first_cb_hd = true;
    first_cb_ld = true;
    first_cb_dp = true;
    first_cb_rt = true;
    first_cb_ls = true;
    first_cb_dp_vrconv = true;
    first_cb_ea = true;
    first_forward_ea = true;

    fname_updated = false;

    session_count = 0; //reset to 0 when outcam pipeline is restarted on account of cam_rec service restart
    pthread_cancel(session_th);

    gst_object_unref (GST_OBJECT (context->pipeline));

    if (NULL != context->record_cb) {
        if (context->abs_epoch_time == 0) {
            context->abs_epoch_time = get_system_time_ns() / 1000; // updated to current epoch time in microseconds
            LOG_E(log_tag[context->cam_pos], "abs_epoch_time was zero, updated to current epoch time: %llu", context->abs_epoch_time);
        }
        //Give Record file stop callback to end the main camera session now
        LOG_I (log_tag[context->cam_pos], "While calling stop: session_start_pts = %lld, abs_raw_time: %ld, abs_epoch_time: %ld, session_frame_count: %d", context->session_start_pts, context->abs_raw_time, context->abs_epoch_time, context->session_frame_count);
        context->record_cb(GST_RECORD_FILE_STOP,
                context->abs_raw_time, context->abs_epoch_time, context->session_start_pts,
                context->app_cb, context->session_frame_count, false, context->next_session_fname);

        if (context->ld_enabled) {
            //Sending RECORD_STOP callback for LD files
            LOG_I (log_tag[context->cam_pos], "While calling stop (LD): session_start_pts_ld = %lld, abs_raw_time: %ld, abs_epoch_time: %ld, session_frame_count: %d", context->session_start_pts_ld, context->abs_raw_time, context->abs_epoch_time, context->session_frame_count);
            context->record_cb(GST_RECORD_FILE_STOP,
                    context->abs_raw_time, context->abs_epoch_time, context->session_start_pts_ld,
                    context->app_cb, context->session_frame_count, true, context->next_session_fname_ld);
        }
    }

    context->pipeline = NULL;
    context->total_frame_count = 0;
    context->total_frames_written = 0;
    context->session_frame_count = 0;
    context->frame_number = 0;

    /* Restart nvargus daemon to unblock cam_rec service */
    restart_nvargus_daemon(context);

    LOG_I(log_tag[context->cam_pos], "Before clearing the nd_map and global statics related memory");
    clear_map();
    prev_pts_ls = 0;
    prev_pts_rt = 0;
    prev_pts_dp = 0;
    buff_num_with_zero_pts_keep_alive = 0;
    buff_num_with_zero_pts_hd = 0;
    buff_num_with_zero_pts_ld = 0;
    buff_num_with_zero_pts_dp = 0;
    buff_num_with_zero_pts_rt = 0;
    frame_count_rt = 0;
    LOG_I(log_tag[context->cam_pos], "After clearing the nd_map and global statics related memory");

    create_main_camera_pipeline(context);

    GstBus *bus = gst_pipeline_get_bus (GST_PIPELINE (context->pipeline));
    gst_bus_add_signal_watch (bus);
    gst_bus_set_sync_handler (bus, gst_bus_sync_signal_handler, context, NULL);
    g_object_connect (bus, "signal::sync-message", G_CALLBACK (cb_bus_message), context, NULL);
    gst_object_unref (bus);

    LOG_I (log_tag[context->cam_pos], "Before changing pipeline state to PLAYING");
    gst_element_set_state(context->pipeline, GST_STATE_PLAYING);
    retValue = gst_element_get_state(context->pipeline, &state, &pending, GST_CLOCK_TIME_NONE);
    LOG_I(log_tag[context->cam_pos], "gst_element_get_state state: %d, pending: %d, retValue: %d", state, pending, retValue);
    LOG_I(log_tag[context->cam_pos], "After changing pipeline state to PLAYING");

    if (retValue == GST_STATE_CHANGE_FAILURE) {
	    LOG_E(log_tag[context->cam_pos],"Failed to set the main camera pipeline to PLAYING state");
    }

    if (g_enable_dp) {
        LOG_I (log_tag[context->cam_pos], "Before changing DP appsrc pipeline state to PLAYING");
        gst_element_set_state(context->data_product_appsrc_pipeline, GST_STATE_PLAYING);
        retValue = gst_element_get_state(context->data_product_appsrc_pipeline, &state, &pending, GST_CLOCK_TIME_NONE);
        if (retValue == GST_STATE_CHANGE_FAILURE) {
            LOG_E(log_tag[context->cam_pos],"Failed to set the DP appsrc pipeline to PLAYING state");
        } else {
            LOG_I(log_tag[context->cam_pos], "Successfully changed DP appsrc pipeline state to PLAYING");
        }
        LOG_I (log_tag[context->cam_pos], "After changing DP appsrc pipeline state to PLAYING");
    }

    if (ea_outward_enabled) {
        LOG_I (log_tag[context->cam_pos], "Before changing Event Access pipeline state to PLAYING");
        retValue = gst_element_set_state(context->event_access_pipeline, GST_STATE_PLAYING);
        /*
         * GST_STATE_CHANGE_ASYNC return value is a success scenario because sometimes
         * sinks complete the state change asynchronously when they receive the first buffer.
         */
        if ((retValue == GST_STATE_CHANGE_SUCCESS) || (retValue == GST_STATE_CHANGE_ASYNC)) {
            LOG_I(log_tag[context->cam_pos], "Successfully changed Event Access pipeline state to PLAYING");
        } else {
            LOG_E(log_tag[context->cam_pos], "Failed to set the Event Access pipeline to PLAYING state");
        }
        LOG_I (log_tag[context->cam_pos], "After changing Event Access pipeline state to PLAYING");
    }

    if (g_live_streaming) {
        LOG_I (log_tag[context->cam_pos], "Before changing live streaming appsrc pipeline state to PLAYING");

        retValue = gst_element_set_state(context->live_streaming_pipeline, GST_STATE_PLAYING);
        /*
         * GST_STATE_CHANGE_ASYNC return value is a success scenario because sometimes
         * sinks complete the state change asynchronously when they receive the first buffer.
         */
        if ((retValue == GST_STATE_CHANGE_SUCCESS) || (retValue == GST_STATE_CHANGE_ASYNC)) {
            LOG_I(log_tag[context->cam_pos], "Successfully changed live streaming appsrc pipeline state to PLAYING");
        } else {
            LOG_E(log_tag[context->cam_pos], "Failed to set the live streaming appsrc pipeline to PLAYING state");
        }
        LOG_I (log_tag[context->cam_pos], "After changing live streaming appsrc pipeline state to PLAYING");
    }

    g_mutex_unlock(&context->mutex);

    return TRUE;
}

void recreate_rt_shared_memory(void *ptr)
{
    cam_record_ctxt *context = (cam_record_ctxt *)ptr;

    g_mutex_lock(&context->shm_writer_mutex);

    drop_rt_frames = true;

    LOG_I(log_tag[context->cam_pos], "SHM_DEBUG :: Deleting older instance of shm_writer 0x%x", context->shm_writer);
    delete context->shm_writer;
    context->shm_writer = NULL;

    int64_t smb_data_size = (int64_t)(context->rt_config.width * context->rt_config.height * 3/2);
    context->shm_writer = new NdSharedMemoryWriter(context->shm_writer_name, smb_data_size, context->outward_shm_buffers);
    if (context->shm_writer) {
        LOG_I(log_tag[context->cam_pos],"SHM_DEBUG :: Shared Memory Writer 0x%x recreated successfully for outward camera with smb_data_size: %lld and shm_buffers: %d",
                context->shm_writer, smb_data_size, context->outward_shm_buffers);

        context->shm_writer->set_log_frequency(100);
        context->shm_writer->register_write_callback(frame_shm_write_cb_func);

        nd_service_obj->send_err_msg(SM_E_NDC_CAM_SHM_RECREATED, context->cam_pos, "RT shared memory recreated for outward camera");
    }
    drop_rt_frames = false;

    g_mutex_unlock(&context->shm_writer_mutex);
}
