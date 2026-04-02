/*
* Copyright (c) 2016-2018, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above
*       copyright notice, this list of conditions and the following
*       disclaimer in the documentation and/or other materials provided
*       with the distribution.
*     * Neither the name of The Linux Foundation nor the names of its
*       contributors may be used to endorse or promote products derived
*       from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
* ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
* BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
* OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#define LOG_TAG "QMMF_REC"

#include <string>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/timerfd.h>
#include <signal.h>
#include <utils/Log.h>
#include <system/graphics.h>
#include <nd_task.h>

#include "gst_recorder.h"
#include "nd_time.h"
#include "qmmf_recorder_test.h"
#include "file_helper.h"

#include <opencv2/opencv.hpp>
#include <opencv2/imgproc/types_c.h>

#include "log.h"
#include "nd_time.h"
#include <config_parser.h>
#include <system_utils.h>
#include <nd_auth_openssl.h>
#include "nd_file_utils.h"
#include <nd_auth_utils.h>
#include <mutex>

#include "QRScanner.h"
#include "nd_factory.h"

//#define DEBUG
#define TEST_INFO(fmt, args...)  ALOGD(fmt, ##args)
#define TEST_ERROR(fmt, args...) ALOGE(fmt, ##args)
#define TEST_WARN(fmt,args...)   ALOGW(fmt, ##args)
#ifdef DEBUG
#define TEST_DBG  TEST_INFO
#else
#define TEST_DBG(...) ((void)0)
#endif
#define DUMP_DEBUG_FRAME 0

/* A static global variable showing the status of live streaming
 * true: live streaming is not yet started
 * false: live streaming is started
 */
static volatile bool streaming = true;
static volatile bool streaming_outward = true;
static volatile bool streaming_inward = true;
static volatile bool rt_config_enabled[CAMERA_POSITION_MAX] = {0};

static int INWARD_MIN_INTRA_FRAME_INTERVAL_NS;
static int OUTWARD_MIN_INTRA_FRAME_INTERVAL_NS;

#define OTHER_FPS 15
#define OUTWARD_FPS 30
#define MS_IN_SECONDS 1000
#define MS_TO_NS 1000000
#define EA_PREVIEW_OUTWARD_FIRST_SESSION_FRAME_SKIP_COUNT 60
#define SERVICE_RESTART_TIMEOUT_SECS 10 // Timeout for service restart (in seconds)
#define QMMF_RESTART_COUNT_FILE "/dev/shm/nd_files_c/qmmf_restart_count.txt"
#define QMMF_SERVER_MAX_RESTARTS_PER_BOOT 5

#define fps_15_2_frames ((MS_IN_SECONDS/OTHER_FPS) * 2)
#define fps_30_2_frames ((MS_IN_SECONDS/OUTWARD_FPS) * 2)

#define DROP_LIMIT_OUTWARD (fps_30_2_frames * MS_TO_NS)
#define DROP_LIMIT_INWARD (fps_15_2_frames * MS_TO_NS)

static int kinesis_req_fps = -1;

using ::std::mutex;
using ::std::unique_lock;
using ::std::vector;

static bool session_change[2] = {false, false};
static bool session_change_ld[2] = {false, false};
static bool session_change_ea[2] = {false, false};

CameraInitInfo *current_camera_info;
int32_t current_camera_id;
int32_t ret;
bool is_connected = false;
bool first_frame[2] = {true, true};
bool first_frame_ld[2] = {true, true};

bool cam_check_thread_created = false;
pthread_t cam_check;
#define CAM_CRASH_THREAD_INTERVAL_IN_SECS 5
#define INIT_CAM_CRASH_THREAD_INTERVAL_IN_SECS 10
#define FPS_THRESHOLD_FOR_CAM_CRASH 5.0
#define QMMF_CONNECT_TIMEOUT_SECS 5
#define QMMF_DATA_CB_BLOCK_THRESHOLD_IN_MSEC 200

#define ND_CONFIG_INI "/home/ubuntu/.nddevice/latest/nd_config.ini"

std::vector<TrackInfo_t> infos;

uint32_t session_id[2];

static unsigned int g_track_id_hd[CAMERA_POSITION_MAX] = {0};
static unsigned int g_track_id_ld[CAMERA_POSITION_MAX] = {0};
static unsigned int g_track_id_rt[CAMERA_POSITION_MAX] = {0};
static unsigned int g_track_id_live[CAMERA_POSITION_MAX] = {0};
static unsigned int g_track_id_qr = 0;
static unsigned int g_track_count[CAMERA_POSITION_MAX] = {0};
uint64_t previous_ts[CAMERA_POSITION_MAX] = {0};

static unsigned int hd_track_offset = 1;
static unsigned int rt_track_offset = 2;
static unsigned int ls_track_offset = 3;

static const char ld_extn[] = ".ld.mp4";
static const char ea_extn[] = ".0_ea.jpeg";

volatile static int64_t session_count = 0;

static const int RETRY_DELAY = 2; // retry delay for init camera failures

static const string BAGHEERACONFIG_INI = "/home/ubuntu/.nddevice/latest/bagheera_config.ini";

RecorderTest recorder_test;

static const string black_frame_outward_HD = "/home/ubuntu/.nddevice/black_frame_outward_HD.hevc";
static const string black_frame_outward_LD = "/home/ubuntu/.nddevice/black_frame_outward_LD.hevc";
static const string black_frame_inward_HD  = "/home/ubuntu/.nddevice/black_frame_inward_HD.hevc";
static const string black_frame_inward_LD  = "/home/ubuntu/.nddevice/black_frame_inward_LD.hevc";
static const string black_frame_inward_LS = "/home/ubuntu/.nddevice/black_frame_inward_LS.avc";
static const string black_frame_outward_LS = "/home/ubuntu/.nddevice/black_frame_outward_LS.avc";

binary_data_t *black_frame_hd[CAMERA_POSITION_MAX] = {NULL};
binary_data_t *black_frame_ld[CAMERA_POSITION_MAX] = {NULL};

binary_data_t *black_frame_ls[CAMERA_POSITION_MAX] = {NULL};

// Variable to check for the bitrate to be configured only once
static bool is_bit_rate_configured = false;
// Variables used for the bitrates, initialized with some default values
static int32_t outward_nrt_bitrate = 6000000;
static int32_t outward_nrt_ld_bitrate = 1000000;
static int32_t inward_nrt_bitrate = 2000000;
static int32_t inward_nrt_ld_bitrate = 500000;

static int32_t live_bitrate_front = 500000;
static int32_t live_bitrate_back = 500000;

static bool g_live_streaming = false;
static bool stream_encryption = true;

static volatile int livestream_framedrop_cnt = 0;
static const string TMP_KINESIS_STREAMING_OUTWARD = "/tmp/kinesis_streaming_out";
static const string TMP_KINESIS_STREAMING_INWARD = "/tmp/kinesis_streaming_in";
static bool kinesis_dual_streaming = false;
std::mutex dualStreamingFileMutex;

volatile static bool drop_incam_rt_frames = false;
volatile static bool drop_outcam_rt_frames = false;

//a frame is changed to valid when it's the first iframe after continuous black frame sending in livestreaming
static const bool VALID_FRAME = false;
//a frame is changed to invalid when after black frames the next frames are not i-frames
static const bool INVALID_FRAME = true;

extern NDService *nd_service_obj; //nd service object, to send error message to service monitor service

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

int raw_buffer_width;
int raw_buffer_height;

struct connect_task_args {
    RecorderCb *recorder_status_cb;
    int32_t result;
};

static const string image_raw_data_file = "/dev/shm/image_raw_buffer.yuv";

static bool is_image_raw_data_present = false;
pthread_mutex_t ea_mutex;
pthread_cond_t ea_cond;
/* Event Access Specific Declarations End */

/* QR scanner specific declarations start*/
using nd::interface::IQRScanner;
using nd::device::QRScannerFactory;
// Flag to indicate if driver login QR scanning feature is enabled
static bool is_driverlogin_qr_enabled = false;
// Global flag to control QR scanning operation state
bool start_QR_scan = false;
// Flag to track if this is the first QR scan attempt
static bool first_time_scan = true;
// Index for writing Y-plane buffer data in circular buffer fashion
static int buf_write_index = 0;
// Smart pointer to the QR scanner instance created by the factory pattern
unique_ptr<IQRScanner> scanner_ptr;
// Vector to store QR code tag patterns that should be matched during scanning
static std::vector<std::string> qr_scan_tags;
// Flag to signal the timeout thread to stop execution
bool stop_timeout_thread = false;
// Flag to signal the QR decode thread to stop processing
bool stop_qrdecode_thread = false;
// Flag indicating if QR scanning operation has timed out
bool qr_scan_timedout = false;
// Flag indicating if QR scan timeout thread has been created
bool qr_scan_timeout_thread_created = false;
// Thread handle for QR scan timeout monitoring thread
pthread_t qr_scan_timeout_th;
// Thread handle for QR code detection and decoding thread
static pthread_t qr_code_detect_decode_th = 0;
// Flag indicating if QR decode thread has been created
static bool qr_decode_thread_created = false;
// Maximum duration (in seconds) allowed for QR scanning before timeout
int max_scan_duration;
// Flag indicating if Y-plane buffer data is available for processing
static bool is_yplane_buf_present = false;
// Mutex for thread-safe access to QR scan shared resources
pthread_mutex_t qr_scan_mutex;
// Condition variable for coordinating QR scan thread operations
pthread_cond_t qr_scan_cond;
// Width of the buffer used for QR scanning image data
int qrscan_buffer_width;
// Height of the buffer used for QR scanning image data
int qrscan_buffer_height;
// Pointer to Y-plane data buffer used for QR code detection
uint8_t *y_planes = NULL;
// Maximum number of Y-plane buffers that can be queued for processing
static int max_queued_yplanes = 2;
// Flag to enable/disable dumping invalid QR scan image data to file for debugging
bool invalid_qr_image_dump_enabled = false;
// File path for storing invalid QR scan image raw data for debugging
static const string invalid_qr_image_raw_data_file = "/dev/shm/invalid_qr_image_raw_buffer.yuv";
/* QR scanner specific declarations end*/

void* qr_scan_timeout_thread(void *args)
{
    int ret;
    uint32_t sec;
    int timer_fd;
    struct itimerspec itval;
    uint64_t missed;

    /* Create the timer */
    timer_fd = timerfd_create(CLOCK_MONOTONIC, 0);
    if (timer_fd == -1) {
        LOG_E("QR_SCAN", "%s: timer not created", __func__);
        return NULL;
    }

    /* Make the timer periodic */
    sec = 1;
    itval.it_interval.tv_sec = sec;
    itval.it_interval.tv_nsec = 0;
    itval.it_value.tv_sec = sec;
    itval.it_value.tv_nsec = 0;
    ret = timerfd_settime(timer_fd, 0, &itval, NULL);
    if (ret == -1) {
        LOG_E("QR_SCAN", "%s: Failed to set the interval time for the timer. ", __func__);
        return NULL;
    }

    int scan_time_elapsed = 0;
    while (!stop_timeout_thread) {
        /* Wait for the next timer event. If we have missed any the
           number is written to "missed" */
        ret = read(timer_fd, &missed, sizeof(missed));
        if (ret == -1) {
            LOG_E("QR_SCAN", "%s: read api call failed", __func__);
            return NULL;
        }
        LOG_D("QR_SCAN", "%s: hitting timer", __func__);

        scan_time_elapsed += 1;
        if (scan_time_elapsed >= max_scan_duration) {
            LOG_I("QR_SCAN", "QR scan has TIMEDOUT");
            start_QR_scan = false;
            stop_qrdecode_thread = true;
            qr_scan_timedout = true;
            pthread_mutex_lock(&qr_scan_mutex);
            pthread_cond_signal(&qr_scan_cond);
            pthread_mutex_unlock(&qr_scan_mutex);

            break;
        }
    }

    return NULL;
}

bool device_start_QR_scan(char tags[kQRScanMaxTagPatterns][kQRScanMaxTagPatternDataLen])
{
    if (!scanner_ptr) {
        scanner_ptr = QRScannerFactory::CreateQRScanner();
        if (scanner_ptr) {
            LOG_I("QR_SCAN", "Successfully created QR scanner instance");
        } else {
            LOG_E("QR_SCAN", "Failed to create QR scanner instance");
            return false;
        }
    } else {
        LOG_I("QR_SCAN", "QR scanner instance already exists");
    }

    // Clear the global vector and copy tags to it
    qr_scan_tags.clear();
    for (int i = 0; i < kQRScanMaxTagPatterns; i++) {
        if (strlen(tags[i]) > 0) { // Only add non-empty tags
            qr_scan_tags.push_back(std::string(tags[i]));
            LOG_I("QR_SCAN", "Tag %d: %s", i, tags[i]);
        }
    }
    LOG_I("QR_SCAN", "Copied %zu tags to global vector", qr_scan_tags.size());

    first_time_scan = true;
    start_QR_scan = true;

    // Kill existing timeout thread if it exists before creating a new one
    if (qr_scan_timeout_thread_created) {
        LOG_I("QR_SCAN", "Stopping existing timeout thread before creating new one");
        stop_timeout_thread = true;

        // Wait for the thread to finish
        int join_result = pthread_join(qr_scan_timeout_th, NULL);
        if (join_result != 0) {
            LOG_E("QR_SCAN", "Failed to join timeout thread: %d", join_result);
        } else {
            LOG_I("QR_SCAN", "Successfully stopped existing timeout thread");
        }
        qr_scan_timeout_thread_created = false;
    }

    // Reset the stop flag and create the timeout thread
    stop_timeout_thread = false;
    int create_result = pthread_create(&qr_scan_timeout_th, NULL, &qr_scan_timeout_thread, NULL);
    if (create_result != 0) {
        LOG_E("QR_SCAN", "Failed to create timeout thread: %d", create_result);
        return false;
    } else {
        qr_scan_timeout_thread_created = true;
        LOG_I("QR_SCAN", "Successfully created QR scan timeout thread");
    }

    return true;
}

bool device_stop_QR_scan()
{
    start_QR_scan = false;

    stop_timeout_thread = true;
    stop_qrdecode_thread = true;

    pthread_mutex_lock(&qr_scan_mutex);
    pthread_cond_signal(&qr_scan_cond);
    pthread_mutex_unlock(&qr_scan_mutex);

    // Wait for decode thread to finish if it was created
    if (qr_decode_thread_created) {
        int join_result = pthread_join(qr_code_detect_decode_th, NULL);
        if (join_result != 0) {
            LOG_E("QR_SCAN", "Failed to join decode thread: %d", join_result);
        } else {
            LOG_I("QR_SCAN", "Successfully joined decode thread");
        }
        qr_decode_thread_created = false;
    }

    // Wait for timeout thread to finish if it was created
    if (qr_scan_timeout_thread_created) {
        int join_result = pthread_join(qr_scan_timeout_th, NULL);
        if (join_result != 0) {
            LOG_E("QR_SCAN", "Failed to join timeout thread during stop: %d", join_result);
        } else {
            LOG_I("QR_SCAN", "Successfully joined timeout thread during stop");
        }
        qr_scan_timeout_thread_created = false;
    }

    // Reset state variables
    first_time_scan = true;
    buf_write_index = 0;
    is_yplane_buf_present = false;

    // Free allocated Y-plane buffers
    if (y_planes) {
        free(y_planes);
        y_planes = NULL;
    }

    LOG_I("QR_SCAN", "QR scanning stopped and resources cleaned up");

    return true;
}

bool device_set_privacy(int cam_num, int privacy)
{
    recorder_test.cam_ctxt[cam_num].privacy = (privacy == 0) ? false : true;
    return true;
}

UINT32 drop_kinesis_frame_cb(UINT64 customData, STREAM_HANDLE streamHandle, UINT64 frameTimecode)
{
    UNUSED_PARAM(customData);
    UNUSED_PARAM(streamHandle);
    UNUSED_PARAM(frameTimecode);
    cam_record_ctxt *context = NULL;
    int req_cam_id = -1;

    for (int cam_num = CAMERA_POSITION_FRONT; cam_num < CAMERA_POSITION_MAX; cam_num++) {
        if (recorder_test.cam_ctxt[cam_num].kinesis_req_cam_id != -1) {
            req_cam_id = recorder_test.cam_ctxt[cam_num].kinesis_req_cam_id;
            break;
        }
    }

    if (req_cam_id == -1) {
        LOG_E("kinesis", "kinesis_req_cam_id is -1");
        return STATUS_SUCCESS;
    }

    context = (cam_record_ctxt *)(&(recorder_test.cam_ctxt[req_cam_id]));
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
            context->live_stream_fram_drop_cb(context->app_cb, req_cam_id);
            livestream_framedrop_cnt = 0;
            return STATUS_SUCCESS;
        }
    }
    LOG_I("kinesis", "Reported droppedFrame callback");
    return STATUS_SUCCESS;
}

// write string to file and return true if success, else false
bool write_string_to_file(string file_name, string str)
{
  std::lock_guard<std::mutex> lock(dualStreamingFileMutex);
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
    int memfd = -1;
    unsigned char *decrypted_key_buf = nullptr;
    size_t decrypted_key_len = 0;
    bool dec_status = false;
    string priv_key_fd_path;
    int priv_key_fd = -1;

    cam_record_ctxt *context = (cam_record_ctxt *)data;
    string cloud_config_path = ND_DEVICE_REL_PATH+ "/latest/cloudconfig.ini";
    string device_config_path = "/home/ubuntu/config/deviceconfig.ini";
    string device_id, cloud_server;
    std::stringstream iot_thing_name_ss;
    string iot_thing_name;
    unsigned int ret = -1, attempt = 0;
    STATUS retStatus = STATUS_SUCCESS;
    black_frame_ls[CAMERA_POSITION_FRONT] = read_file(black_frame_outward_LS.c_str());
    black_frame_ls[CAMERA_POSITION_BACK] = read_file(black_frame_inward_LS.c_str());
    if (black_frame_ls[CAMERA_POSITION_FRONT] != NULL) {
        LOG_I("kinesis", "black_frame_ls[CAMERA_POSITION_FRONT] size = %d", black_frame_ls[CAMERA_POSITION_FRONT]->size);
    } else {
        LOG_E("kinesis", "black_frame_ls[CAMERA_POSITION_FRONT] is NULL");
    }
    if (black_frame_ls[CAMERA_POSITION_BACK] != NULL) {
        LOG_I("kinesis", "black_frame_ls[CAMERA_POSITION_BACK] size = %d", black_frame_ls[CAMERA_POSITION_BACK]->size);
    } else {
        LOG_E("kinesis", "black_frame_ls[CAMERA_POSITION_BACK] is NULL");
    }
    LOG_I("kinesis", "LIVE stream Enter start kinesis in gst_recorder");

    Config_parser cloud_config_parser(cloud_config_path);
    Config_parser device_config_parser(device_config_path);
    if (cloud_config_parser.getParseStatus() && device_config_parser.getParseStatus()) {
        device_id = device_config_parser.getConfig("identity","deviceId","");
        if (device_id == "")
            device_id = device_config_parser.getConfig("identity","deviceid","");

        cloud_server = cloud_config_parser.getConfig("cloud","server","prod");

        if (cloud_server == "prod")
            iot_thing_name_ss << "production" << "-" << device_id.c_str();
        else
            iot_thing_name_ss << cloud_server.c_str() << "-" << device_id.c_str();

        iot_thing_name = iot_thing_name_ss.str();
        nd_strncpy(context->kinesis_stream_info.iot_str, iot_thing_name.c_str(), sizeof(context->kinesis_stream_info.iot_str));
        LOG_I("*********** kinesis", "#*#*#*#*#*# LIVE stream iot_thing_name = %s", context->kinesis_stream_info.iot_str);
    } else {
        LOG_E("kinesis", "Parsing of config files failed. Exiting!!");
        kinesis_req_msg->error = LS_ERR_UNKNOWN;
        return false;
    }

    /* copy the requested changes */
    context->kinesis_stream_info.req_stream.duration = kinesis_req_msg->duration;
    context->kinesis_stream_info.req_stream.bitrate = kinesis_req_msg->bitrate;
    context->kinesis_stream_info.req_stream.fps = kinesis_req_msg->fps;
    nd_strncpy(context->kinesis_stream_info.req_stream.resolution, kinesis_req_msg->resolution, sizeof(char) * 10);
    nd_strncpy(context->kinesis_stream_info.req_stream.endpoint, kinesis_req_msg->endpoint, sizeof(char) * 100);
    context->kinesis_stream_info.req_stream.camera = kinesis_req_msg->camera;
    context->kinesis_stream_info.req_stream.req_id = kinesis_req_msg->req_id;
    context->kinesis_stream_info.req_stream.id = kinesis_req_msg->id;
    context->kinesis_stream_info.frameIndex = 0;
    context->kinesis_stream_info.frame.frameData = NULL;
    context->kinesis_req_cam_id = kinesis_req_msg->camera;
    livestream_framedrop_cnt = 0;

    int livestream_bitrate = 500000;

    if (context->kinesis_req_cam_id == CAMERA_POSITION_FRONT) {
        livestream_bitrate = live_bitrate_front;
    } else {
        livestream_bitrate = live_bitrate_back;
    }
    LOG_I("kinesis", "LIVE stream starting for kinesis_req_cam_id = %d", context->kinesis_req_cam_id);

    // Create default device info, default storage size is 128MB.
    if (STATUS_SUCCESS != createDefaultDeviceInfo(&context->kinesis_stream_info.pDeviceInfo)) {
        LOG_E("kinesis", "LIVE stream Failed to createDefaultDeviceInfo");
        kinesis_req_msg->error = LS_ERR_UNKNOWN;
        CHK(FALSE, ret);
    }
    context->kinesis_stream_info.pDeviceInfo->clientInfo.loggerLogLevel = LOG_LEVEL_DEBUG;
    context->kinesis_stream_info.pDeviceInfo->storageInfo.storageSize = DEFAULT_STORAGE_SIZE;

    /* create and initialize the StreamInfo object */
    if(kinesis_req_msg->dual_streaming) {
        LOG_I("kinesis","Dual Live streaming mode, stream_name:%s\n", kinesis_req_msg->stream_name);
        ret = createRealtimeVideoStreamInfoProvider(kinesis_req_msg->stream_name, DEFAULT_RETENTION_PERIOD, DEFAULT_BUFFER_DURATION, &context->kinesis_stream_info.pStreamInfo);
    }
    else{
        ret = createRealtimeVideoStreamInfoProvider(context->kinesis_stream_info.iot_str, DEFAULT_RETENTION_PERIOD, DEFAULT_BUFFER_DURATION, &context->kinesis_stream_info.pStreamInfo);
    }
    if(STATUS_SUCCESS != ret)
    {
    	LOG_E("kinesis","LIVE stream Failed to createRealtimeVideoStreamInfoProvider");
        kinesis_req_msg->error = LS_ERR_UNKNOWN;
        CHK(FALSE, ret);
    }

    if (STATUS_SUCCESS != setStreamInfoBasedOnStorageSize(DEFAULT_STORAGE_SIZE,
                                                          livestream_bitrate, 1,
                                                          context->kinesis_stream_info.pStreamInfo)) {
        LOG_E("kinesis", "LIVE stream Failed to setStreamInfoBasedOnStorageSize");
        kinesis_req_msg->error = LS_ERR_UNKNOWN;
        CHK(FALSE, ret);
    }

    /* Iot certificate validation */
    LOG_I("kinesis", "LIVE stream createDefaultCallbacksProviderWithIotCertificate calling");
    attempt = 1;
    
    // Decrypt the private key into a buffer
    decrypted_key_len = nd_file_reoperate_to_buffer(IOT_CERT_PRIVATE_KEY_PATH, &decrypted_key_buf);
    if(decrypted_key_len <=0 || decrypted_key_buf == NULL) {   
        LOG_E("kinesis", "Failed to decrypt IOT_CERT_PRIVATE_KEY_PATH to buffer");
        return false;
    }

    try {
        string keyData(reinterpret_cast<const char*>(decrypted_key_buf), decrypted_key_len);

        bool memfd_status = createMemfdFromBuffer(keyData, priv_key_fd, "gst_recorder_krait_priv_key");

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

    while (attempt <= MAX_RETRY_ATTEMPTS) {
        LOG_I("kinesis", ">> LIVE stream createDefaultCallbacksProviderWithIotCertificate attempt %d", attempt);
        ret = createDefaultCallbacksProviderWithIotCertificate(context->kinesis_stream_info.req_stream.endpoint, \
                                                               IOT_CERT_PATH, (PCHAR)priv_key_fd_path.c_str(), CA_CERT_PATH, \
                                                               IOT_ROLE_ALIAS, context->kinesis_stream_info.iot_str, \
                                                               DEFAULT_AWS_REGION, NULL, NULL, &context->kinesis_stream_info.pClientCallbacks);
        if (STATUS_SUCCESS != ret) {
            LOG_E("kinesis", "*** LIVE stream Failed to createDefaultCallbacksProviderWithIotCertificate err - %d", ret);
            if (attempt == MAX_RETRY_ATTEMPTS){
                kinesis_req_msg->error = LS_ERR_BAD_LTE;
                close(priv_key_fd);
                CHK(FALSE, ret);
            }
            attempt++;
        } else {
            break;
        }
    }
    if (priv_key_fd != -1) close(priv_key_fd);
    LOG_I("kinesis", "*** LIVE stream working to createDefaultCallbacksProviderWithIotCertificate ret - %d", ret);

#ifdef DEBUG
    addFileLoggerPlatformCallbacksProvider(context->kinesis_stream_info.pClientCallbacks, \
                                           FILE_LOGGING_BUFFER_SIZE, MAX_NUMBER_OF_LOG_FILES, \
                                           (PCHAR) QMMF_FILE_LOGGER_LOG_FILE_DIRECTORY_PATH, TRUE);
#endif
    if (STATUS_SUCCESS != createStreamCallbacks(&context->kinesis_stream_info.pStreamCallbacks)) {
        LOG_E("kinesis", "LIVE stream Failed createStreamCallbacks");
        kinesis_req_msg->error = LS_ERR_BAD_LTE;
        CHK(FALSE, ret);
    }

    /* set framedrop call back */
    context->kinesis_stream_info.pStreamCallbacks->droppedFrameReportFn = drop_kinesis_frame_cb;

    if (STATUS_SUCCESS != addStreamCallbacks(context->kinesis_stream_info.pClientCallbacks,
                                             context->kinesis_stream_info.pStreamCallbacks)) {
        LOG_E("kinesis", "LIVE stream Failed to addStreamCallbacks");
        kinesis_req_msg->error = LS_ERR_BAD_LTE;
        CHK(FALSE, ret);
    }

    /* Initializing and configuring KinesisVideoClient and KinesisVideoStream for the pipeline */
    if (STATUS_SUCCESS != createKinesisVideoClient(context->kinesis_stream_info.pDeviceInfo,
                                                   context->kinesis_stream_info.pClientCallbacks,
                                                   &context->kinesis_stream_info.clientHandle)) {
        LOG_E("kinesis", "LIVE stream Failed at createKinesisVideoClient");
        kinesis_req_msg->error = LS_ERR_BAD_LTE;
        CHK(FALSE, ret);
    }

    if(STATUS_SUCCESS != createKinesisVideoStreamSync(context->kinesis_stream_info.clientHandle,
                                                      context->kinesis_stream_info.pStreamInfo,
                                                      &context->kinesis_stream_info.streamHandle)) {
        LOG_E("kinesis", "LIVE stream Failed at createKinesisVideoClient");
        kinesis_req_msg->error = LS_ERR_BAD_LTE;
        CHK(FALSE, ret);
    }

    /* allocate memory for frame */
    if (context->kinesis_stream_info.frame.frameData == NULL)
        context->kinesis_stream_info.frame.frameData = (BYTE *) calloc(sizeof(char), MAX_FRAME_BUFFER_SIZE);

    //Enable streaming
	context->kinesis_stream_info.first_time = true;
	streaming = false;
    kinesis_dual_streaming = kinesis_req_msg->dual_streaming;
    if (context->kinesis_req_cam_id == CAMERA_POSITION_FRONT){
        streaming_outward = false;
    }
    else {
        streaming_inward = false;
    }
    if(kinesis_req_msg->dual_streaming) {
        kinesis_dual_streaming = true;
        if(kinesis_req_msg->camera == DEVICE_CAMERA_POSITION_FRONT){
            file_touch(TMP_KINESIS_STREAMING_OUTWARD);
            write_string_to_file(TMP_KINESIS_STREAMING_OUTWARD, "1");
            streaming_outward = false;
        }
        else if(kinesis_req_msg->camera == DEVICE_CAMERA_POSITION_BACK){
            file_touch(TMP_KINESIS_STREAMING_INWARD);
            write_string_to_file(TMP_KINESIS_STREAMING_INWARD, "1");
            streaming_inward = false;
        }
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

    LOG_I("kinesis","LIVE stream Exit start kinesis");
    return true;
}

bool stop_kinesis_stream_task(void *args)
{
	cam_record_ctxt *context = (cam_record_ctxt *)args;

	LOG_I("kinesis", "LIVE stream FORCE stop kinesis");
    if (STATUS_SUCCESS != stopKinesisVideoStream(context->kinesis_stream_info.streamHandle)) {
        LOG_I("kinesis", "time task stop_kinesis_stream_task failed");
        return false;
    }
    return true;
}

bool stop_kinesis_sync_task(void *args)
{
	cam_record_ctxt *context = (cam_record_ctxt *)args;

	LOG_I("kinesis", "LIVE stream stop kinesis");
    if (STATUS_SUCCESS != stopKinesisVideoStreamSync(context->kinesis_stream_info.streamHandle)) {
        LOG_I("kinesis", "time task stop_kinesis_sync_task failed");
        return false;
    }
    return true;
}

bool stopkinesis (void *data, req_livestreaming_data_t *kinesis_req_msg)
{
    //Deleting both files because stopkinesis will not be called for the camera which is disabled or privacy is enabled
    file_delete(TMP_KINESIS_STREAMING_INWARD);
    file_delete(TMP_KINESIS_STREAMING_OUTWARD);
    kinesis_dual_streaming = false;
    cam_record_ctxt *context = (cam_record_ctxt *)data;
    context -> prevFrameState = VALID_FRAME;
    task_result_t timed_task_result;

    // Stop streaming
    streaming = true;
    streaming_outward = true;
    streaming_inward = true;
    context->kinesis_req_cam_id = -1;
    context->kinesis_stream_info.first_time = false;

    /* free the frame buffer */
    if (context->kinesis_stream_info.frame.frameData != NULL) {
        free(context->kinesis_stream_info.frame.frameData);
        context->kinesis_stream_info.frame.frameData = NULL;
    }
    /* Stop live stream immediately */
    /*if (kinesis_req_msg->force_stop == true) {
        timed_task_result = nd_timed_task(stop_kinesis_stream_task, INIT_CAM_CRASH_THREAD_INTERVAL_IN_SECS, context, "stop_kinesis_stream_task");
        if (timed_task_result != TASK_SUCCESS) {
            LOG_E ("kinesis", "nd_timed_task for stop_kinesis_stream_task failed");
            return false;
        }
    } else {// Stop live stream synchronously
        timed_task_result = nd_timed_task(stop_kinesis_sync_task, INIT_CAM_CRASH_THREAD_INTERVAL_IN_SECS, context, "stop_kinesis_sync_task");
        if (timed_task_result != TASK_SUCCESS) {
            LOG_E ("kinesis", "nd_timed_task for stop_kinesis_sync_task failed");
        }
    }*/
    stopKinesisVideoStream(context->kinesis_stream_info.streamHandle);
    livestream_framedrop_cnt = 0;

    /* Deininitialize and free API calls */
    if (context->kinesis_stream_info.pDeviceInfo != NULL)
        freeDeviceInfo(&context->kinesis_stream_info.pDeviceInfo);

    if (context->kinesis_stream_info.pStreamInfo != NULL)
        freeStreamInfoProvider(&context->kinesis_stream_info.pStreamInfo);

    if (IS_VALID_STREAM_HANDLE(context->kinesis_stream_info.streamHandle))
        freeKinesisVideoStream(&context->kinesis_stream_info.streamHandle);

    if (IS_VALID_CLIENT_HANDLE(context->kinesis_stream_info.clientHandle))
        freeKinesisVideoClient(&context->kinesis_stream_info.clientHandle);

    if (context->kinesis_stream_info.pClientCallbacks != NULL)
        freeCallbacksProvider(&context->kinesis_stream_info.pClientCallbacks);

    if (context->cam_pos == DEVICE_CAMERA_POSITION_FRONT &&  black_frame_ls[CAMERA_POSITION_FRONT] != NULL) {
        free(black_frame_ls[CAMERA_POSITION_FRONT]);
        black_frame_ls[CAMERA_POSITION_FRONT] = NULL;
    }

    if (context->cam_pos == DEVICE_CAMERA_POSITION_BACK && black_frame_ls[CAMERA_POSITION_BACK] != NULL) {
        free(black_frame_ls[CAMERA_POSITION_BACK]);
        black_frame_ls[CAMERA_POSITION_BACK] = NULL;
    }

    LOG_I("kinesis","LIVE stream Exit stop kinesis");
    return TRUE;
}

void *cam_check_thread(void *arg)
{
    int64_t curr_time = get_system_monotonic_time ();
    int64_t prev_time = get_system_monotonic_time ();
    float fps;
    int cam_check_interval = INIT_CAM_CRASH_THREAD_INTERVAL_IN_SECS;
    while (1) {
        sleep(cam_check_interval);
        cam_check_interval = CAM_CRASH_THREAD_INTERVAL_IN_SECS;
        curr_time = get_system_monotonic_time ();
        obj_context_t *obj = NULL;
        cam_crash_status_t *error_status = NULL;
        if(recorder_test.cam_ctxt[CAMERA_POSITION_FRONT].app_cb != NULL) {
            obj = (obj_context_t *)recorder_test.cam_ctxt[CAMERA_POSITION_FRONT].app_cb;

            if(obj != NULL) {
                error_status = (cam_crash_status_t *)obj->app_error_ctxt;
            }

            for(int i=0; i<CAMERA_POSITION_MAX; i++) {
                pthread_mutex_lock(&recorder_test.cam_ctxt[i].cam_frame_mutex);
                // condition check if camera enabled
                //if(recorder_test.cam_ctxt[i].cam_frame_count != 0) {
                if (NULL != recorder_test.cam_ctxt[i].event_cb) {
                    if (recorder_test.cam_ctxt[i].cam_frame_count == 0) {
                        LOG_E(log_tag[i], "No frames generated from camera, do error callback");
                        error_status->status[i] = true;
                    } else {
                        LOG_I(log_tag[i], "Frame count recieved: %lld ", recorder_test.cam_ctxt[i].cam_frame_count);
                        // time diff is in milli seconds
                        fps = ( recorder_test.cam_ctxt[i].cam_frame_count * 1000.0 ) / (curr_time - prev_time);
                        if((fps < FPS_THRESHOLD_FOR_CAM_CRASH) && (error_status != NULL)) {
                            error_status->status[i] = true;
                            LOG_E(log_tag[i], "cam is not giving frames, do error callback: count = %lld, fps = %f", recorder_test.cam_ctxt[i].cam_frame_count, fps);
                        }
                        recorder_test.cam_ctxt[i].cam_frame_count = 0;
                    }
                }
                pthread_mutex_unlock(&recorder_test.cam_ctxt[i].cam_frame_mutex);
            }
        }

        for(int i = 0; i < CAMERA_POSITION_MAX; i++) {
            if ((obj == NULL) || (error_status == NULL)) {
                continue;
            }
            if(error_status->status[i] == true) {
                if (NULL != recorder_test.cam_ctxt[i].event_cb) {
                    recorder_test.cam_ctxt[i].event_cb(GST_RECORD_EVENT_PIPELINE_ERR, recorder_test.cam_ctxt[i].app_cb);
                    break;
                }
            }
        }
        prev_time = curr_time;
    }
}

RecorderTest::RecorderTest() :
            camera_id_(0),
            session_enabled_(false),
            camera_error_(false)
{
    LOG_I(log_tag[camera_id_], "%s: Enter", __func__);
    char prop_val[PROPERTY_VALUE_MAX];

    property_get(PROP_DUMP_BITSTREAM, prop_val, "1");
    is_dump_bitstream_enabled_ = (atoi(prop_val) == 0) ? false : true;

    property_get(PROP_DUMP_YUV, prop_val, "1");
    is_dump_yuv_enabled_ = (atoi(prop_val) == 0) ? false : true;

    property_get(PROP_DUMP_RAW, prop_val, "1");
    is_dump_raw_enabled_ = (atoi(prop_val) == 0) ? false : true;

    property_get(PROP_DUMP_JPEG, prop_val, "1");
    is_dump_jpg_enabled_ = (atoi(prop_val) == 0) ? false : true;

    property_get(PROP_DUMP_FRAME_FREQ, prop_val, DEFAULT_DUMP_FRAME_FREQ);
    dump_frame_freq_ = atoi(prop_val);

    property_set(PROP_SCALER_TYPE, "C2D");
    property_set(PROP_UBWC_ENABLE, "0");

    LOG_I(log_tag[camera_id_], "is_dump_bitstream_enabled_ = %d", is_dump_bitstream_enabled_);
    LOG_I(log_tag[camera_id_], "is_dump_yuv_enabled_ = %d", is_dump_yuv_enabled_);
    LOG_I(log_tag[camera_id_], "is_dump_raw_enabled_ = %d", is_dump_raw_enabled_);
    LOG_I(log_tag[camera_id_], "is_dump_jpg_enabled_ = %d", is_dump_jpg_enabled_);
    LOG_I(log_tag[camera_id_], "dump_frame_freq_ = %d", dump_frame_freq_);

    LOG_I(log_tag[camera_id_], "%s: Exit", __func__);
}

RecorderTest::~RecorderTest()
{
    LOG_I(log_tag[camera_id_], "%s: Enter", __func__);
    LOG_I(log_tag[camera_id_], "%s: Exit", __func__);
}

void RecorderTest::RecorderEventCallbackHandler(EventType event_type,
                                                void *event_data,
                                                size_t event_data_size)
{
    LOG_I(log_tag[camera_id_], "%s: Enter", __func__);
    if (event_type == EventType::kServerDied) {
        // qmmf-server died, reason could be non recoverable FATAL error,
        // qmmf-server runs as a daemon and gets restarted automatically, on death
        // event application can cleanup all its resources and connect again.
        LOG_E(log_tag[camera_id_], "%s: Recorder Service died!", __func__);
    } else if (event_type == EventType::kCameraError) {
        LOG_E(log_tag[camera_id_], "%s: Found CameraError", __func__);
        if (event_data != NULL) {
            RecorderErrorData *camera_error_data;
            camera_error_data = (RecorderErrorData *)event_data;
            /* These error codes are defined in qmmf_camera3_types.h */
            switch(camera_error_data->error_code) {
                case -1: /* ERROR_CAMERA_INVALID_ERROR */
                    LOG_E(log_tag[camera_id_], "Invalid camera error");
                    break;
                case 0: /* ERROR_CAMERA_DISCONNECTED */
                    LOG_E(log_tag[camera_id_], "Camera is disconnected");
                    break;
                case 1: /* ERROR_CAMERA_DEVICE */
                    LOG_E(log_tag[camera_id_], "Unrecoverable camera error");
                    break;
                case 2: /* ERROR_CAMERA_SERVICE */
                    LOG_E(log_tag[camera_id_], "Camera service error");
                    break;
                case 3: /* ERROR_CAMERA_REQUEST */
                    LOG_E(log_tag[camera_id_], "Error during camera request processing");
                    break;
                case 4: /* ERROR_CAMERA_RESULT */
                    LOG_E(log_tag[camera_id_], "Error during camera result generating");
                    break;
                case 5: /* ERROR_CAMERA_BUFFER */
                    LOG_E(log_tag[camera_id_], "Error during camera buffer processing");
                    break;
                default:
                    LOG_E(log_tag[camera_id_], "Undefined camera error");
                    break;
            }
        }
        std::lock_guard<std::mutex> lock(error_lock_);
        camera_error_ = true;
     }
     LOG_I(log_tag[camera_id_], "%s: Exit", __func__);
}

void RecorderTest::SessionCallbackHandler(EventType event_type,
                                          void *event_data,
                                          size_t event_data_size)
{
    LOG_I(log_tag[camera_id_], "%s: Enter", __func__);
    LOG_I(log_tag[camera_id_], "%s: Exit", __func__);
}

void RecorderTest::CameraResultCallbackHandler(uint32_t camera_id,
                                               const CameraMetadata &result)
{
    LOG_D(log_tag[camera_id_], "%s: Enter", __func__);
    LOG_D(log_tag[camera_id_], "%s: Exit", __func__);
}

bool frame_shm_write_cb_func(int smb_id, int64_t uid, void *data_ptr, int64_t data_len, void *user_data)
{
    cam_record_ctxt *context = (cam_record_ctxt *)user_data;
    int camera_num = 0;
    if (context->cam_pos == CAMERA_POSITION_BACK) {
        camera_num = 1;
    }

    int aligned_height = ((int)(context->rt_config.height / 32) + 1) * 32;
    cv::Mat input_mat = cv::Mat(aligned_height * 3 / 2, context->rt_config.width, CV_8UC1, (uint8_t *)context->frame_data_ptr);

    cv::Mat input_bgr_mat;
    cv::cvtColor(input_mat, input_bgr_mat, CV_YUV2BGR_NV12);
    cv::Mat resize_mat = input_bgr_mat;

    cv::Mat output_mat = cv::Mat(context->rt_config.height, context->rt_config.width, CV_8UC3, (uint8_t *)data_ptr);
    LOG_D(log_tag[camera_num], "output_mat: rows: %d, cols: %d, ptr: %x", output_mat.rows, output_mat.cols, output_mat.data);

    cv::Rect img_roi;
    img_roi.x = 0;
    img_roi.y = 0;
    img_roi.width = context->rt_config.width;
    img_roi.height = context->rt_config.height;

    cv::Mat cropped_mat(resize_mat, img_roi);
    LOG_D(log_tag[camera_num], "cropped_mat: rows: %d, cols: %d, ptr: %x", cropped_mat.rows, cropped_mat.cols, cropped_mat.data);

    if ((cropped_mat.rows != output_mat.rows) || (cropped_mat.cols != output_mat.cols)) {
        resize(cropped_mat, output_mat, output_mat.size(), 0, 0, CV_INTER_LINEAR);
    } else {
        cropped_mat.copyTo(output_mat);
    }

#if DUMP_DEBUG_FRAME
    string fname = "/home/ubuntu/.nddevice/RT_bagheera_CAM" + to_string(camera_num) + "_" + to_string(smb_id) + to_string(uid) + ".png";
    cv::imwrite(fname, output_mat);
#endif
    if (data_len != output_mat.total() * output_mat.elemSize()) {
        LOG_E(log_tag[camera_num], "data_lengths didn't match");
        return false;
    }

    return true;
}

// This function dumps YUV, JPEG and RAW frames to file.
qmmf_status_t RecorderTest::DumpFrameToFile(BufferDescriptor& buffer,
                                       CameraBufferMetaData& meta_data,
                                       std::string& file_path, int cam_num)
{
    size_t written_len = 0;
    cam_record_ctxt *context = (cam_record_ctxt *)(&(recorder_test.cam_ctxt[cam_num]));

    if ((cam_num == CAMERA_POSITION_FRONT) && (drop_outcam_rt_frames == true)) {
        LOG_E (log_tag[context->cam_pos], "Dropping outward cam RT frames because outcam shared memory will be recreated.");
        return NO_ERROR;
    } else if ((cam_num == CAMERA_POSITION_BACK) && (drop_incam_rt_frames == true)) {
        LOG_E (log_tag[context->cam_pos], "Dropping inward cam RT frames because incam shared memory will be recreated.");
        return NO_ERROR;
    }

    // JPEG, RAW & NV12UBWC
    if ((meta_data.format == BufferFormat::kBLOB) ||
        (meta_data.format == BufferFormat::kRAW10) ||
        (meta_data.format == BufferFormat::kRAW8) ||
        (meta_data.format == BufferFormat::kRAW12) ||
        (meta_data.format == BufferFormat::kNV12UBWC)){
    } else {
        static int count = 0;
        int smb_id = -1;
        int64_t uid = -1;
        int64_t data_len = -1;
        bool bIsExit = false;
        pthread_mutex_lock(&context->shm_writer_mutex);
        if (context->shm_writer) {
            context->frame_data_ptr = buffer.data;
            if (!context->shm_writer->get_free_smb(context, smb_id, uid, data_len, cam_num, bIsExit)) {
                LOG_E(log_tag[cam_num], "failed to write data to smb, dropping frame");

                /* commenting the following lines as there is a possibility of replenishing
                 * the shared memory buffer once the analytics service restarts.
                 */
#if 0
                if (bIsExit) {
                    obj_context_t *obj = NULL;
                    cam_crash_status_t *error_status = NULL;
                    obj = (obj_context_t *)context->app_cb;
                    if (obj != NULL) {
                        error_status = (cam_crash_status_t *)obj->app_error_ctxt;
                        error_status->status[cam_num] = true;
                    }
                    context->event_cb(GST_RECORD_EVENT_SHM_FAILED, context->app_cb);
                }
#endif
            } else {
                int dev_cam_num = 0;
                if (cam_num == CAMERA_POSITION_BACK) {
                    dev_cam_num = DEVICE_CAMERA_POSITION_BACK;
                } else { //CAMERA_POSITION_FRONT
                    dev_cam_num = DEVICE_CAMERA_POSITION_FRONT;
                }

                // logic to get the new session start time from first frame
                if (context->first_frame_pts == 0) {
                    context->first_frame_pts = buffer.timestamp;
                    LOG_I(log_tag[cam_num], "first_frame_pts = %lld", context->first_frame_pts);
                }
                LOG_D(log_tag[cam_num], "RT frame pts = %lld", buffer.timestamp);
                uint64_t raw_frame_sent_time_ms = get_system_time();
                /* buffer TS is coming as monotonic, need to calculate the epoch TS using that */
                uint64_t epoch_time_ns = get_system_time_ns() - (get_system_monotonic_time_ns() - buffer.timestamp);
                LOG_D(log_tag[cam_num], "RT frame TS mono = %lld, epoch = %lld", buffer.timestamp, epoch_time_ns);
                context->appsink_cb((buffer.timestamp - context->first_frame_pts), dev_cam_num, 0, epoch_time_ns, raw_frame_sent_time_ms, data_len, smb_id, uid, context->app_cb);
            }
        } else {
            LOG_E(log_tag[cam_num], "shm writer null. so not triggering shm write callback");
        }
        pthread_mutex_unlock(&context->shm_writer_mutex);

#if DUMP_DEBUG_FRAME
    string fname = "/home/ubuntu/.nddevice/RT_bagheera_CAM" + to_string(cam_num) + "_" + to_string(smb_id) + to_string(uid) + ".raw";
    FILE *file = fopen(fname.c_str(), "w+");
    void *temp;
    int offset = 0;
    for (uint32_t i = 0; i < meta_data.num_planes; ++i) {
        temp = static_cast<void*>((static_cast<uint8_t*>(buffer.data)
                    +                      + offset));
        written_len += fwrite(temp, sizeof(uint8_t), meta_data.plane_info[i].width * meta_data.plane_info[i].height, file);
        LOG_I(log_tag[cam_num], "width = %d, height = %d, stride = %d, scanline = %d\n",
              meta_data.plane_info[i].width,
              meta_data.plane_info[i].height,
              meta_data.plane_info[i].stride,
              meta_data.plane_info[i].scanline);
        offset += meta_data.plane_info[i].stride * (meta_data.plane_info[i].scanline);
    }
    fclose(file);
#endif
    }
    return NO_ERROR;
}

static void set_qmmf_restart_count(int count) {
    // Ensure directory exists before creating the file
    struct stat st;
    if (stat("/dev/shm/nd_files_c", &st) != 0) {
        mkdir("/dev/shm/nd_files_c", 0777);
    }
    std::ofstream outfile(QMMF_RESTART_COUNT_FILE, std::ios::trunc);
    if (outfile.is_open()) {
        outfile << count;
        outfile.close();
    }
}

static int get_qmmf_restart_count() {
    struct stat st;
    // Check if file exists
    if (stat(QMMF_RESTART_COUNT_FILE, &st) != 0) {
        // File does not exist, create it and write 0
        set_qmmf_restart_count(0);
        return 0;
    }
    // File exists, read value
    std::ifstream infile(QMMF_RESTART_COUNT_FILE);
    int count = 0;
    if (infile.is_open()) {
        infile >> count;
        infile.close();
    }
    return count;
}

void restart_qmmf_server()
{
    // Limit qmmf-server restarts to QMMF_SERVER_MAX_RESTARTS_PER_BOOT per boot cycle because if the camera fails continuously to start,
    // we should not keep restarting qmmf-server endlessly, as it can affect other services.
    int restart_count = get_qmmf_restart_count();
    if (restart_count >= QMMF_SERVER_MAX_RESTARTS_PER_BOOT) {
        LOG_E(LOG_TAG, "QMMF server restart count is %d (max allowed: %d) for this boot cycle. Not restarting qmmf-server.", restart_count, QMMF_SERVER_MAX_RESTARTS_PER_BOOT);
        return;
    }
    set_qmmf_restart_count(restart_count+1);

    LOG_I(LOG_TAG, "Restarting qmmf-server");
    service_task_args service_task = {"qmmf-server.service",sysctl_action::eSYSCTL_RESTART,false};
    task_result_t time_task_result = nd_timed_task(sysctl_task_tt, SERVICE_RESTART_TIMEOUT_SECS, (void*)&service_task, "restart service", true);
    if(TASK_SUCCESS != time_task_result){
        LOG_C(LOG_TAG,"nd_timed_task failed for sysctl_task_tt with status : %d",(int)time_task_result);
        return;
    }
    LOG_I(LOG_TAG, "qmmf-server restarted successfully");
    return;
}

struct sigevent sev;
struct itimerspec its;
long long freq_nanosecs;
sigset_t mask;
struct sigaction sa;
timer_t timerid;
bool timer_set = false;

static void handler(int sig, siginfo_t *si, void *uc)
{
    if(si->si_value.sival_ptr != &timerid){
        LOG_I(log_tag[0], "Stray signal\n");
    } else {
        LOG_I(log_tag[0], "Caught signal %d from timer \n", sig);
        session_change[0] = true;
        session_change[1] = true;
    }
}

/* Reference:
 * https://github.com/csimmonds/periodic-threads/blob/master/timerfd.c
 */
void* session_change_thread(void *args)
{
    int ret;
    uint32_t sec;
    int timer_fd;
    struct itimerspec itval;
    uint64_t missed;

    session_count = 0;

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

    while(1) {
        /* Wait for the next timer event. If we have missed any the
           number is written to "missed" */
        ret = read(timer_fd, &missed, sizeof(missed));
        LOG_I(log_tag[0], "%s: hitting timer \n", __func__);
        session_change[0] = true;
        //session_change[1] = true;
    }

    return NULL;
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

    if ((NULL == context) || (NULL == file_cb) || (NULL == event_cb) || (NULL == timestamp_cb))
        return false;

    context->record_cb = file_cb;
    context->event_cb = event_cb;
    context->frame_write_cb = frame_write_cb;
    context->timestamp_cb = timestamp_cb;
    context->data_prod_cb = data_prod_cb;
    context->app_cb  = app;

    return true;
}

int set_fname_callback_platform(void *ptr,record_fname_cb_t fname_cb,
                                 void *app)
{
    cam_record_ctxt *context = (cam_record_ctxt *)ptr;

    if (NULL == context || NULL == fname_cb)
        return false;

    context->fname_cb = fname_cb;
    context->app_fname  = app;

    LOG_I(log_tag[context->cam_pos], "Registered fname callback");

    return true;
}

//Set live stream frame drop callback
int set_live_stream_frame_drop_callback_platform(void *ptr, live_stream_frame_drop_cb_t live_stream_frame_drop_cb)
{
    cam_record_ctxt *context = (cam_record_ctxt *)ptr;

    if (NULL == context || NULL == live_stream_frame_drop_cb)
        return false;

    context->live_stream_fram_drop_cb = live_stream_frame_drop_cb;
    // context->app_fname  = app;

    LOG_I(log_tag[context->cam_pos], "Registered live stream frame drop callback");

    return true;
}

// Set QR scan callback
int set_qr_scan_callback_platform(void *ptr, qr_scan_cb_t qr_scan_cb)
{
    cam_record_ctxt *context = (cam_record_ctxt *)ptr;

    if ((context == NULL) || (qr_scan_cb == NULL))
        return false;

    context->qr_scan_cb = qr_scan_cb;

    LOG_I(log_tag[context->cam_pos], "Registered QR scan callback");

    return true;
}

int set_appsink_callback_platform(void *ptr, record_appsink_cb_t appsink_cb /*, void *app*/ )
{
    cam_record_ctxt *context = (cam_record_ctxt *)ptr;

    if (NULL == context || NULL == appsink_cb)
        return false;

    context->appsink_cb = appsink_cb;
    //context->app_fname  = app;

    LOG_I(log_tag[context->cam_pos], "Registered appsink callback");

    return true;
}

bool connect_with_timeout_task(void *args) {
    connect_task_args *task_args = (connect_task_args *)args;
    task_args->result = recorder_test.recorder_.Connect(*(task_args->recorder_status_cb));
    return true;
}

void* init_camera_record_platform(native_camera_config_t native_cam_config, realtime_camera_config_t* rt_config)
{
    int32_t ret;
    int shm_buffers;
    string temp;
    int cam_num = 0;
    bool get_override_val = true, is_val_overridden = false;

    if (native_cam_config.cam_pos == DEVICE_CAMERA_POSITION_FRONT) {
        cam_num = CAMERA_POSITION_FRONT;
    } else { //CAMERA_POSITION_BACK
        cam_num = CAMERA_POSITION_BACK;
    }

    Config_parser *nd_config = new Config_parser(ND_CONFIG_INI);
    if (nd_config->getParseStatus() != true) {
        LOG_E(log_tag[cam_num], "Cannot allocate nd Config");
    }

    const char *tag = native_cam_config.name.c_str();
    int len = native_cam_config.name.length();

    ret = init_params(cam_num, rt_config);
    if (ret != 0)
        return NULL;

    // reset kinesis stream info
    memset(&recorder_test.cam_ctxt[cam_num].kinesis_stream_info, 0 , sizeof(recorder_test.cam_ctxt[cam_num].kinesis_stream_info));
    recorder_test.cam_ctxt[cam_num].kinesis_req_cam_id = -1;

    LOG_I(LOG_TAG, "%s Connect - Start\n",__func__);
    strncpy(log_tag[cam_num],tag,len);

    if (is_connected == false) {
        RecorderCb recorder_status_cb;
        recorder_status_cb.event_cb = [&] ( EventType event_type, void *event_data,
                       size_t event_data_size) { recorder_test.RecorderEventCallbackHandler(event_type,
                               event_data, event_data_size); };
        // Connect - Start
        connect_task_args connect_task_args;
        connect_task_args.result = -1;
        connect_task_args.recorder_status_cb = &recorder_status_cb;
        task_result_t connect_task_result;
        int32_t RETRY_COUNT_Connect = 5;
        while (RETRY_COUNT_Connect--) {
            // Made Connect api as a timed task to avoid blocking in case of api being stuck/hung due to bad state of qmmf-server.
            // If timeout occurs, qmmf-server is restarted and Connect is retried again for RETRY_COUNT_Connect times.
            connect_task_result = nd_timed_task(connect_with_timeout_task, QMMF_CONNECT_TIMEOUT_SECS, &connect_task_args, "connect_qmmf_recorder", false);
            if (connect_task_result == TASK_TIMEOUT) {
                LOG_E(log_tag[cam_num], "%s: Connect Timeout!!", __func__);
                sleep(RETRY_DELAY);
            } else if (NO_ERROR  != connect_task_args.result) {
                LOG_E(log_tag[cam_num], "%s Connect Failed with error code: %d!!", __func__, connect_task_args.result);
                sleep(RETRY_DELAY);
            } else {
                break;
            }
        }
        if ((connect_task_args.result != NO_ERROR) || (connect_task_result == TASK_TIMEOUT)) {
            string str_msg = "Connect failed/timed-out for camera_id " + to_string(cam_num);
            nd_service_obj->send_err_msg(SM_E_NDC_CONNECT_FAIL, cam_num, str_msg);
            restart_qmmf_server();
            return NULL;
        }
        // Connect - End
        is_connected = true;
        pthread_t session_th;
        pthread_create(&session_th, NULL, &session_change_thread, NULL);

    }

    recorder_test.cam_ctxt[cam_num].cam_pos = (camera_pos)cam_num;
    if (rt_config->enable_streaming) {
        memcpy(&recorder_test.cam_ctxt[cam_num].rt_config, rt_config, sizeof(realtime_camera_config_t));

        LOG_I(log_tag[cam_num], "RT fps for cam_num %d is %d", cam_num, rt_config->fps);

        if (cam_num == CAMERA_POSITION_FRONT) {
            temp = nd_config->getConfig("autocam", "shm_buffers", "5", get_override_val, is_val_overridden);
            if (!string_to_integer(temp.c_str(), shm_buffers))
                shm_buffers = 5;
            OUTWARD_MIN_INTRA_FRAME_INTERVAL_NS = ((1000 / rt_config->fps) * 1000 * 1000) - ((1000 / (30 * 2)) * 1000 * 1000);
        } else if (cam_num == CAMERA_POSITION_BACK) {
            temp = nd_config->getConfig("inwardRealTime", "shm_buffers", "5", get_override_val, is_val_overridden);
            if (!string_to_integer(temp.c_str(), shm_buffers))
                shm_buffers = 5;
            INWARD_MIN_INTRA_FRAME_INTERVAL_NS = ((1000 / rt_config->fps) * 1000 * 1000) - ((1000 / (30 * 2)) * 1000 * 1000);
        }
        LOG_I(log_tag[cam_num], "shm_buffers for cam_num %d are %d", cam_num, shm_buffers);

        // fill shm writer object into context and register write cb
        int64_t smb_data_size = (int64_t)(rt_config->width * rt_config->height * 3);
        recorder_test.cam_ctxt[cam_num].shm_writer = new NdSharedMemoryWriter(native_cam_config.name, smb_data_size, shm_buffers);

        recorder_test.cam_ctxt[cam_num].shm_buffers = shm_buffers;

        if (recorder_test.cam_ctxt[cam_num].shm_writer) {
            LOG_I(log_tag[cam_num],"Shared Memory Writer 0x%x created successfully for camera %d with smb_data_size: %lld and shm_buffers: %d",
                    recorder_test.cam_ctxt[cam_num].shm_writer, cam_num, smb_data_size, shm_buffers);

            recorder_test.cam_ctxt[cam_num].shm_writer->set_log_frequency(100);
            recorder_test.cam_ctxt[cam_num].shm_writer->register_write_callback(frame_shm_write_cb_func);

            recorder_test.cam_ctxt[cam_num].shm_writer_name = native_cam_config.name;
        }
        pthread_mutex_init (&recorder_test.cam_ctxt[cam_num].shm_writer_mutex, NULL);
    }
    pthread_mutex_init(&recorder_test.cam_ctxt[cam_num].cam_frame_mutex, NULL);
    pthread_mutex_init(&recorder_test.cam_ctxt[cam_num].rt_timestamp_mutex, NULL);

    return (void *)(&(recorder_test.cam_ctxt[cam_num]));
}

int init_record_session_platform(void *hndl)
{
    // StartCamera - Begin
    // TODO: this parameters to be configured from config file
    // once the proper lower layer support for zsl is added
    int ret = true;
    cam_record_ctxt *context = NULL;

    context = (cam_record_ctxt *) hndl;
    int cam_num = (int)context->cam_pos;
    CameraStartParam camera_params{};

    camera_params.zsl_mode            = false;
    camera_params.zsl_queue_depth     = 10;
    camera_params.zsl_width           = 3840;
    camera_params.zsl_height          = 2160;
    camera_params.flags               = 0x0;

    current_camera_id = cam_num;

    if (cam_num == CAMERA_POSITION_FRONT) {
        camera_params.frame_rate = 30;
    } else { // CAMERA_POSITION_BACK
        camera_params.frame_rate = 15;
    }
    CameraResultCb result_cb = [&] (uint32_t camera_id,
            const CameraMetadata &result) {
                recorder_test.CameraResultCallbackHandler(camera_id, result); };

    LOG_I(log_tag[cam_num], "%s StartCamera (%d)", __func__, current_camera_id);

    int32_t RETRY_COUNT_StartCamera = 5;
    while (RETRY_COUNT_StartCamera--) {
        ret = recorder_test.recorder_.StartCamera(current_camera_id, camera_params, result_cb);
        if (ret != 0) {
            LOG_E(log_tag[cam_num], "%s StartCamera (%d) Failed!", __func__, current_camera_id);
            sleep(RETRY_DELAY);
        } else {
            break;
        }
    }
    if (ret != 0) {
        string str_msg = "Start Camera failed for camera_id " + to_string(current_camera_id);
        nd_service_obj->send_err_msg(SM_E_NDC_START_CAMERA_FAIL, current_camera_id, str_msg);
        restart_qmmf_server();
        return 0;
    }
    // StartCamera - End

    LOG_I(log_tag[cam_num], "%s Create session and add track", __func__);

    std::vector<TestTrack*> tracks;
    // Session for encoder tracks
    SessionCb session_status_cb;
    session_status_cb.event_cb = [&] ( EventType event_type, void *event_data,
            size_t event_data_size) { recorder_test.SessionCallbackHandler(event_type,
                event_data, event_data_size); };

    int32_t RETRY_COUNT_CreateSession = 5;
    while (RETRY_COUNT_CreateSession--) {
        ret = recorder_test.recorder_.CreateSession(session_status_cb, &session_id[cam_num]);
        if (ret != 0) {
            LOG_E(log_tag[cam_num], "%s CreateSession Failed!!", __func__);
            sleep(RETRY_DELAY);
        } else {
            break;
        }
    }
    if (ret != 0) {
        string str_msg = "Create Session failed for camera_id " + to_string(cam_num);
        nd_service_obj->send_err_msg(SM_E_NDC_CREATE_SESSION_FAIL, cam_num, str_msg);
        restart_qmmf_server();
        return 0;
    }

    LOG_I(log_tag[cam_num], "%s: session_id = %d", __func__, session_id[cam_num]);

    if ((is_driverlogin_qr_enabled == true) && (cam_num == CAMERA_POSITION_BACK)) {
        g_track_id_qr = g_track_count[0] + g_track_count[1] + 1; // last track is QR track
        LOG_I(log_tag[cam_num], "QR track id is %d", g_track_id_qr);
    }

    for (uint32_t i = 1; i <= infos.size(); i++) {
        TrackInfo_t track_info = infos[i-1];
        LOG_I(log_tag[cam_num], "%s: before testtrack, trackinfo cam = %d, cam_num = %d \n", __func__, track_info.camera_id, cam_num);
        if (track_info.camera_id == cam_num) {
            TestTrack *video_track = new TestTrack(&recorder_test);
            track_info.track_id = i;
            // saving track ID for future use
            if ((i == hd_track_offset) || (i == g_track_count[0] + hd_track_offset)) {
                g_track_id_hd[cam_num] = i;
                LOG_I(log_tag[cam_num], "HD track id for cam %d is %d", cam_num, i);
            }
            if (context->rt_config.enable_streaming) {
                if ((i == rt_track_offset) || (i == g_track_count[0] + rt_track_offset)) {
                    g_track_id_rt[cam_num] = i;
                    LOG_I(log_tag[cam_num], "RT track id for cam %d is %d", cam_num, i);
                }
                if (g_live_streaming == true) {
                    if ((i == ls_track_offset) || (i == g_track_count[0] + ls_track_offset)) {
                        g_track_id_live[cam_num] = i; // Lets save live stream track id for future use.
                        LOG_I(log_tag[cam_num], "LS track id for cam %d is %d", cam_num, i);
                    }
                }
            } else if (g_live_streaming == true) {
                if ((i == rt_track_offset) || (i == g_track_count[0] + rt_track_offset)) {
                    g_track_id_live[cam_num] = i; // Lets save live stream track id for future use.
                    LOG_I(log_tag[cam_num], "LS track id for cam %d is %d", cam_num, i);
                }
            }
            if (context->ld_enabled) {
                if ((i == g_track_count[0]) || (i == (g_track_count[0] + g_track_count[1]))) {
                    g_track_id_ld[cam_num] = i;
                    LOG_I(log_tag[cam_num], "LD track id for cam %d is %d", cam_num, i);
                }
            }

            track_info.session_id = session_id[cam_num];
            ret = video_track->SetUp(track_info);
            if (ret == 0) {
                tracks.push_back(video_track);
                LOG_I(log_tag[cam_num], "%s, Create track id %d, camera_id %d\n",
                        __func__, track_info.track_id, track_info.camera_id);
            } else {
                LOG_E(log_tag[cam_num], "%s, video track setup failed", __func__);
            }
            if (ret != 0) {
                string str_msg = "Track Creation failed for track_id " + to_string(track_info.track_id);
                nd_service_obj->send_err_msg(SM_E_NDC_TRACK_CREATION_FAIL, track_info.track_id, str_msg);
                return 0;
            }
        }
    }
    LOG_I(log_tag[cam_num], "%s: %d", __func__, __LINE__);
    for (uint32_t i=0;i < tracks.size();i++) {
        tracks[i]->Prepare();
    }
    int32_t RETRY_COUNT_StartSession = 5;
    while (RETRY_COUNT_StartSession--) {
        ret = recorder_test.recorder_.StartSession(session_id[cam_num]);
        LOG_I(log_tag[cam_num], "%s: %d", __func__, __LINE__);

        if (ret != NO_ERROR) {
            LOG_E(log_tag[cam_num], "%s: StartSession failed", __func__);
            sleep(RETRY_DELAY);
        } else {
            LOG_I(log_tag[cam_num], "%s: StartSession done", __func__);
            break;
        }
    }
    if (ret != 0) {
        string str_msg = "Start Session failed for camera_id " + to_string(cam_num);
        nd_service_obj->send_err_msg(SM_E_NDC_START_SESSION_FAIL, cam_num, str_msg);
        restart_qmmf_server();
        return 0;
    }
    LOG_I(log_tag[cam_num], "%s end", __func__);
    return 1;
}

int start_record_session_platform( void *ptr)
{
    if (cam_check_thread_created == false) {
        if (!pthread_create (&cam_check, NULL, cam_check_thread, NULL))
            cam_check_thread_created = true;
        else
            LOG_E ("QMMF", "cam_check_thread creation failed");
    }

    return true;
}

/* @configure_bit_rate function will take its value from the
 * bagherra_config.ini file. This function will be called
 * once during the initialization.
 */
void configure_bit_rate()
{
    LOG_I(LOG_TAG, "Reading Config file for bit-rate key....");
    string bitrate;
    Config_parser c(BAGHEERACONFIG_INI);
    is_bit_rate_configured = true;
    if (c.getParseStatus() != true) {
        LOG_I(LOG_TAG, "Config Parser failed...");
        return;
    }

    bool get_override_val = true, is_val_overridden = false;

    // bitrate for hevc codec format, front camera and 1920*1080 resolution
    bitrate = c.getConfig("camera","outward_nrt_bitrate", std::to_string(outward_nrt_bitrate) , get_override_val, is_val_overridden);
    if (false == string_to_integer(bitrate, outward_nrt_bitrate))
        LOG_E(LOG_TAG, "String to integer api failed for outward_nrt_bitrate");

    // bitrate for hevc codec format, front camera and 864*480 resolution
    bitrate = c.getConfig("camera","outward_nrt_ld_bitrate", std::to_string(outward_nrt_ld_bitrate) , get_override_val, is_val_overridden);
    if (false == string_to_integer(bitrate, outward_nrt_ld_bitrate))
        LOG_E(LOG_TAG, "String to integer api failed for outward_nrt_ld_bitrate");

    // bitrate for hevc codec format, back camera and 1920*1080 resolution
    bitrate = c.getConfig("camera","inward_nrt_bitrate", std::to_string(inward_nrt_bitrate) , get_override_val, is_val_overridden);
    if (false == string_to_integer(bitrate, inward_nrt_bitrate))
        LOG_E(LOG_TAG, "String to integer api failed for inward_nrt_bitrate");

    // bitrate for hevc codec format, back camera and 864*480 resolution
    bitrate = c.getConfig("camera","inward_nrt_ld_bitrate", std::to_string(inward_nrt_ld_bitrate) , get_override_val, is_val_overridden);
    if (false == string_to_integer(bitrate, inward_nrt_ld_bitrate))
        LOG_E(LOG_TAG, "String to integer api failed for inward_nrt_ld_bitrate");

    // check the LS feature
    string ls_enable_str = c.getConfig("live_streaming","enabled", "false", get_override_val, is_val_overridden);
    if (ls_enable_str == "false") {
        g_live_streaming = false;
    } else {
        g_live_streaming = true;
    }

    LOG_I(LOG_TAG, "Live streaming featue = %d", g_live_streaming);

    // Printing the bitrate values for all different types of codec, camera typpe and resolution
    LOG_I(LOG_TAG, "value for outward_nrt_bitrate is %d", outward_nrt_bitrate);
    LOG_I(LOG_TAG, "value for inward_nrt_bitrate is %d", inward_nrt_bitrate);
    LOG_I(LOG_TAG, "value for outward_nrt_ld_bitrate is %d", outward_nrt_ld_bitrate);
    LOG_I(LOG_TAG, "value for inward_nrt_ld_bitrate is %d", inward_nrt_ld_bitrate);

    return;
}

void init_default_track_info(int cam_num, TrackInfo_t *track_info)
{
    LOG_I(log_tag[cam_num], "%s: Camera ID %d\n", __func__, cam_num);
    track_info->camera_id = cam_num;

    //initParams->recordTime = 10;
    track_info->width = 1920;
    track_info->height = 1080;

    if (cam_num == CAMERA_POSITION_FRONT) {
        track_info->fps = 30;
    } else { // CAMERA_POSITION_BACK
        track_info->fps = 15;
    }
    track_info->low_power_mode = false;

    if (track_info->track_type == TrackType::kVideoAVC) {
        if (cam_num == CAMERA_POSITION_FRONT) {
            track_info->avcparams.bitrate = outward_nrt_bitrate;
        } else { // CAMERA_POSITION_BACK
            track_info->avcparams.bitrate = inward_nrt_bitrate;
        }
        track_info->avcparams.profile = AVCProfileType::kHigh;
        track_info->avcparams.level = AVCLevelType::kLevel3;
        track_info->avcparams.ratecontrol_type = VideoRateControlType::kVariable;
        track_info->avcparams.ltr_count = track_info->ltr_count;
        track_info->avcparams.hier_layer = 0;
        track_info->avcparams.qp_params.enable_init_qp = 1;
        track_info->avcparams.qp_params.init_qp.init_IQP = 27;
        track_info->avcparams.qp_params.init_qp.init_PQP = 28;
        track_info->avcparams.qp_params.init_qp.init_BQP = 28;
        track_info->avcparams.qp_params.init_qp.init_QP_mode = 0x7;
        track_info->avcparams.qp_params.enable_qp_range = 1;
        track_info->avcparams.qp_params.qp_range.min_QP = 10;
        track_info->avcparams.qp_params.qp_range.max_QP = 51;
        track_info->avcparams.qp_params.enable_qp_IBP_range = 1;
        track_info->avcparams.qp_params.qp_IBP_range.min_IQP = 10;
        track_info->avcparams.qp_params.qp_IBP_range.max_IQP = 51;
        track_info->avcparams.qp_params.qp_IBP_range.min_PQP = 10;
        track_info->avcparams.qp_params.qp_IBP_range.max_PQP = 51;
        track_info->avcparams.qp_params.qp_IBP_range.min_BQP = 10;
        track_info->avcparams.qp_params.qp_IBP_range.max_BQP = 51;
        track_info->avcparams.insert_aud_delimiter = 1;
        track_info->avcparams.prepend_sps_pps_to_idr = 1;
        track_info->avcparams.slice_enabled = 0;
        track_info->avcparams.slice_header_spacing = 8192;
    } else if (track_info->track_type == TrackType::kVideoHEVC) {
        if (cam_num == CAMERA_POSITION_FRONT) {
            track_info->hevcparams.bitrate = outward_nrt_bitrate;
        } else { // CAMERA_POSITION_BACK
            track_info->hevcparams.bitrate = inward_nrt_bitrate;
        }
        track_info->hevcparams.profile = HEVCProfileType::kMain10;
        track_info->hevcparams.level = HEVCLevelType::kLevel3;
        track_info->hevcparams.ratecontrol_type = VideoRateControlType::kVariable;
        track_info->hevcparams.ltr_count = track_info->ltr_count;
        track_info->hevcparams.hier_layer = 0;
        track_info->hevcparams.qp_params.enable_init_qp = 1;
        track_info->hevcparams.qp_params.init_qp.init_IQP = 27;
        track_info->hevcparams.qp_params.init_qp.init_PQP = 28;
        track_info->hevcparams.qp_params.init_qp.init_BQP = 28;
        track_info->hevcparams.qp_params.init_qp.init_QP_mode = 0x7;
        track_info->hevcparams.qp_params.enable_qp_range = 1;
        track_info->hevcparams.qp_params.qp_range.min_QP = 10;
        track_info->hevcparams.qp_params.qp_range.max_QP = 51;
        track_info->hevcparams.qp_params.enable_qp_IBP_range = 1;
        track_info->hevcparams.qp_params.qp_IBP_range.min_IQP = 10;
        track_info->hevcparams.qp_params.qp_IBP_range.max_IQP = 51;
        track_info->hevcparams.qp_params.qp_IBP_range.min_PQP = 10;
        track_info->hevcparams.qp_params.qp_IBP_range.max_PQP = 51;
        track_info->hevcparams.qp_params.qp_IBP_range.min_BQP = 10;
        track_info->hevcparams.qp_params.qp_IBP_range.max_BQP = 51;
        track_info->hevcparams.insert_aud_delimiter = 1;
        track_info->hevcparams.prepend_sps_pps_to_idr = 1;
    }
}

int32_t init_params(int cam_num, realtime_camera_config_t* rt_config)
{
    // Condition check to avoid calling configure_bit_rate multiple times
    if(!is_bit_rate_configured)
        configure_bit_rate();

    // HD video encoding 1920 x 1080
    TrackInfo_t track_info{};
    track_info = {};
    track_info.track_type = TrackType::kVideoHEVC;
    init_default_track_info(cam_num, &track_info);
    infos.push_back(track_info);
    g_track_count[cam_num]++;

    string bagheera_config_path = ND_DEVICE_REL_PATH+ "/latest/bagheera_config.ini";
    Config_parser bagheera_config_parser(bagheera_config_path);
    bool get_override_val = true;
    bool is_val_overridden = false;
    if (bagheera_config_parser.getParseStatus()) {
        string temp = bagheera_config_parser.getConfig("camera", "video_encryption", "true", get_override_val, is_val_overridden);
        if (temp == "false") {
            stream_encryption = false;
        }
        else {
            stream_encryption = true;
        }
    }
    LOG_I(LOG_TAG, "Stream Encryption is %d", stream_encryption);

    bool rt_enabled = rt_config->enable_streaming;
    rt_config_enabled[cam_num] = rt_enabled;
    if (rt_enabled) {
        track_info = {};
        track_info.camera_id = cam_num;
        track_info.width = rt_config->width;
        track_info.height = rt_config->height;

        if (cam_num == CAMERA_POSITION_FRONT)
            track_info.fps = 30;
        else
            track_info.fps = 15;

        track_info.track_type = TrackType::kVideoYUV;
        track_info.low_power_mode = true;

        infos.push_back(track_info);
        g_track_count[cam_num]++;
    }

    if (g_live_streaming == true) {
        track_info = {};
        track_info.track_type = TrackType::kVideoAVC;
        init_default_track_info(cam_num, &track_info);
        track_info.width = 640;
        track_info.height = 480;
        track_info.fps = AWS_KINESIS_FPS;
        track_info.low_power_mode = false;

        /* parse and set configurations */
        string bagheera_config_path = ND_DEVICE_REL_PATH+ "/latest/bagheera_config.ini";
        string fps_str("10"), width_str("640"), length_str("480"), bitrate_str("500000"), enc_type_str("2");
        string default_fps_str("10"), default_width_str("640"), default_length_str("480"), default_bitrate_str("500000"), default_enc_type_str("2");
        int birate = 500000, track_type = 2, fps = 10, width = 640, height = 480;
        bool get_override_val = true;
        bool is_val_overridden = false;

        Config_parser bagheera_config_parser(bagheera_config_path);
        if (bagheera_config_parser.getParseStatus()) {
            if ((enc_type_str = bagheera_config_parser.getConfig("live_streaming","enc_type",enc_type_str, get_override_val, is_val_overridden)) == "") {
                enc_type_str = default_enc_type_str;    // set default enc type. i.e AVC
            }

            if ((fps_str = bagheera_config_parser.getConfig("live_streaming","fps", fps_str, get_override_val, is_val_overridden)) == "") {
                fps_str = default_fps_str;    //set default fps. i.e 10
            }

            if ((width_str = bagheera_config_parser.getConfig("live_streaming","width", width_str, get_override_val, is_val_overridden)) == "") {
                width_str = default_width_str;    //set default width. i.e 640
            }

            if ((length_str = bagheera_config_parser.getConfig("live_streaming","height", length_str, get_override_val, is_val_overridden)) == "") {
                length_str = default_length_str;    //set default length. i.e 480
            }

            if ((bitrate_str = bagheera_config_parser.getConfig("live_streaming","bitrate", bitrate_str, get_override_val, is_val_overridden)) == "") {
                bitrate_str = default_bitrate_str;    //set default bitrate. i.e 500kb
            }

            string_to_integer(enc_type_str, track_type);
            string_to_integer(fps_str, fps);
            string_to_integer(width_str, width);
            string_to_integer(length_str, height);
            string_to_integer(bitrate_str, birate);

            track_info.track_type = (track_type == 0) ? (TrackType) 2 : (TrackType) track_type;
            track_info.fps = (fps == 0) ? 10 : fps;
            kinesis_req_fps = track_info.fps;
            track_info.width = (width == 0) ? 640 : width;
            track_info.height = (height == 0) ? 480 : height;
            birate = (birate == 0) ? 500000 : birate;
            if (cam_num == CAMERA_POSITION_FRONT) {
                live_bitrate_front = birate;
            } else {
                live_bitrate_back = birate;
            }
        }

        if (track_info.track_type == TrackType::kVideoAVC) {
            if (cam_num == CAMERA_POSITION_FRONT) {
                track_info.avcparams.bitrate = birate;
            } else { // CAMERA_POSITION_BACK
                track_info.avcparams.bitrate = birate;
            }
            track_info.avcparams.profile = AVCProfileType::kHigh;
            track_info.avcparams.level = AVCLevelType::kLevel3;
            track_info.avcparams.ratecontrol_type = VideoRateControlType::kVariable;
        } else if (track_info.track_type == TrackType::kVideoHEVC) {
            if (cam_num == CAMERA_POSITION_FRONT) {
                track_info.hevcparams.bitrate = birate;
            } else { // CAMERA_POSITION_BACK
                track_info.hevcparams.bitrate = birate;
            }
            track_info.hevcparams.profile = HEVCProfileType::kMain;
            track_info.hevcparams.level = HEVCLevelType::kLevel3;
            track_info.hevcparams.ratecontrol_type = VideoRateControlType::kVariable;
        }
        LOG_I(LOG_TAG, "track for kvs, track_type = %d, fps = %d, width = %d, height = %d, bitrate = %d", (int)track_info.track_type, (int)track_info.fps, \
                                                                         (int)track_info.width, (int)track_info.height, (int)track_info.avcparams.bitrate);
        infos.push_back(track_info);
        g_track_count[cam_num]++;
    }

    /* Check support for event access preview feature */
    if (cam_num == CAMERA_POSITION_FRONT) {
        int ea_enabled = 0;
        string temp = bagheera_config_parser.getConfig("ea_config", "enabled", "0", get_override_val, is_val_overridden);
        string_to_integer(temp.c_str(), ea_enabled);
        if (ea_enabled == 0) {
            LOG_I(LOG_TAG, "Event Access Preview Feature is disabled");
        } else if (ea_enabled == 1) {
            LOG_I(LOG_TAG, "Event Access Preview Feature is enabled");

            temp = bagheera_config_parser.getConfig("ea_config", "outward", "0", get_override_val, is_val_overridden);
            string_to_integer(temp.c_str(), ea_outward_enabled);

            if (ea_outward_enabled == 0) {
                LOG_I(LOG_TAG, "Event Access Image capture disabled for outward camera");
            } else if (ea_outward_enabled == 1) {
                LOG_I(LOG_TAG, "Event Access Image capture enabled for outward camera, read image specific params from bagheera config");

                temp = bagheera_config_parser.getConfig("ea_config", "width", "206", get_override_val, is_val_overridden);
                string_to_integer(temp.c_str(), ea_image_params.width);
                if (ea_image_params.width != 206) {
                    LOG_I(LOG_TAG, "Invalid value %d provided for width field under ea_config section, forcing image width to default value (206)", ea_image_params.width);
                    ea_image_params.width = 206;
                }

                temp = bagheera_config_parser.getConfig("ea_config", "height", "112", get_override_val, is_val_overridden);
                string_to_integer(temp.c_str(), ea_image_params.height);
                if (ea_image_params.height != 112) {
                    LOG_I(LOG_TAG, "Invalid value %d provided for height field under ea_config section, forcing image height to default value (112)", ea_image_params.height);
                    ea_image_params.height = 112;
                }

                temp = bagheera_config_parser.getConfig("ea_config", "quality", "70", get_override_val, is_val_overridden);
                string_to_integer(temp.c_str(), ea_image_params.quality);
                if ((ea_image_params.quality < 0) || (ea_image_params.quality > 100)) {
                    LOG_I(LOG_TAG, "Invalid value %d provided for quality field under ea_config section, forcing image quality to default value (70)", ea_image_params.quality);
                    ea_image_params.quality = 70;
                }

                LOG_I(LOG_TAG, "ea_image_params: width %d, height %d, quality %d", ea_image_params.width, ea_image_params.height, ea_image_params.quality);

                temp = bagheera_config_parser.getConfig("ea_config", "num_images_per_hour", "12", get_override_val, is_val_overridden);
                string_to_integer(temp.c_str(), ea_images_per_hr);
                if ((ea_images_per_hr != 12) && (ea_images_per_hr != 20) && (ea_images_per_hr != 30) && (ea_images_per_hr != 60)) {
                    LOG_I(LOG_TAG, "Invalid value %d provided for num_images_per_hour field under ea_config section, forcing ea_images_per_hr to default value (12)", ea_images_per_hr);
                    ea_images_per_hr = 12;
                }
                ea_freq_session_wise = (int)(60 / ea_images_per_hr);

                LOG_I(LOG_TAG, "ea_config: ea_images_per_hr %d", ea_images_per_hr);

                pthread_mutex_init(&ea_mutex, NULL);
                pthread_cond_init(&ea_cond, NULL);
            } else {
                LOG_I(LOG_TAG, "Invalid value %d provided for outward field under ea_config section, forcing ea_outward_enabled to default value (0) thereby disabling Event Access Image capture for outward camera", ea_outward_enabled);
                ea_outward_enabled = 0;
            }
        } else {
            LOG_I(LOG_TAG, "Invalid value %d provided for enabled field under ea_config section, forcing ea_enabled to default value (0) thereby disabling Event Access Preview Feature", ea_enabled);
            ea_enabled = 0;
        }
    }

    string ld_enabled_str = "false";
    cam_record_ctxt *context = (cam_record_ctxt *)(&(recorder_test.cam_ctxt[cam_num]));
    if (cam_num == CAMERA_POSITION_FRONT)
        ld_enabled_str = bagheera_config_parser.getConfig("camera", "outward_ld_enabled", "true", get_override_val, is_val_overridden);
    else if (cam_num == CAMERA_POSITION_BACK)
        ld_enabled_str = bagheera_config_parser.getConfig("camera", "inward_ld_enabled", "true", get_override_val, is_val_overridden);

    if (ld_enabled_str == "false") {
        context->ld_enabled = false;
    } else {
        context->ld_enabled = true;
    }
    LOG_I(LOG_TAG, "LD feature = %d", context->ld_enabled);

    if (context->ld_enabled) {
        // LD video encoding 864 x 480
        track_info = {};
        track_info.track_type = TrackType::kVideoHEVC;
        init_default_track_info(cam_num, &track_info);

        int outward_nrt_ld_width = 864, outward_nrt_ld_height = 480;
        int inward_nrt_ld_width = 864, inward_nrt_ld_height = 480;
        string outward_nrt_ld_width_str = "864", outward_nrt_ld_height_str = "480";
        string inward_nrt_ld_width_str = "864", inward_nrt_ld_height_str = "480";

        outward_nrt_ld_width_str = bagheera_config_parser.getConfig("camera", "outward_nrt_ld_width", outward_nrt_ld_width_str, get_override_val, is_val_overridden);
        inward_nrt_ld_width_str = bagheera_config_parser.getConfig("camera", "inward_nrt_ld_width", inward_nrt_ld_width_str, get_override_val, is_val_overridden);
        outward_nrt_ld_height_str = bagheera_config_parser.getConfig("camera", "outward_nrt_ld_height", outward_nrt_ld_height_str, get_override_val, is_val_overridden);
        inward_nrt_ld_height_str = bagheera_config_parser.getConfig("camera", "inward_nrt_ld_height", inward_nrt_ld_height_str, get_override_val, is_val_overridden);

        string_to_integer(outward_nrt_ld_width_str, outward_nrt_ld_width);
        string_to_integer(inward_nrt_ld_width_str, inward_nrt_ld_width);
        string_to_integer(outward_nrt_ld_height_str, outward_nrt_ld_height);
        string_to_integer(inward_nrt_ld_height_str, inward_nrt_ld_height);

        if(cam_num == CAMERA_POSITION_FRONT) {
            track_info.width = outward_nrt_ld_width;
            track_info.height = outward_nrt_ld_height;
        } else { // CAMERA_POSITION_BACK
            track_info.width = inward_nrt_ld_width;
            track_info.height = inward_nrt_ld_height;
        }

        if (track_info.track_type == TrackType::kVideoAVC) {
            if (cam_num == CAMERA_POSITION_FRONT) {
                track_info.avcparams.bitrate = outward_nrt_ld_bitrate;
            } else { // CAMERA_POSITION_BACK
                track_info.avcparams.bitrate = inward_nrt_ld_bitrate;
            }
            track_info.avcparams.profile = AVCProfileType::kHigh;
            track_info.avcparams.level = AVCLevelType::kLevel3;
            track_info.avcparams.ratecontrol_type = VideoRateControlType::kVariable;
        } else if (track_info.track_type == TrackType::kVideoHEVC) {
            if (cam_num == CAMERA_POSITION_FRONT) {
                track_info.hevcparams.bitrate = outward_nrt_ld_bitrate;
            } else { // CAMERA_POSITION_BACK
                track_info.hevcparams.bitrate = inward_nrt_ld_bitrate;
            }
            track_info.hevcparams.profile = HEVCProfileType::kMain;
            track_info.hevcparams.level = HEVCLevelType::kLevel3;
            track_info.hevcparams.ratecontrol_type = VideoRateControlType::kVariable;
        }

        infos.push_back(track_info);
        g_track_count[cam_num]++;
    }

    if (cam_num == CAMERA_POSITION_BACK) {
        if ("true" == bagheera_config_parser.getConfig("driverlogin_v2", "qr_enabled", "false", true, is_val_overridden)) {
            is_driverlogin_qr_enabled = true;
        }

        if (is_driverlogin_qr_enabled == true) {
            LOG_I(LOG_TAG, "Driver login through QR code scanning is enabled");

            string temp = bagheera_config_parser.getConfig("driverlogin_v2", "max_qr_scan_duration", "3600", get_override_val, is_val_overridden);
            string_to_integer(temp.c_str(), max_scan_duration); // maximum scan duration
            
            if ("true" == bagheera_config_parser.getConfig("driverlogin_v2", "enable_invalid_qr_image_dump", "false", get_override_val, is_val_overridden)) {
                LOG_I(LOG_TAG, "Invalid QR image dump is enabled");
                invalid_qr_image_dump_enabled = true;
            } else {
                LOG_I(LOG_TAG, "Invalid QR image dump is disabled");
                invalid_qr_image_dump_enabled = false;
            }

            pthread_mutex_init(&qr_scan_mutex, NULL);
            pthread_cond_init(&qr_scan_cond, NULL);

            // add QR scan track
            track_info = {};
            track_info.camera_id = cam_num;
            track_info.width = 1920;
            track_info.height = 1080;
            track_info.fps = 1;
            track_info.track_type = TrackType::kVideoYUV;
            track_info.low_power_mode = true;

            infos.push_back(track_info);
            //g_track_count[cam_num]++;            
        } else {
            LOG_I(LOG_TAG, "Driver login through QR code scanning is disabled");
        }
    }

    return 0;
}

TestTrack::TestTrack(RecorderTest* recorder_test)
    : recorder_test_(recorder_test)
{
    LOG_I(log_tag[0], "%s: Enter", __func__);
    track_info_ = {};
    LOG_I(log_tag[0], "%s: Exit", __func__);
}

TestTrack::~TestTrack()
{
    LOG_I(log_tag[track_info_.camera_id], "%s: Enter", __func__);
    LOG_I(log_tag[track_info_.camera_id], "%s: Exit!", __func__);
}

VideoTrackCreateParam video_track_param[9]{}; // assuming max 9 tracks (8 + 1 for QR scan)
qmmf_status_t TestTrack::SetUp(TrackInfo_t& track_info)
{
    static int i = 0;
    LOG_I(log_tag[track_info_.camera_id], "%s: Enter cam_num = %d, lpm = %d", __func__, track_info.camera_id, track_info.low_power_mode);
    int32_t ret = NO_ERROR;

    if ((track_info.track_type == TrackType::kVideoAVC) ||
        (track_info.track_type == TrackType::kVideoYUV) ||
        (track_info.track_type == TrackType::kVideoHEVC)) {
        float fps = track_info.fps;
        // Create Video Track.
        //VideoTrackCreateParam video_track_param{};
        video_track_param[i].camera_id = track_info.camera_id;
        video_track_param[i].width = track_info.width;
        video_track_param[i].height = track_info.height;

        if (fps != 0)
            video_track_param[i].frame_rate  = fps;
        else
            video_track_param[i].frame_rate  = 30;

        video_track_param[i].low_power_mode  = track_info.low_power_mode;

        switch (track_info.track_type) {
        case TrackType::kVideoAVC:
            video_track_param[i].format_type = VideoFormat::kAVC;
            video_track_param[i].codec_param.avc.idr_interval =
                track_info.avcparams.idr_interval;
            video_track_param[i].codec_param.avc.bitrate =
                track_info.avcparams.bitrate;
            video_track_param[i].codec_param.avc.profile =
                track_info.avcparams.profile;
            video_track_param[i].codec_param.avc.level = track_info.avcparams.level;
            video_track_param[i].codec_param.avc.ratecontrol_type =
                track_info.avcparams.ratecontrol_type;
            video_track_param[i].codec_param.avc.qp_params.enable_init_qp =
                track_info.avcparams.qp_params.enable_init_qp;
            video_track_param[i].codec_param.avc.qp_params.init_qp.init_IQP =
                track_info.avcparams.qp_params.init_qp.init_IQP;
            video_track_param[i].codec_param.avc.qp_params.init_qp.init_PQP =
                track_info.avcparams.qp_params.init_qp.init_PQP;
            video_track_param[i].codec_param.avc.qp_params.init_qp.init_BQP =
                track_info.avcparams.qp_params.init_qp.init_BQP;
            video_track_param[i].codec_param.avc.qp_params.init_qp.init_QP_mode =
                track_info.avcparams.qp_params.init_qp.init_QP_mode;
            video_track_param[i].codec_param.avc.qp_params.enable_qp_range =
                track_info.avcparams.qp_params.enable_qp_range;
            video_track_param[i].codec_param.avc.qp_params.qp_range.min_QP =
                track_info.avcparams.qp_params.qp_range.min_QP;
            video_track_param[i].codec_param.avc.qp_params.qp_range.max_QP =
                track_info.avcparams.qp_params.qp_range.max_QP;
            video_track_param[i].codec_param.avc.qp_params.enable_qp_IBP_range =
                track_info.avcparams.qp_params.enable_qp_IBP_range;
            video_track_param[i].codec_param.avc.qp_params.qp_IBP_range.min_IQP =
                track_info.avcparams.qp_params.qp_IBP_range.min_IQP;
            video_track_param[i].codec_param.avc.qp_params.qp_IBP_range.max_IQP =
                track_info.avcparams.qp_params.qp_IBP_range.max_IQP;
            video_track_param[i].codec_param.avc.qp_params.qp_IBP_range.min_PQP =
                track_info.avcparams.qp_params.qp_IBP_range.min_PQP;
            video_track_param[i].codec_param.avc.qp_params.qp_IBP_range.max_PQP =
                track_info.avcparams.qp_params.qp_IBP_range.max_PQP;
            video_track_param[i].codec_param.avc.qp_params.qp_IBP_range.min_BQP =
                track_info.avcparams.qp_params.qp_IBP_range.min_BQP;
            video_track_param[i].codec_param.avc.qp_params.qp_IBP_range.max_BQP =
                track_info.avcparams.qp_params.qp_IBP_range.max_BQP;
            video_track_param[i].codec_param.avc.ltr_count =
                track_info.avcparams.ltr_count;
            video_track_param[i].codec_param.avc.hier_layer =
                track_info.avcparams.hier_layer;
            video_track_param[i].codec_param.avc.insert_aud_delimiter =
                track_info.avcparams.insert_aud_delimiter;
            video_track_param[i].codec_param.avc.prepend_sps_pps_to_idr =
                track_info.avcparams.prepend_sps_pps_to_idr;
            video_track_param[i].codec_param.avc.slice_enabled =
                track_info.avcparams.slice_enabled;
            video_track_param[i].codec_param.avc.slice_header_spacing =
                track_info.avcparams.slice_header_spacing;
            break;
        case TrackType::kVideoHEVC:
            video_track_param[i].format_type = VideoFormat::kHEVC;
            video_track_param[i].codec_param.hevc.idr_interval =
                track_info.hevcparams.idr_interval;
            video_track_param[i].codec_param.hevc.bitrate =
                track_info.hevcparams.bitrate;
            video_track_param[i].codec_param.hevc.profile =
                track_info.hevcparams.profile;
            video_track_param[i].codec_param.hevc.level = track_info.hevcparams.level;
            video_track_param[i].codec_param.hevc.ratecontrol_type =
                track_info.hevcparams.ratecontrol_type;
            video_track_param[i].codec_param.hevc.qp_params.enable_init_qp =
                track_info.hevcparams.qp_params.enable_init_qp;
            video_track_param[i].codec_param.hevc.qp_params.init_qp.init_IQP =
                track_info.hevcparams.qp_params.init_qp.init_IQP;
            video_track_param[i].codec_param.hevc.qp_params.init_qp.init_PQP =
                track_info.hevcparams.qp_params.init_qp.init_PQP;
            video_track_param[i].codec_param.hevc.qp_params.init_qp.init_BQP =
                track_info.hevcparams.qp_params.init_qp.init_BQP;
            video_track_param[i].codec_param.hevc.qp_params.init_qp.init_QP_mode =
                track_info.hevcparams.qp_params.init_qp.init_QP_mode;
            video_track_param[i].codec_param.hevc.qp_params.enable_qp_range =
                track_info.hevcparams.qp_params.enable_qp_range;
            video_track_param[i].codec_param.hevc.qp_params.qp_range.min_QP =
                track_info.hevcparams.qp_params.qp_range.min_QP;
            video_track_param[i].codec_param.hevc.qp_params.qp_range.max_QP =
                track_info.hevcparams.qp_params.qp_range.max_QP;
            video_track_param[i].codec_param.hevc.qp_params.enable_qp_IBP_range =
                track_info.hevcparams.qp_params.enable_qp_IBP_range;
            video_track_param[i].codec_param.hevc.qp_params.qp_IBP_range.min_IQP =
                track_info.hevcparams.qp_params.qp_IBP_range.min_IQP;
            video_track_param[i].codec_param.hevc.qp_params.qp_IBP_range.max_IQP =
                track_info.hevcparams.qp_params.qp_IBP_range.max_IQP;
            video_track_param[i].codec_param.hevc.qp_params.qp_IBP_range.min_PQP =
                track_info.hevcparams.qp_params.qp_IBP_range.min_PQP;
            video_track_param[i].codec_param.hevc.qp_params.qp_IBP_range.max_PQP =
                track_info.hevcparams.qp_params.qp_IBP_range.max_PQP;
            video_track_param[i].codec_param.hevc.qp_params.qp_IBP_range.min_BQP =
                track_info.hevcparams.qp_params.qp_IBP_range.min_BQP;
            video_track_param[i].codec_param.hevc.qp_params.qp_IBP_range.max_BQP =
                track_info.hevcparams.qp_params.qp_IBP_range.max_BQP;
            video_track_param[i].codec_param.hevc.ltr_count =
                track_info.hevcparams.ltr_count;
            video_track_param[i].codec_param.hevc.hier_layer =
                track_info.hevcparams.hier_layer;
            video_track_param[i].codec_param.hevc.insert_aud_delimiter =
                track_info.hevcparams.insert_aud_delimiter;
            video_track_param[i].codec_param.hevc.prepend_sps_pps_to_idr =
                track_info.hevcparams.prepend_sps_pps_to_idr;
            break;
        case TrackType::kVideoYUV:
            video_track_param[i].format_type = VideoFormat::kYUV;
            break;
        default:
            break;
        }

        TrackCb video_track_cb;
        video_track_cb.data_cb = [&] (uint32_t track_id,
            std::vector<BufferDescriptor> buffers, std::vector<MetaData>
            meta_buffers) { TrackDataCB(track_id, buffers, meta_buffers); };

        video_track_cb.event_cb = [&] (uint32_t track_id, EventType event_type,
            void *event_data, size_t data_size) { TrackEventCB(track_id,
            event_type, event_data, data_size); };

        if (recorder_test_ != NULL) {
            if (track_info.track_id == g_track_id_qr) {
                VideoExtraParam extra_param;
                SourceVideoTrack surface_video_copy;
                surface_video_copy.source_track_id = g_track_id_hd[track_info.camera_id];
                extra_param.Update(QMMF_SOURCE_VIDEO_TRACK_ID, surface_video_copy);

                int32_t RETRY_COUNT_CreateVideoTrack = 5;
                while (RETRY_COUNT_CreateVideoTrack--) {
                    ret = recorder_test_->GetRecorder().CreateVideoTrack(track_info.session_id,
                            track_info.track_id, video_track_param[i], extra_param, video_track_cb);
                    if (ret != 0) {
                        LOG_E(log_tag[track_info_.camera_id], "%s: CreateVideoTrack failed", __func__);
                        sleep(RETRY_DELAY);
                    } else {
                        LOG_I(log_tag[track_info_.camera_id], "%s: CreateVideoTrack done", __func__);
                        break;
                    }
                }                
            } else {
                // Enable Force Sensor Mode if needed
                VideoExtraParam extra_param_force_mode;
                ForceSensorMode force_sensor_mode;

                if (track_info.camera_id == 1) {
                    force_sensor_mode.mode = 1;
                    LOG_I(log_tag[track_info.camera_id], "Force sensor mode enabled !!!");
                } else {
                    force_sensor_mode.mode = -1; // -1 for disable
                }
                extra_param_force_mode.Update(QMMF_FORCE_SENSOR_MODE, force_sensor_mode);

                int32_t RETRY_COUNT_CreateVideoTrack = 5;
                while (RETRY_COUNT_CreateVideoTrack--) {
                    ret = recorder_test_->GetRecorder().CreateVideoTrack(track_info.session_id,
                        track_info.track_id, video_track_param[i], extra_param_force_mode, video_track_cb);
                    if (ret != 0) {
                        LOG_E(log_tag[track_info_.camera_id], "%s: CreateVideoTrack failed", __func__);
                        sleep(RETRY_DELAY);
                    } else {
                        LOG_I(log_tag[track_info_.camera_id], "%s: CreateVideoTrack done", __func__);
                        break;
                    }
                }
            }
        }
    }
    track_info_ = track_info;
    i++;
    LOG_I(log_tag[track_info_.camera_id], "%s: Exit", __func__);
    return ret;
}

// Set up file to dump track data.
qmmf_status_t TestTrack::Prepare(uint64_t timestamp)
{
    LOG_I(log_tag[track_info_.camera_id], "%s: Enter", __func__);
    int32_t ret = NO_ERROR;

    if ((track_info_.track_type == TrackType::kVideoAVC) || (track_info_.track_type == TrackType::kVideoHEVC)) {
        VideoFormat videoformat;
        if (track_info_.track_type == TrackType::kVideoAVC)
            videoformat = VideoFormat::kAVC;
        else if (track_info_.track_type == TrackType::kVideoHEVC)
            videoformat = VideoFormat::kHEVC;
        else if(track_info_.track_type == TrackType::kVideoYUV)
            videoformat = VideoFormat::kYUV;

        if (recorder_test_->is_dump_bitstream_enabled_) {
            StreamDumpInfo dumpinfo = {
                videoformat, // format
                track_info_.track_id, // track_id
                (int32_t)track_info_.width, // width
                (int32_t)track_info_.height, // height
                (int32_t)track_info_.camera_id // cam_num
            };

            ret = dump_bitstream_.SetUp(dumpinfo, track_info_, timestamp);
            if (ret != NO_ERROR) {
                LOG_E(log_tag[track_info_.camera_id], "%s: bitstream setup failed", __func__);
            } else {
                LOG_I(log_tag[track_info_.camera_id], "%s: bitstream setup done", __func__);
            }
            if (ret != NO_ERROR) {
                string str_msg = "Bitstream setup failed for track_id " + to_string(track_info_.track_id);
                nd_service_obj->send_err_msg(SM_E_NDC_BITSTREAM_SETUP_FAIL, track_info_.track_id, str_msg);
                return ret;
            }
        }
    }

    LOG_I(log_tag[track_info_.camera_id], "%s: Exit", __func__);
    return ret;
}

// Clean up file.
qmmf_status_t TestTrack::CleanUp()
{
    LOG_I(log_tag[track_info_.camera_id], "%s: Enter", __func__);
    int32_t ret = NO_ERROR;
    switch (track_info_.track_type) {
    case TrackType::kVideoAVC:
    case TrackType::kVideoYUV:
    case TrackType::kVideoHEVC:
        dump_bitstream_.Close();
        break;
    default:
        break;
    }
    LOG_I(log_tag[track_info_.camera_id], "%s: Exit", __func__);
    return ret;
}

void TestTrack::TrackEventCB(uint32_t track_id, EventType event_type,
                             void *event_data, size_t event_data_size)
{
    LOG_I(log_tag[track_info_.camera_id], "%s: Enter", __func__);
    LOG_I(log_tag[track_info_.camera_id], "%s: Exit", __func__);
}

void* enc_and_save_ea_image(void *args)
{
    cam_record_ctxt *context = (cam_record_ctxt *)args;

    char ea_image_fname[GSTREAMER_NAME_LENGTH_MAX];

    while (1) {
        pthread_mutex_lock(&ea_mutex);
        while (is_image_raw_data_present == false) {
            pthread_cond_wait(&ea_cond, &ea_mutex);
        }
        pthread_mutex_unlock(&ea_mutex);

        string curr_session_fname = context->cur_file_name;

        /* Remove .mp4 from video filename as ea_image filename does not contain .mp4 */
        int dot = curr_session_fname.find(".mp4");
        if (dot != string::npos)
            curr_session_fname.resize(dot);

        nd_strncpy(ea_image_fname, curr_session_fname.c_str(), sizeof(ea_image_fname));
        strcat(ea_image_fname, ea_extn);

        LOG_I(log_tag[context->cam_pos], "Event Access session filename: %s", ea_image_fname);

        // Load raw YUV data from a file
        std::ifstream yuvFile(image_raw_data_file, std::ios::binary);
        if (!yuvFile) {
            LOG_E(log_tag[context->cam_pos], "Failed to open image raw data file %s for image encoding", image_raw_data_file.c_str());
            file_delete(image_raw_data_file);
            continue;
        }
        // Read YUV data
        std::vector<unsigned char> yuvData(raw_buffer_width * raw_buffer_height * 3 / 2); // NV12 format
        yuvFile.read(reinterpret_cast<char*>(yuvData.data()), yuvData.size());
        yuvFile.close();

        // Convert YUV to BGR
        cv::Mat yuvImage(raw_buffer_height + raw_buffer_height / 2, raw_buffer_width, CV_8UC1, yuvData.data()); // YUV 4:2:0
        cv::Mat bgrImage;
        cv::cvtColor(yuvImage, bgrImage, cv::COLOR_YUV2BGR_NV12); // Convert YUV to BGR

        // Resize BGR image
        int jpeg_image_width = ea_image_params.width;
        int jpeg_image_height = ea_image_params.height;
        uint8_t *data_ptr = (uint8_t *)malloc(jpeg_image_width * jpeg_image_height * 3);
        cv::Mat resized_mat = cv::Mat(jpeg_image_height, jpeg_image_width, CV_8UC3, (uint8_t *)data_ptr);
        resize(bgrImage, resized_mat, resized_mat.size(), 0, 0, CV_INTER_LINEAR);

        // Encode to JPEG
        std::vector<unsigned char> jpegData;
        std::vector<int> compressionParams = {cv::IMWRITE_JPEG_QUALITY, ea_image_params.quality}; // Set JPEG quality
        if (!cv::imencode(".jpg", resized_mat, jpegData, compressionParams)) {
            LOG_E(log_tag[context->cam_pos], "Failed to encode image to JPEG");
            file_delete(image_raw_data_file);
            free(data_ptr);
            continue;
        }
        free(data_ptr);

        // Save the JPEG data to a file
        int jpeg_fd = open(ea_image_fname, O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (jpeg_fd < 0) {
            LOG_E(log_tag[context->cam_pos], "Failed to open EA image file %s", ea_image_fname);
            file_delete(image_raw_data_file);
            continue;
        }
        if (stream_encryption) {
            unsigned char* encrypted_buffer = NULL;
            size_t encrypted_buffer_len = 0;

            int encryption_status = nd_stream_encryption((unsigned char*)jpegData.data(), jpegData.size(), &encrypted_buffer, &encrypted_buffer_len);
            LOG_I(log_tag[context->cam_pos], "enc_and_save_ea_image: EA buffer encryption status: %d", encryption_status);

            if (encrypted_buffer) {
                write(jpeg_fd, encrypted_buffer, encrypted_buffer_len);
                free(encrypted_buffer);
            } else {
                LOG_E(log_tag[context->cam_pos], "enc_and_save_ea_image: EA buffer encryption has failed, writing in file without encryption");
                write(jpeg_fd, jpegData.data(), jpegData.size());
            }
        } else {
            write(jpeg_fd, jpegData.data(), jpegData.size());
        }

        if (fsync(jpeg_fd) < 0) {
            LOG_E(log_tag[context->cam_pos], "Failed to fsync EA image file %s", ea_image_fname);
        }

        close(jpeg_fd);
        file_delete(image_raw_data_file);
        is_image_raw_data_present = false;
    }
}

// Helper function for dumping invalid QR debug images
static void dump_invalid_qr_debug_image(uint8_t *y_plane, size_t buffer_size, uint64_t qr_frame_epoch)
{
    try {
        std::ofstream yuvFile(invalid_qr_image_raw_data_file, std::ios::binary);
        if (!yuvFile.is_open()) {
            LOG_E("QR_SCAN", "Failed to open debug image file: %s", invalid_qr_image_raw_data_file.c_str());
            return;
        }

        yuvFile.write(reinterpret_cast<char*>(y_plane), buffer_size);
        if (yuvFile.fail()) {
            LOG_E("QR_SCAN", "Failed to write debug image data");
            return;
        }
        yuvFile.close();

        // Compress the debug image
        stringstream compress_cmd;
        compress_cmd << "7za a -m0=LZMA2 -mx1 /home/ubuntu/.nddevice/log/archive/critical/"
                    << qr_frame_epoch << "_qrimage.7z " << invalid_qr_image_raw_data_file;

        execute_cmd(compress_cmd.str(), "QR_SCAN");
    } catch (const std::exception& e) {
        LOG_E("QR_SCAN", "Exception in debug image dump: %s", e.what());
    }
}

void* qr_code_detect_decode_thread(void *args)
{
    cam_record_ctxt *context = (cam_record_ctxt *)args;
    unordered_map<string, vector<string>> out;
    int buf_read_index = 0;
    uint64_t frame_time_epoch;

    // Validate input parameters
    if (!context || !scanner_ptr) {
        LOG_E("QR_SCAN", "Invalid context or scanner pointer, exiting thread");
        return NULL;
    }

    LOG_I("QR_SCAN", "QR decode thread started");

    while (!stop_qrdecode_thread) {
        pthread_mutex_lock(&qr_scan_mutex);
        // Wait for new Y-plane data
        while (is_yplane_buf_present == false && !stop_qrdecode_thread) {
            pthread_cond_wait(&qr_scan_cond, &qr_scan_mutex);
        }
        // Check if we should exit
        if (stop_qrdecode_thread) {
            pthread_mutex_unlock(&qr_scan_mutex);
            break;
        }
        is_yplane_buf_present = false; //Making false so that thread blocks on next iteration
        pthread_mutex_unlock(&qr_scan_mutex);

        // Validate buffer bounds
        if (!y_planes || buf_read_index >= max_queued_yplanes || buf_read_index < 0) {
            LOG_E("QR_SCAN", "Invalid buffer state: y_planes = %p, buf_read_index = %d, max_queued_yplanes = %d",
                  y_planes, buf_read_index, max_queued_yplanes);
            continue;
        }
        // Calculate buffer offset safely
        size_t buffer_size = qrscan_buffer_width * qrscan_buffer_height;
        size_t buffer_offset = buf_read_index * buffer_size;
        uint8_t *y_plane = static_cast<uint8_t*>(y_planes + buffer_offset);

        // Extract timestamp from the Y-plane buffer
        memcpy(&frame_time_epoch, y_plane, sizeof(uint64_t));
        LOG_D("QR_SCAN", "Processing buffer with timestamp %llu", frame_time_epoch);

        // Detect QR codes with error handling
        int num_qr_codes_detected = 0;
        try {
            num_qr_codes_detected = scanner_ptr->DetectQRs(y_plane, qrscan_buffer_width, qrscan_buffer_height);
        } catch (const std::exception& e) {
            LOG_E("QR_SCAN", "Exception in QR detection: %s", e.what());
            goto next_buffer;
        }
        if (num_qr_codes_detected > 0) {
            LOG_I("QR_SCAN", "Detected %d QR code(s) for frame with timestamp %llu, proceeding to decode", num_qr_codes_detected, frame_time_epoch);

            try {
                out = scanner_ptr->Decode(); // Decode the detected QR codes
            } catch (const std::exception& e) {
                LOG_E("QR_SCAN", "Exception in QR decoding: %s", e.what());
                context->qr_scan_cb(context->app_cb, out, num_qr_codes_detected, frame_time_epoch, QR_SCAN_QR_DETECTED);
                goto next_buffer;
            }

            // Check if the decoded output is empty
            if (out.empty()) {
                LOG_W("QR_SCAN", "Decode operation returned empty result, continuing to scan");
                context->qr_scan_cb(context->app_cb, out, num_qr_codes_detected, frame_time_epoch, QR_SCAN_QR_DETECTED);
            } else {
                // Check if any of the QR scan tags are present in the decoded output
                bool found_valid_tag = false;
                for (const auto& tag : qr_scan_tags) {
                    if (out.find(tag) != out.end()) {
                        found_valid_tag = true;
                        LOG_I("QR_SCAN", "Found valid tag '%s' in decoded QR code", tag.c_str());
                        break;
                    }
                }

                if (found_valid_tag) {
                    LOG_I("QR_SCAN", "Success in decoding the QR code for frame with timestamp %llu, sending callback to ndcentral to publish the decoded data", frame_time_epoch);
                    context->qr_scan_cb(context->app_cb, out, num_qr_codes_detected, frame_time_epoch, QR_SCAN_QR_DECODED);
                } else {
                    LOG_W("QR_SCAN", "Decoded QR code does not contain any valid tag fields, continuing to scan");
                    context->qr_scan_cb(context->app_cb, out, num_qr_codes_detected, frame_time_epoch, QR_SCAN_QR_DETECTED);

                    if (invalid_qr_image_dump_enabled == true) {
                        dump_invalid_qr_debug_image(y_plane, buffer_size, frame_time_epoch);
                    }
                }
            }
        } else {
            LOG_D("QR_SCAN", "No QR code detected in the current frame, continuing to scan");
            context->qr_scan_cb(context->app_cb, out, 0, 0, QR_SCAN_NO_QR);
        }
next_buffer:
        // Clear the contents of the vector out before moving to the next buffer
        out.clear();

        // Move to next buffer with wraparound
        buf_read_index = (buf_read_index + 1) % max_queued_yplanes;
    }

    // Send appropriate exit callback
    if (qr_scan_timedout == true) {
        LOG_I("QR_SCAN", "QR decoding stopped due to timeout, sending callback to ndcentral");
        qr_scan_timeout_thread_created = false;
        qr_scan_timedout = false;
        context->qr_scan_cb(context->app_cb, out, 0, 0, QR_SCAN_TIMEDOUT); //QR scan TIMEDOUT callback to ndcentral
    } else {
        LOG_I("QR_SCAN", "QR decoding stopped by ndcentral request");
    }

    LOG_I("QR_SCAN", "QR decode thread exiting");
    return NULL;
}

void TestTrack::TrackDataCB(uint32_t track_id, std::vector<BufferDescriptor> buffers, std::vector<MetaData> meta_buffers)
{
    //LOG_I(log_tag[track_info_.camera_id], "%s: Enter track_id(%d) meta_buffers[0].video_frame_type_info = %d", __func__, track_id, meta_buffers[0].video_frame_type_info);
    if (recorder_test_ == NULL) {
        LOG_E(log_tag[track_info_.camera_id], "%s: recorder_test is NULL", __func__);
        return;
    }
    int32_t ret = 0;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    int local_cam_num = 0;
    static uint64_t print_count = 0;
    cam_record_ctxt *context = (cam_record_ctxt *)(&(recorder_test.cam_ctxt[track_info_.camera_id]));
    unsigned int kinesis_ret = -1;
//#ifdef DEBUG
    FILE *fptr = NULL;
    //FILE *fptr1 = NULL;
//#endif
    int64_t TrackDataCB_entry_time = get_system_monotonic_time();
    int64_t TrackDataCB_exit_time = -1;

    switch (track_info_.track_type) {
    case TrackType::kVideoAVC:
    case TrackType::kVideoHEVC:
        pthread_mutex_lock(&recorder_test.cam_ctxt[track_info_.camera_id].rt_timestamp_mutex);
        context->rt_frame_timestamp = buffers[0].timestamp * 1000; // timestamp in nano seconds
        pthread_mutex_unlock(&recorder_test.cam_ctxt[track_info_.camera_id].rt_timestamp_mutex);
        print_count++;
        //LOG_I(log_tag[track_info_.camera_id], "frame time stamp = %lld", buffers[0].timestamp);
        if ((print_count % 30 == 0) && (track_id == 2)) {
            LOG_D(log_tag[track_info_.camera_id], "frame time stamp = %lld, track_id = %d", buffers[0].timestamp, track_id);
            print_count = 0;
        }

        //LOG_I(log_tag[track_info_.camera_id], "session_change = %d, session_change_ld = %d \n",
        //       session_change[track_info_.camera_id], session_change_ld[track_info_.camera_id] );
        if (track_id == g_track_id_hd[track_info_.camera_id]) {
            uint64_t ts_diff;
            if (previous_ts[track_info_.camera_id])
                ts_diff = (buffers[0].timestamp * 1000) - previous_ts[track_info_.camera_id];
            else {
                int fps = 5;
                if (track_info_.camera_id == CAMERA_POSITION_FRONT)
                    fps = OUTWARD_FPS;
                else if (track_info_.camera_id == CAMERA_POSITION_BACK)
                    fps = OTHER_FPS;
                ts_diff = (1000 / fps) * 1000 * 1000;
            }

            if (track_info_.camera_id == CAMERA_POSITION_FRONT) {
                if ((ts_diff > DROP_LIMIT_OUTWARD) && previous_ts[track_info_.camera_id])
                    LOG_E(log_tag[track_info_.camera_id], "Time difference between frames: %lld, buffer TS %lld, previous_ts %lld",
                                      ts_diff, buffers[0].timestamp * 1000, previous_ts[track_info_.camera_id]);
            } else if (track_info_.camera_id == CAMERA_POSITION_BACK) {
                if ((ts_diff > DROP_LIMIT_INWARD) && previous_ts[track_info_.camera_id])
                    LOG_E(log_tag[track_info_.camera_id], "Time difference between frames: %lld, buffer TS %lld, previous_ts %lld",
                                      ts_diff, buffers[0].timestamp * 1000, previous_ts[track_info_.camera_id]);
            }

            previous_ts[track_info_.camera_id] = buffers[0].timestamp * 1000;
        }

        if (track_id == g_track_id_hd[track_info_.camera_id]) {
            pthread_mutex_lock(&recorder_test.cam_ctxt[track_info_.camera_id].cam_frame_mutex);
            recorder_test.cam_ctxt[track_info_.camera_id].cam_frame_count++;
            recorder_test.cam_ctxt[track_info_.camera_id].session_frame_count++;
            pthread_mutex_unlock(&recorder_test.cam_ctxt[track_info_.camera_id].cam_frame_mutex);
#if DUMP_DEBUG_FRAME
            if (meta_buffers[0].video_frame_type_info == VIDEO_FRAME_INFO_KEYFRAME) {
                for (auto& iter : buffers) {
                    string fname = "/home/ubuntu/.nddevice/HD_bagheera_CAM" + to_string(track_info_.camera_id) + "_" + to_string(iter.timestamp) + ".h265";
                    FILE *fptr1 = fopen(fname.c_str(), "w+");
                    fwrite(iter.data, sizeof(char), iter.size, fptr1);
                    fclose(fptr1);
                }
            }
#endif

#if 0
            /* set blackout at the GOP boundary when privacy is enabled */
            context->is_I_frame = false;
            if (meta_buffers[0].video_frame_type_info == VIDEO_FRAME_INFO_KEYFRAME) {
                context->blackout_hd = context->privacy;
                context->is_I_frame = true;
            }
#endif
        }
#if DUMP_DEBUG_FRAME
        if (track_id == g_track_id_ld[track_info_.camera_id]) {
            if (meta_buffers[0].video_frame_type_info == VIDEO_FRAME_INFO_KEYFRAME) {
                for (auto& iter : buffers) {
                    string fname = "/home/ubuntu/.nddevice/LD_bagheera_CAM" + to_string(track_info_.camera_id) + "_" + to_string(iter.timestamp) + ".h265";
                    FILE *fptr1 = fopen(fname.c_str(), "w+");
                    fwrite(iter.data, sizeof(char), iter.size, fptr1);
                    fclose(fptr1);
                }
            }
        }
#endif

#if 0
        if (track_id == g_track_id_ld[track_info_.camera_id]) {
            /* set blackout at the GOP boundary when privacy is enabled */
            context->is_I_frame = false;
            if (meta_buffers[0].video_frame_type_info == VIDEO_FRAME_INFO_KEYFRAME) {
                context->blackout_ld = context->privacy;
                context->is_I_frame = true;
            }
        }
#endif
        // Condition makes sure that we go inside only for LD files
        if (context->ld_enabled && (session_change_ld[track_info_.camera_id] == true) &&
            (meta_buffers[0].video_frame_type_info == 24) &&
            (track_id == g_track_id_ld[track_info_.camera_id])) {
            LOG_I(log_tag[track_info_.camera_id], "New session for LD %d !!!!\n", track_info_.camera_id);

            LOG_I(log_tag[track_info_.camera_id], "frame time stamp = %lld", buffers[0].timestamp);
            Prepare(buffers[0].timestamp);
            session_change_ld[track_info_.camera_id] = false;
        }

        // Condition makes sure that we go inside only for HD files
        if ((session_change[track_info_.camera_id] == true) &&
            (meta_buffers[0].video_frame_type_info == 24) &&
            (track_id == g_track_id_hd[track_info_.camera_id])) {
            LOG_I(log_tag[track_info_.camera_id], "New session for %d !!!!\n", track_info_.camera_id);
            LOG_I(log_tag[track_info_.camera_id], "frame time stamp = %lld", buffers[0].timestamp);
            Prepare(buffers[0].timestamp);
            session_change[track_info_.camera_id] = false;
            session_change_ld[track_info_.camera_id] = true;

            if ((track_info_.camera_id == 0) && (ea_outward_enabled == 1)) {
                session_count++;
                if ((session_count % ea_freq_session_wise) == 0)
                    session_change_ea[track_info_.camera_id] = true;
            }
            /* This is being done to make sure that inward cam session
            * changes after outward cam session changes.
            * */
            if (track_info_.camera_id == 0)
                session_change[1] = true;
        }

        if (g_track_id_live[track_info_.camera_id] == track_id) {
#if DUMP_DEBUG_FRAME
            if (meta_buffers[0].video_frame_type_info == VIDEO_FRAME_INFO_KEYFRAME) {
                for (auto& iter : buffers) {
                    string fname = "/home/ubuntu/.nddevice/LS_bagheera_CAM" +  to_string(track_info_.camera_id) + "_" + to_string(iter.timestamp) + ".h264";
                    FILE *fptr1 = fopen(fname.c_str(), "w+");
                    fwrite(iter.data, sizeof(char), iter.size, fptr1);
                    fclose(fptr1);
                }
            }
#endif
#ifdef DEBUG
            if (track_info_.camera_id == 0) {
                fptr1 = fopen("/data/aws_kvs_0000.h264", "a+");
                if (fptr1 != NULL) {
                    int count = 0;
                    for (auto& iter : buffers) {
                        count++;
                        fwrite(iter.data, sizeof(char), iter.size, fptr1);
                        LOG_I(log_tag[track_info_.camera_id], "Calling putKinesisVideoFrame, count = %d, size = %d\n", count, iter.size);
                    }

                    //fwrite(context->kinesis_stream_info.frame.frameData, sizeof(char), buffers[0].size, fptr);
                    fclose(fptr1);
                }
            }
#endif
//            // When streaming enabled, fill kinesis stream info
            if (!streaming_outward || !streaming_inward) {
                // if inward camera, continue to stream only when privacy is disabled
                if (context->kinesis_req_cam_id == track_info_.camera_id) {
                    int32_t size = 0;
                    if (buffers[0].data != NULL) {
                        if (context->kinesis_stream_info.frame.frameData == NULL)
                            context->kinesis_stream_info.frame.frameData = (BYTE *)calloc(sizeof(char), MAX_FRAME_BUFFER_SIZE);

                        if (buffers[0].size > MAX_FRAME_BUFFER_SIZE) {
                            free(context->kinesis_stream_info.frame.frameData);
                            context->kinesis_stream_info.frame.frameData = NULL;
                            context->kinesis_stream_info.frame.frameData = (BYTE *)calloc(sizeof(char), buffers[0].size);
                        }
                        if (context->kinesis_stream_info.frame.frameData != NULL) {

                            if (context->privacy) {
                                //we are sending black iframe whenever privacy is enabled
                                //but once privacy is disabled, we have to wait for the next key frame for the normal frame
                                //so prevFrameState helps us to check when do we need to wait.
                                context->prevFrameState = INVALID_FRAME;
                                LOG_I("LIVESTREAM", "entered privacy case");
                                if ((black_frame_ls[track_info_.camera_id] != NULL) &&
                                    (black_frame_ls[track_info_.camera_id]->data != NULL) &&
                                    (black_frame_ls[track_info_.camera_id]->size > 0)) {
                                    memcpy((context->kinesis_stream_info.frame.frameData), (BYTE *)black_frame_ls[track_info_.camera_id]->data, black_frame_ls[track_info_.camera_id]->size);
                                    size = black_frame_ls[track_info_.camera_id]->size;
                                } else {
                                    LOG_E("LIVESTREAM", "black_frame_ls invalid for cam_id: %d", track_info_.camera_id);
                                }
                            }
                            else {
                                //privacy is disabled, so we can send the actual frame but still have to wauit for the next key frame 
                                //so till then we will send black frame
                                if (context->prevFrameState == INVALID_FRAME && !(meta_buffers[0].video_frame_type_info == VIDEO_FRAME_INFO_KEYFRAME)) {
                                        if ((black_frame_ls[track_info_.camera_id] != NULL) &&
                                            (black_frame_ls[track_info_.camera_id]->data != NULL) &&
                                            (black_frame_ls[track_info_.camera_id]->size > 0)) {
                                            memcpy((context->kinesis_stream_info.frame.frameData), (BYTE *)black_frame_ls[track_info_.camera_id]->data, black_frame_ls[track_info_.camera_id]->size);
                                            size = black_frame_ls[track_info_.camera_id]->size;
                                        } else {
                                            LOG_E("LIVESTREAM", "black_frame_ls invalid for cam_id: %d", track_info_.camera_id);
                                        }
                                }
                                else {
                                    //sending the actual frame
                                    for (auto &iter : buffers) {
                                        if (iter.data != NULL) {
                                            context->prevFrameState = VALID_FRAME;
                                            memcpy((context->kinesis_stream_info.frame.frameData + size), (BYTE *)iter.data, iter.size);
                                            size += iter.size;
                                        }
                                    }
                                }
                            }
                            context->kinesis_stream_info.frame.size = size;
                        } else {
                            LOG_E(log_tag[track_info_.camera_id], "Failed to allocate the heap memory\n");
                        }
                    }
                    if (context->kinesis_stream_info.first_time == true) {
                        context->kinesis_stream_info.frame.version = FRAME_CURRENT_VERSION;
                        context->kinesis_stream_info.frame.trackId = DEFAULT_VIDEO_TRACK_ID;
                        context->kinesis_stream_info.frame.duration = HUNDREDS_OF_NANOS_IN_A_SECOND / kinesis_req_fps;
                        context->kinesis_stream_info.frame.index = 0;
                        context->kinesis_stream_info.first_time = false;
                    }
                    if (meta_buffers[0].video_frame_type_info == 24) {
                        //LOG_I(log_tag[track_info_.camera_id], ">>>>> Key frame is available\n");
                        context->kinesis_stream_info.frame.flags = FRAME_FLAG_KEY_FRAME;
                    } else {
                        context->kinesis_stream_info.frame.flags = FRAME_FLAG_NONE;
                    }
#ifdef DEBUG
                    if (track_info_.camera_id == CAMERA_POSITION_FRONT){
                        fptr = fopen("/data/aws_kvs_0001.h264", "a+");
                    }
                    else{
                        fptr = fopen("/data/aws_kvs_0002.h264", "a+");
                    }
                    if (fptr != NULL) {
                        int count = 0;
                        for (auto& iter : buffers) {
                            count++;
                            fwrite(iter.data, sizeof(char), iter.size, fptr);
                            // LOG_I(log_tag[track_info_.camera_id], "Calling putKinesisVideoFrame, count = %d, size = %d", count, iter.size);
                        }
                        //fwrite(context->kinesis_stream_info.frame.frameData, sizeof(char), buffers[0].size, fptr);
                        fclose(fptr);
                    }
#endif
                    /* timestamp coming in micro seconds, covert it first to nano seconds */
                    context->kinesis_stream_info.frame.decodingTs = (buffers[0].timestamp * 1000) / DEFAULT_TIME_UNIT_IN_NANOS;
                    context->kinesis_stream_info.frame.presentationTs = context->kinesis_stream_info.frame.decodingTs;
                    if ((kinesis_dual_streaming && file_is_present(TMP_KINESIS_STREAMING_INWARD) && file_is_present(TMP_KINESIS_STREAMING_OUTWARD))) {
                        if (track_info_.camera_id == CAMERA_POSITION_FRONT && !streaming_outward || track_info_.camera_id == CAMERA_POSITION_BACK && !streaming_inward) {
                            kinesis_ret = putKinesisVideoFrame(context->kinesis_stream_info.streamHandle, &context->kinesis_stream_info.frame);
                            if (STATUS_SUCCESS != kinesis_ret) {
                                LOG_I(log_tag[track_info_.camera_id], "putKinesisVideoFrame status %d\n",kinesis_ret);
                            }
                        }
                    } else if (!kinesis_dual_streaming && context->kinesis_req_cam_id == track_info_.camera_id) {
                        kinesis_ret = putKinesisVideoFrame(context->kinesis_stream_info.streamHandle, &context->kinesis_stream_info.frame);
                        if (STATUS_SUCCESS != kinesis_ret) {
                            LOG_I(log_tag[track_info_.camera_id], "putKinesisVideoFrame status %d\n",kinesis_ret);
                        }
                    }
                    LOG_D(log_tag[track_info_.camera_id], "sending live streaming frame !!!!!!");
                    context->kinesis_stream_info.frame.index++;
                }
            } else {
                context->kinesis_stream_info.frameIndex = 0;
                context->kinesis_stream_info.frame.flags = FRAME_FLAG_NONE;
                context->kinesis_stream_info.first_time = false;
            }
        }

        for (uint32_t i = 0; i < meta_buffers.size(); ++i) {
            MetaData meta_data = meta_buffers[i];
            if (meta_data.meta_flag & static_cast<uint32_t>(MetaParamType::kVideoFrameType)) {
                LOG_D(log_tag[track_info_.camera_id], "%s: frame_type = %d", __func__, meta_data.video_frame_type_info);
            }
        }

        // Do not dump bit stream if it is live streaming
        if (g_track_id_live[track_info_.camera_id] != track_id) {
            if (recorder_test_->is_dump_bitstream_enabled_) {
                dump_bitstream_.Dump(buffers, context, track_info_);
            }
        }
        break;
    case TrackType::kVideoYUV:
        static int count[2] = {0, 0};
        for (uint32_t i = 0; i < meta_buffers.size(); ++i) {
            MetaData meta_data = meta_buffers[i];
            if (meta_data.meta_flag & static_cast<uint32_t>(MetaParamType::kCamBufMetaData)) {
                CameraBufferMetaData cam_buf_meta = meta_data.cam_buffer_meta_data;
                LOG_D(log_tag[track_info_.camera_id], "%s: format=%d", __func__, cam_buf_meta.format);
                LOG_D(log_tag[track_info_.camera_id], "%s: num_planes=%d", __func__, cam_buf_meta.num_planes);

                for (uint8_t i = 0; i < cam_buf_meta.num_planes; ++i) {
                    LOG_D(log_tag[track_info_.camera_id], "%s: plane[%d]:stride(%d)", __func__, i, cam_buf_meta.plane_info[i].stride);
                    LOG_D(log_tag[track_info_.camera_id], "%s: plane[%d]:scanline(%d)", __func__, i, cam_buf_meta.plane_info[i].scanline);
                    LOG_D(log_tag[track_info_.camera_id], "%s: plane[%d]:width(%d)", __func__, i, cam_buf_meta.plane_info[i].width);
                    LOG_D(log_tag[track_info_.camera_id], "%s: plane[%d]:height(%d)", __func__, i, cam_buf_meta.plane_info[i].height);
                }

                if (recorder_test_->is_dump_yuv_enabled_ && (track_info_.track_id != g_track_id_qr)) {
                    count[track_info_.camera_id]++;
                    int rt_fps = 4;
                    if (context != NULL) {
                        rt_fps = context->rt_config.fps;
                    }
                    int cam_fps = 30;
                    if (track_info_.camera_id == 1) {
                        cam_fps = 15;
                    }

                    if (count[track_info_.camera_id] % ((int)(cam_fps/rt_fps)) == 0) {
                        const char *ext = "yuv";
                        std::string file_path("/data/misc/qmmf/track_");
                        file_path += std::to_string(track_info_.track_id) + "_";
                        file_path += std::to_string(cam_buf_meta.plane_info[0].width);
                        file_path += "x" + std::to_string(cam_buf_meta.plane_info[0].height) + "_";
                        file_path += std::to_string(buffers[i].timestamp) + ".";
                        file_path += ext;
                        recorder_test_->DumpFrameToFile(buffers[i], cam_buf_meta, file_path, track_info_.camera_id);
                    }
                }

                if ((track_info_.camera_id == 0) && (ea_outward_enabled == 1)) {
                    static bool first_cb_ea = true;
                    // In case of first session, first few frames are skipped as camera may not be stable at the start hence may generate green/bright/dark images
                    if ((first_cb_ea == true && context->cam_frame_count >= EA_PREVIEW_OUTWARD_FIRST_SESSION_FRAME_SKIP_COUNT) || (session_change_ea[track_info_.camera_id] == true)) {
                        if (first_cb_ea) {
                            first_cb_ea = false;
                            raw_buffer_width = cam_buf_meta.plane_info[0].width;
                            raw_buffer_height = cam_buf_meta.plane_info[0].height;
                            pthread_t jpeg_enc_th;
                            pthread_create(&jpeg_enc_th, NULL, &enc_and_save_ea_image, context);
                        }
                        session_change_ea[track_info_.camera_id] = false;
                        pthread_mutex_lock(&ea_mutex);
                        //Dump yuv data to a file for image encoding
                        std::ofstream yuvFile(image_raw_data_file, std::ios::binary);
                        void *temp;
                        int offset = 0;
                        for (uint32_t i = 0; i < cam_buf_meta.num_planes; ++i) {
                            temp = static_cast<void*>((static_cast<uint8_t*>(buffers[0].data) + offset));
                            yuvFile.write(reinterpret_cast<char*>(temp), cam_buf_meta.plane_info[i].width * cam_buf_meta.plane_info[i].height);

                            offset += cam_buf_meta.plane_info[i].stride * (cam_buf_meta.plane_info[i].scanline);
                        }
                        yuvFile.close();
                        is_image_raw_data_present = true;
                        pthread_cond_signal(&ea_cond);
                        pthread_mutex_unlock(&ea_mutex);
                    }
                }
                // QR scan processing
                if ((track_info_.track_id == g_track_id_qr) && (start_QR_scan == true)) {
                    if (first_time_scan) {
                        first_time_scan = false;
                        qrscan_buffer_width = cam_buf_meta.plane_info[0].width;
                        qrscan_buffer_height = cam_buf_meta.plane_info[0].height;

                        // Allocate y_planes if not already allocated
                        if (y_planes) {
                            LOG_W("QR_SCAN", "y_planes buffer already allocated");
                        } else {
                            y_planes = (uint8_t *)malloc(max_queued_yplanes * (qrscan_buffer_width * qrscan_buffer_height));
                            if (!y_planes) {
                                LOG_E("QR_SCAN", "Failed to allocate y_planes buffer");
                                return;
                            }
                        }

                        if (scanner_ptr) {
                            scanner_ptr->Initialize(qrscan_buffer_width, qrscan_buffer_height);
                            scanner_ptr->SetInterestedTags(qr_scan_tags);
                        }

                        buf_write_index = 0;
                        stop_qrdecode_thread = false;
                        is_yplane_buf_present = false;

                        int ret = pthread_create(&qr_code_detect_decode_th, NULL, &qr_code_detect_decode_thread, context);
                        if (ret != 0) {
                            LOG_E("QR_SCAN", "Failed to create decode thread: %d", ret);
                            free(y_planes);
                            y_planes = NULL;
                            return;
                        }
                        qr_decode_thread_created = true;
                    }

                    if (!y_planes) {
                        LOG_E("QR_SCAN", "y_planes is NULL, cannot copy buffer");
                        return;
                    }

                    pthread_mutex_lock(&qr_scan_mutex);
                    void *src_ptr = static_cast<void*>(static_cast<uint8_t*>(buffers[0].data));
                    void *dst_ptr = static_cast<void*>(static_cast<uint8_t*>(y_planes + (buf_write_index * (qrscan_buffer_width * qrscan_buffer_height))));
                    memcpy(reinterpret_cast<char*>(dst_ptr), reinterpret_cast<char*>(src_ptr), qrscan_buffer_width * qrscan_buffer_height);
                    /* buffer timestamp is coming as monotonic in ns, need to calculate the epoch timestamp using that */
                    uint64_t frame_epoch_time = get_system_time() - ((get_system_monotonic_time_ns() - buffers[0].timestamp) / 1000000); //in milliseconds

                    // Overwrite first 8 bytes of dst_ptr with frame_epoch_time
                    memcpy(dst_ptr, &frame_epoch_time, sizeof(frame_epoch_time));

                    is_yplane_buf_present = true;

                    buf_write_index++;
                    if (buf_write_index == max_queued_yplanes)
                        buf_write_index = 0;

                    pthread_cond_signal(&qr_scan_cond);
                    pthread_mutex_unlock(&qr_scan_mutex);
                }
            }
        }
        break;
    default:
        break;
    }

    // Return buffers back to service.
    int32_t RETRY_COUNT_ReturnTrackBuffer = 5;
    while (RETRY_COUNT_ReturnTrackBuffer--) {
        ret = recorder_test_->GetRecorder().ReturnTrackBuffer(track_info_.session_id, track_id, buffers);
        if (ret != NO_ERROR) {
            LOG_E(log_tag[track_info_.camera_id], "%s: ReturnTrackBuffer failed", __func__);
            sleep(RETRY_DELAY);
        } else {
            break;
        }
    }
    if (ret != NO_ERROR) {
        string str_msg = "ReturnTrackBuffer failed for track_id " + to_string(track_id);
        nd_service_obj->send_err_msg(SM_E_NDC_RETURNTRACKBUFFER_FAIL, track_id, str_msg);
        return;
    }
    TrackDataCB_exit_time = get_system_monotonic_time();
    if ((TrackDataCB_exit_time - TrackDataCB_entry_time) > QMMF_DATA_CB_BLOCK_THRESHOLD_IN_MSEC) {
        LOG_W(log_tag[track_info_.camera_id], "%s: TrackDataCB execution time %lld ms exceeded Threshold of %d ms", __func__,
              (TrackDataCB_exit_time - TrackDataCB_entry_time), QMMF_DATA_CB_BLOCK_THRESHOLD_IN_MSEC);
        LOG_W(log_tag[track_info_.camera_id], "It took time to return the buffer back to QMMF for track_id:%d, track_type:%d", track_id, track_info_.track_type);
    }
    LOG_D(log_tag[track_info_.camera_id], "%s: Exit", __func__);
}

qmmf_status_t DumpBitStream::SetUp(const StreamDumpInfo& dumpinfo, TrackInfo_t track_info, uint64_t timestamp)
{
    LOG_I(log_tag[track_info.camera_id], "%s: Enter cam_num = %d", __func__, track_info.camera_id);
    cam_record_ctxt *context = (cam_record_ctxt *)(&(recorder_test.cam_ctxt[track_info.camera_id]));
    const char *f = "";

    Close();

    // Check if the track is for HD
    if ((track_info.track_id == g_track_id_hd[0]) || (track_info.track_id == g_track_id_hd[1])) {
        LOG_I(log_tag[track_info.camera_id], "track_id = %d, g_track_id_hd[0] = %d, g_track_id_hd[1] = %d, session_id = %d",
               track_info.track_id, g_track_id_hd[0], g_track_id_hd[1], track_info.session_id);

        uint64_t stop_raw_time_us = get_system_monotonic_time_ns() / 1000;
        context->session_epoch_time = get_system_time_ns() / 1000; //session_epoch_time in microseconds
        if ((NULL != context->record_cb) && (first_frame[dumpinfo.cam_num] == false)) {
            LOG_I(log_tag[track_info.camera_id], "calling GST_RECORD_FILE_STOP for cam_num = %d, filename: %s", dumpinfo.cam_num, context->cur_file_name);
            context->record_cb(GST_RECORD_FILE_STOP,
                               stop_raw_time_us, context->session_epoch_time,
                               context->session_start_pts,
                               context->app_cb,
                               context->session_frame_count, false, context->cur_file_name);
            pthread_mutex_lock(&context->cam_frame_mutex);
            context->session_frame_count = 0;
            pthread_mutex_unlock(&context->cam_frame_mutex);
        }

        // first session condition match
        if ((context->first_frame_pts == 0) || (timestamp == 0)) {
            context->session_start_pts = 0;
        } else {
            context->session_start_pts = (timestamp * 1000) - context->first_frame_pts; // session_start_pts in nano seconds
        }
        uint64_t start_raw_time_us = get_system_monotonic_time_ns() / 1000;

        if ( NULL != context->fname_cb) {
            uint64_t session_first_frame_epoch_time_ms = context->session_epoch_time / 1000;
            LOG_I(log_tag[track_info.camera_id], "session_first_frame_epoch_time_ms: %llu", session_first_frame_epoch_time_ms);
            f = context->fname_cb(context->app_fname, session_first_frame_epoch_time_ms);
        }

        if (file_fd_ > 0) {
            close(file_fd_);
            file_fd_ = -1;
            LOG_I(log_tag[track_info.camera_id], "File FD closed for an HD session");
        }

        file_fd_ = open(f, O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (file_fd_ <= 0) {
            LOG_I(log_tag[track_info.camera_id], "%s File open failed! %s", __func__, f);
            return BAD_VALUE;
        }

        memset(context->cur_file_name, 0x0, GSTREAMER_NAME_LENGTH_MAX);
        strncpy(context->cur_file_name, f, GSTREAMER_NAME_LENGTH_MAX);

        free((char*)f);

        if (first_frame[dumpinfo.cam_num] == true &&  NULL != context->event_cb) {
            context->event_cb(GST_RECORD_EVENT_FIRST_FRAME_MARKER, context->app_cb);
            first_frame[dumpinfo.cam_num] = false;
        }

        //Give Record start callback
        if (NULL != context->record_cb) {
            context->record_cb(GST_RECORD_FILE_START,
                               start_raw_time_us, context->session_epoch_time,
                               context->session_start_pts,
                               context->app_cb, 0, false, NULL);
        }
    } else if (context->ld_enabled && ((track_info.track_id == g_track_id_ld[0]) || (track_info.track_id == g_track_id_ld[1]))) {
        LOG_I(log_tag[track_info.camera_id], "track_id = %d, g_track_id_ld[0] = %d, g_track_id_ld[1] = %d, session_id = %d",
               track_info.track_id, g_track_id_ld[0], g_track_id_ld[1], track_info.session_id);

        // first session condition match
        if ((context->first_frame_pts == 0) || (timestamp == 0)) {
            context->session_start_pts_ld = 0;
        } else {
            context->session_start_pts_ld = (timestamp * 1000)  - context->first_frame_pts; // session_start_pts_ld in nano seconds
        }

        uint64_t stop_raw_time_us = get_system_monotonic_time_ns() / 1000;
        context->session_epoch_time_ld = get_system_time(); //session_epoch_time_ld is in milliseconds for both front and back cameras

        if ((NULL != context->record_cb) && (first_frame_ld[dumpinfo.cam_num] == false)) {
            LOG_I(log_tag[track_info.camera_id], "calling for LQ file GST_RECORD_FILE_STOP for cam_num = %d", dumpinfo.cam_num);
            //Sending RECORD_STOP callback for LD files
            context->record_cb(GST_RECORD_FILE_STOP,
                               stop_raw_time_us, context->session_epoch_time_ld,
                               context->session_start_pts_ld,
                               context->app_cb,
                               context->session_frame_count,
                               true,
                               context->cur_file_name_ld);
        }

        if (first_frame_ld[dumpinfo.cam_num] == true) {
            first_frame_ld[dumpinfo.cam_num] = false;
        }

        LOG_I(log_tag[track_info.camera_id], "calling timestamp_cb from GST !!!!!!!!");
        if (context->timestamp_cb != NULL) {
            context->timestamp_cb(context->session_epoch_time_ld,
                    context->session_start_pts_ld,
                    context->app_cb);
        }
        memset(context->cur_file_name_ld, 0x0, GSTREAMER_NAME_LENGTH_MAX);
        strncpy(context->cur_file_name_ld, context->cur_file_name, GSTREAMER_NAME_LENGTH_MAX);
        strcat(context->cur_file_name_ld, ld_extn);

        if (file_fd_ > 0) {
            close(file_fd_);
            file_fd_ = -1;
            LOG_I(log_tag[track_info.camera_id], "File FD closed for an LD session");
        }

        LOG_I(log_tag[track_info.camera_id], "cur_file_name_ld = %s", context->cur_file_name_ld);
        file_fd_ = open(context->cur_file_name_ld, O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (file_fd_ <= 0) {
            LOG_E(log_tag[track_info.camera_id], "%s File open failed! %s", __func__, context->cur_file_name_ld);
            return BAD_VALUE;
        }
    }
    LOG_I(log_tag[track_info.camera_id], "%s: Exit", __func__);

    return NO_ERROR;
}

void DumpBitStream::Close()
{
    LOG_I(log_tag[0], "%s: Enter", __func__);
    if (file_fd_ > 0) {
        close(file_fd_);
        file_fd_ = -1;
    }
    LOG_I(log_tag[0], "%s: Exit", __func__);
}

qmmf_status_t DumpBitStream::Dump(const std::vector<BufferDescriptor>& buffers, cam_record_ctxt *context, TrackInfo_t track_info)
{
    if (context->blackout_hd == true) {
        if (track_info.track_id == g_track_id_hd[context->cam_pos]) {
#if 0
            if (context->is_I_frame == true) {
                /* write 1 sec worth of black frames at the start of GOP */
                uint32_t written_length = write(file_fd_, black_frame_hd[context->cam_pos]->data, black_frame_hd[context->cam_pos]->size);
                context->is_I_frame = false;
            }
#endif
            return NO_ERROR;
        }
    }

    if(context->blackout_ld == true) {
        if(track_info.track_id == g_track_id_ld[context->cam_pos]) {
#if 0
        /* write 1 sec worth of black frames at the start of GOP */
            if(context->is_I_frame == true)
              {
                uint32_t written_length = write(file_fd_, black_frame_ld[context->cam_pos]->data, black_frame_ld[context->cam_pos]->size);
                context->is_I_frame = false;
            }
#endif
            return NO_ERROR;
        }
    }

    if  (file_fd_ <= 0) {
        LOG_E(LOG_TAG, "%s: file fd is not correct", __func__);
        return NO_ERROR;
    }

    for (auto& iter : buffers) {

        uint32_t exp_size = iter.size;
        uint32_t written_length = 0;
        unsigned char* encrypted_buffer = NULL;
        size_t encrypted_buffer_len = 0;
        size_t file_len1 = 0;
        int64_t file_write_start_time = -1;
        int64_t file_write_end_time = -1;
        //printf("%s: BitStream buffer data(0x%x):size(%d):ts(%lld):flag(0x%x)"
        //  ":buf_id(%d):capacity(%d)",  __func__, iter.data, iter.size,
        //   iter.timestamp, iter.flag, iter.buf_id, iter.capacity);

        if (stream_encryption)
        {
            file_len1 =  nd_stream_encryption((unsigned char*)iter.data, iter.size, &encrypted_buffer, &encrypted_buffer_len);
            LOG_D(log_tag[context->cam_pos], "%s:%d nd_stream_encryption return val:%d", __func__, __LINE__, file_len1);
            LOG_D(log_tag[context->cam_pos], "NRT frame TS = %lld, frame size = %d", iter.timestamp, iter.size);
            if (encrypted_buffer)
            {
                file_write_start_time = get_system_monotonic_time();
                written_length = write(file_fd_, encrypted_buffer, encrypted_buffer_len);
                exp_size = encrypted_buffer_len;
                free(encrypted_buffer);
                LOG_D(log_tag[context->cam_pos], "%s:%d Freeing the encrypted buffer memory", __func__, __LINE__);
            }
            else
            {
                LOG_E(log_tag[context->cam_pos], "%s:%d Stream encryption is failed, writing in file without encryption", __func__, __LINE__);
                file_write_start_time = get_system_monotonic_time();
                written_length = write(file_fd_, iter.data, iter.size);
            }
        }
        else {
            file_write_start_time = get_system_monotonic_time();
            written_length = write(file_fd_, iter.data, iter.size);
        }
        file_write_end_time = get_system_monotonic_time();
        if ((file_write_end_time - file_write_start_time) > QMMF_DATA_CB_BLOCK_THRESHOLD_IN_MSEC) {
            LOG_W(log_tag[context->cam_pos], "%s: Time taken to write frame to file is %lld ms, exceeded threshold of %d ms", __func__,
                  (file_write_end_time - file_write_start_time), QMMF_DATA_CB_BLOCK_THRESHOLD_IN_MSEC);
            LOG_W(log_tag[context->cam_pos], "It took time to file write for track_id:%d, track_type:%d", track_info.track_id, track_info.track_type);
        }
        //printf("%s: written_length(%d)", __func__, written_length);
        if (written_length != exp_size) {
            LOG_E(LOG_TAG, "%s: Bad Write error (%d) %s", __func__, errno,
            strerror(errno));
            return BAD_VALUE;
        }
        if (iter.flag & static_cast<uint32_t>(BufferFlags::kFlagEOS)) {
            LOG_I(LOG_TAG, "%s EOS Last buffer!", __func__);
            Close();
        }
    }
    LOG_D(LOG_TAG, "%s: Exit", __func__);

    return NO_ERROR;
}

void destroy_camera_record_platform(void *ptr)
{
   return ;
}

int stop_record_session_platform (void *ptr)
{
   return true;
}

void *recreate_outcam_rt_shared_memory(void *arg)
{
    cam_record_ctxt *context = (cam_record_ctxt *)arg;

    pthread_mutex_lock(&context->shm_writer_mutex);

    drop_outcam_rt_frames = true;

    LOG_I(log_tag[context->cam_pos], "SHM_DEBUG :: Deleting older instance of outcam shm_writer 0x%x", context->shm_writer);
    delete context->shm_writer;
    context->shm_writer = NULL;

    int64_t smb_data_size = (int64_t)(context->rt_config.width * context->rt_config.height * 3);
    context->shm_writer = new NdSharedMemoryWriter(context->shm_writer_name, smb_data_size, context->shm_buffers);
    if (context->shm_writer) {
        LOG_I(log_tag[context->cam_pos],"Shared Memory Writer 0x%x recreated successfully for outward camera with smb_data_size: %lld and shm_buffers: %d",
                context->shm_writer, smb_data_size, context->shm_buffers);

        context->shm_writer->set_log_frequency(100);
        context->shm_writer->register_write_callback(frame_shm_write_cb_func);

        nd_service_obj->send_err_msg(SM_E_NDC_CAM_SHM_RECREATED, context->cam_pos, "RT shared memory recreated for outward camera");
    }
    drop_outcam_rt_frames = false;

    pthread_mutex_unlock(&context->shm_writer_mutex);

    return NULL;
}

void *recreate_incam_rt_shared_memory(void *arg)
{
    cam_record_ctxt *context = (cam_record_ctxt *)arg;

    pthread_mutex_lock(&context->shm_writer_mutex);

    drop_incam_rt_frames = true;

    LOG_I(log_tag[context->cam_pos], "SHM_DEBUG :: Deleting older instance of incam shm_writer 0x%x", context->shm_writer);
    delete context->shm_writer;
    context->shm_writer = NULL;

    int64_t smb_data_size = (int64_t)(context->rt_config.width * context->rt_config.height * 3);
    context->shm_writer = new NdSharedMemoryWriter(context->shm_writer_name, smb_data_size, context->shm_buffers);
    if (context->shm_writer) {
        LOG_I(log_tag[context->cam_pos], "Shared Memory Writer 0x%x recreated successfully for inward camera with smb_data_size: %lld and shm_buffers: %d",
                context->shm_writer, smb_data_size, context->shm_buffers);
        context->shm_writer->set_log_frequency(100);
        context->shm_writer->register_write_callback(frame_shm_write_cb_func);

        nd_service_obj->send_err_msg(SM_E_NDC_CAM_SHM_RECREATED, context->cam_pos, "RT shared memory recreated for inward camera");
    }
    drop_incam_rt_frames = false;

    pthread_mutex_unlock(&context->shm_writer_mutex);

    return NULL;
}

void recreate_rt_shared_memory(void *ptr)
{
    cam_record_ctxt *context = (cam_record_ctxt *)ptr;
    pthread_t recreate_shm_mem_th;

    if (context->cam_pos == CAMERA_POSITION_FRONT)
        pthread_create(&recreate_shm_mem_th, NULL, &recreate_outcam_rt_shared_memory, ptr);
    else if (context->cam_pos == CAMERA_POSITION_BACK)
        pthread_create(&recreate_shm_mem_th, NULL, &recreate_incam_rt_shared_memory, ptr);
}
