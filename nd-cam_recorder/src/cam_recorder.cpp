/* Copyright (C) 2016 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Danish Rauf <danish.rauf@netradyne.com>, April-May 2022
 */

#include <glib.h>
#include <gst/gst.h>
#include "cam_recorder.h"
#include <pthread.h>
#include <stdio.h>
#include <sched.h>
#include "nd_time.h"
#include "service_utils.h"
#include "log.h"
#include <stdint.h>
#include "gst/app/gstappsink.h"
#include <sstream>
#include <sys/syscall.h>
#include <sys/sysinfo.h>

#include "file_helper.h"
#include "config_parser.h"
#include "nd_cam_utils.h"
#include "nd_file_utils.h"
#include "nd_shared_mem_utils.h"
#include <nd_task.h>
#include "nd_gpio.h"
#include "nd_factory.h"
#include <nd_db_utils.h>
#include <nd_prop_utils.h>
#include <nd_auth_openssl.h>
#include <nd_auth_utils.h>
#include <nd_accessory_db.h>
#include <jansson/jansson.h>

#include <sstream>
#include "livestreaming_kinesis.h"
#include <audio.pb.h>
#include <sys/timerfd.h>
#include <nd_net_utils.h>

#include <zmq.h>
#include <atomic>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include "QRScanner.h"
#include "nd_messenger.h"

#define DEVICE_CONFIG_INI "/home/ubuntu/config/deviceconfig.ini"
#define CLOUD_CONFIG_INI "/home/ubuntu/.nddevice/latest/cloudconfig.ini"
#define BAGHEERA_CONFIG_INI "/home/ubuntu/.nddevice/latest/bagheera_config.ini"
#define ND_CONFIG_INI "/home/ubuntu/.nddevice/latest/nd_config.ini"

#define GSTREAMER_NAME_LENGTH_MAX (512)

static int INWARD_MIN_INTRA_FRAME_INTERVAL_NS;
static int DMS_MIN_INTRA_FRAME_INTERVAL_NS;

static const int DELAY_MAIN_EXIT = 2 * 60; //If minimum free space is not available exit after this delay in secs.
static const int64_t MIN_FREE_SPACE = 256 * 1024 * 1024; //256 MB of minimum free space for bagheera to run
static bool camrec_service_exiting = false;

#define OTHER_FPS 15
#define MS_IN_SECONDS 1000
#define MS_TO_NS 1000000

#define fps_15_2_frames ((MS_IN_SECONDS/OTHER_FPS) * 2)

#define DROP_LIMIT (fps_15_2_frames * MS_TO_NS)

#define SIDECAM_BITRATE 500000
#define SIDECAM_FPS 15

#define DEBUG_15_FPS

#define MAX_PUBLISHER_Q_SZ 16

#define LINE_LENGTH 256

#define ROUTE_LOGS

#define CPU_CORE_0 0 //refers to CPU Core 0 of device to set CPU affinity for Inward recording, Outward recording and RT threads
#define CPU_CORE_3 3 //refers to CPU Core 3 of device to set CPU affinity for DMS, Left and Right recording threads

NDService *cam_record_service_obj; //Camera Recorder service object, to detect crashes
ND_DeviceFactory *nd_device_obj = NULL;

nd_msgq_t *msg_q;

static const char *TAG="CAM_REC";

static const int INWARD_CAM_STATUS_MASK = 0x2;
static const int LEFT_CAM_STATUS_MASK = 0x4;
static const int RIGHT_CAM_STATUS_MASK = 0x8;
static const int DELAY_NO_FILES_FOLDER = 30;
int cam_crash_count[CAMERA_POSITION_MAX] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
int max_cam_crash_count = 5;
db_handle_t* db_handle;
db_handle_t* db_handle_camera_crash = NULL;

static const string inward_cam_reset_cmd = GPIO_TEST_APP_SIG(IN_CAM_GPIO, 0); // "/bin/vendor/gpio_test -n 277 -s 0";
static const string inward_cam_oo_reset_cmd = GPIO_TEST_APP_SIG(IN_CAM_GPIO, 1); // "/bin/vendor/gpio_test -n 277 -s 1";

static const string inward_boot_status_set_cmd = "i2cset -f -y 2 0x3c 0xfd 0x00" ;
static const string inward_boot_status_cmd = "i2cget -f -y 2 0x3c 0x02" ;
static const string inward_boot_status2_cmd = "i2cget -f -y 2 0x3c 0x03" ;
static const string left_cam_boot_status_cmd = "/home/ubuntu/.nddevice/latest/service/bagheera/i2c_transfer 0 16 2 36 300a | cut -d' ' -f 7";
static const string left_cam_boot_status2_cmd = "/home/ubuntu/.nddevice/latest/service/bagheera/i2c_transfer 0 16 2 36 300b | cut -d' ' -f 7";
static const string right_cam_boot_status_cmd = "/home/ubuntu/.nddevice/latest/service/bagheera/i2c_transfer 0 16 7 36 300a | cut -d' ' -f 7";
static const string right_cam_boot_status2_cmd = "/home/ubuntu/.nddevice/latest/service/bagheera/i2c_transfer 0 16 7 36 300b | cut -d' ' -f 7";
static const string right_mipi_status_cmd = "i2cdump -f -y 6 0x6c | grep \"44 50 48 59 31 30 30 20\"";
static const string left_mipi_status_cmd = "i2cdump -f -y 8 0x6c | grep \"44 50 48 59 31 30 30 20\"";

static const string side_cams_reset_cmd = GPIO_TEST_APP_SIG(SIDE_CAM_GPIO, 0); // "/bin/vendor/gpio_test -n 457 -s 0";
static const string side_cams_oo_reset_cmd = GPIO_TEST_APP_SIG(SIDE_CAM_GPIO, 1); //  "/bin/vendor/gpio_test -n 457 -s 1";
static const string right_cam_retimer_cmd = GPIO_TEST_APP_SIG(RIGHT_CAM_RETIMER_GPIO, 1); // "/bin/vendor/gpio_test -n 267 -s 0";
static const string right_cam_oo_retimer_cmd = GPIO_TEST_APP_SIG(RIGHT_CAM_RETIMER_GPIO, 0); //  "/bin/vendor/gpio_test -n 267 -s 1";
static const string left_cam_retimer_cmd = GPIO_TEST_APP_SIG(LEFT_CAM_RETIMER_GPIO, 1); // "/bin/vendor/gpio_test -n 443 -s 0";
static const string left_cam_oo_retimer_cmd = GPIO_TEST_APP_SIG(LEFT_CAM_RETIMER_GPIO, 0); //  "/bin/vendor/gpio_test -n 443 -s 1";

static const int CAM_BOOT_STATUS_CHECK_TIMEOUT = 5;
static const int CAMERA_RESET_DURATION = 100 * 1000; // Giving enough time(100 ms) to reset camera sensors and ISP
static const int SESSION_FILENAME_MONITOR_INTERVAL_SEC = 90; // Monitor session filename every SESSION_FILENAME_MONITOR_INTERVAL_SEC seconds to check if it has changed
static const int MAX_WAIT_TIME_FOR_SENDING_CRITICAL_INFO = 20; // Maximum wait time in seconds for sending critical info when DMS is enabled in nd_config.ini but disconnected (waiting for service monitor to start)

static const unsigned long DMS_RECONNECT_GRACE_WINDOW_US = 7UL * 1000UL * 1000UL; // 7 seconds grace window to allow DMS reconnect after disconnection

static const string VIDEO_FILES_BASE_PATH = "/home/iriscli/files" ;

static const string SIDE_CAM_CRASH_INFO_DB_TABLE = "SIDE_CAM_CRASH_INFO";
int64_t CRASH_TIME_DIFF_THRESHOLD_MS = 7200000;

int NUM_CAMERA_BOOT_STATUS_RETRY = 5; //Try resetting Camera 5 times
int NUM_SIDE_CAMERA_BOOT_STATUS_RETRY = 3; //Try resetting side Camera 3 times

bool cams_enabled[CAMERA_POSITION_MAX] = {true, false, false, false, false, false, false, false, false};

bool cam_crash_status[CAMERA_POSITION_MAX] = {false, false, false, false, false, false, false, false, false};
bool cam_crash_bus_status[CAMERA_POSITION_MAX] = {false, false, false, false, false, false, false, false, false};

static char session_filename[GSTREAMER_NAME_LENGTH_MAX];

static bool g_live_streaming = false;
static bool stream_encryption = true;

int main_thread_pid = 0;
int DELAY_CAMERA_BOOT_STATUS_CHECK = 2; // Delay after camera reset before checking boot status
int DELAY_SIDE_CAMERA_BOOT_STATUS_CHECK = 1; // Delay after side camera reset before checking boot status

static const int i_interval[CAMERA_POSITION_MAX] = { 30, 15, 15, 15, 0, 0, 0, 0, 30 };

static const string Q_NAME = "q_cam_rec";

static const char ld_extn[] = ".ld.mp4";

static int inward_rt_buf_size;
static int dms_rt_buf_size;

uint64_t previous_time[CAMERA_POSITION_MAX] = {0,0,0,0,0,0,0,0,0};

#ifdef DEBUG_15_FPS
int cam_reg_write(int cam_num, int reg, unsigned char value);
int cam_reg_read(int cam_num, int reg, unsigned char *value);

#define SIDE_FPS_REG 0x380E
#define SIDE_FPS_VAL 0x6

#define INW_FPS_REG1 0xFD
#define INW_FPS_VAL1 0x01

#define INW_FPS_REG2 0x05
#define INW_FPS_VAL2 0x06

#define INW_FPS_REG3 0x06
#define INW_FPS_VAL3 0x00
#endif

#define VERSION_STRING_LENGTH (32)

#define CAM_CRASH_THREAD_INTERVAL_IN_SECS 5
#define INIT_CAM_CRASH_THREAD_INTERVAL_IN_SECS 10
#define FPS_THRESHOLD_FOR_CAM_CRASH 5.0

#ifdef DMS_CAMERA_SUPPORTED
/* BSP handle for DMS camera interface */
dmsCam_t dmsCam;
static unsigned char dmsCam_SN[SN_LEN + 1];
static const string dms_connection_status_file = "/dev/shm/is_dms_connected";
ofstream dms_connection_status_file_write_fd;
int dms_camera_flip = 0;
GMutex dms_disconnect_connect_mutex;
pthread_t dms_irled_thread;
nd_msgq_t* msgq_dms_irled = NULL;
static const string DMS_IRLED_MSGQ_NAME = "DMS_IRLED_MSGQ";
typedef enum {
    DMS_IRLED_EVENT_STARTED_STREAMING = 1,
    DMS_IRLED_EVENT_SET_LED_COLOR,
    DMS_IRLED_EVENT_CHECK_STATUS,
    DMS_IRLED_EVENT_MAX
} dms_irled_event_t;

typedef struct {
    dms_irled_event_t  event;
    dmsLedColor_t      led_colour;
    char               session_filename[GSTREAMER_NAME_LENGTH_MAX];
} dms_irled_event_info_t;

bool restart_dms_recording();
bool init_dms_camera();
bool deinit_dms_camera();
#endif

std::atomic<bool> is_dms_streaming_started(false);
static bool is_dms_disconnected_and_reconnected = false;

static const string DMS_NODE = "/dev/dms_h264";
static bool is_dms_connected = false;
static bool is_dms_disconnect_connect_happened = false;
static bool is_dms_enabled_in_config = false;

static std::atomic<bool> is_dms_initializing{false};
static unsigned long dms_init_complete_time_us = 0;

typedef enum _sensor_map_version
{
   SENSOR_MAP_VERSION_INVALID = -1,
   SENSOR_MAP_VERSION_0_1,
   SENSOR_MAP_VERSION_1_0,
   SENSOR_MAP_VERSION_MAX
} sensor_map_version;

sensor_map_version version_id = SENSOR_MAP_VERSION_INVALID;

typedef struct cam_record_ctxt
{
  gint            width;
  gint            height;
  camera_pos      cam_pos;
  GCond           cond;
  GMutex          mutex;
  gint            cond_var;
  GstElement      *pipeline;
  GstElement      *videorate_nrt;
  GstElement      *videorate_rt;
  GstElement      *videorate_tc;
  guint64         videorate_nrt_dup_count;
  guint64         videorate_rt_dup_count;
  guint64         videorate_tc_dup_count;
  GstPad          *mux_src_pad;
  gulong          mux_src_probe;

  // realtime
  realtime_camera_config_t rt_config;
  int             yuvbuf_size_rt;

  gint            analytics_width_driv;
  gint            analytics_height_driv;

  nrt_in_camera_config_t nrt_inward_config;
  nrt_left_camera_config_t nrt_leftcam_config;
  nrt_right_camera_config_t nrt_rightcam_config;
  nrt_dms_camera_config_t nrt_dms_config;

  // LD Configs
  nrt_in_camera_config_t nrt_ld_inward_config;
  nrt_dms_camera_config_t nrt_ld_dms_config;

  GstPad          *keep_alive_pad;
  gulong          keep_alive_probe;
  GstPad          *queue_rt_pad;
  gulong          queue_rt_probe;


  gulong          total_frame_count;
  gulong          session_frame_count;
  gulong          cam_frame_count;
  GMutex          cam_frame_mutex;
  GThread         *thread;
  gint             queue_msg;
  pipeline_state   state;
  GAsyncQueue      *queue;

  gchar            cur_file_name[GSTREAMER_NAME_LENGTH_MAX];
  gchar            cur_file_name_ld[GSTREAMER_NAME_LENGTH_MAX];

  volatile  uint64_t  session_start_epoch;
  volatile  uint64_t  session_end_epoch;
  volatile  uint64_t  session_start_pts;

  volatile  uint64_t  session_start_epoch_ld;
  volatile  uint64_t  session_end_epoch_ld;
  volatile  uint64_t  session_start_pts_ld;

  bool stop_recording_received;

  //kinesis
  aws_kinesis_stream_info_t kinesis_stream_info;

  //shared memory
  NdSharedMemoryWriter *shm_writer;
  void                 *frame_data_ptr;

  GMutex               shm_writer_mutex;
  gint                 shm_buffers;
  string               shm_writer_name;
  bool                 ld_enabled;

//this variable to store previous frame state if it's black or not and then wait for the next normal iframe to arrive 
//before sending normal frames.
  bool                 prevFrameState;

} cam_record_ctxt;

GMutex session_change_mutex;

static gchar log_tag[CAMERA_POSITION_MAX][GSTREAMER_NAME_LENGTH_MAX] = {0};

static cam_record_ctxt cam_rec_ctx[CAMERA_POSITION_MAX] = {0};

static const string CAMERA_CRASH_DB_TABLE = "CAMERA_CRASH_DB";

static bool streaming = true;
static const string TMP_KINESIS_STREAMING_OUTWARD = "/tmp/kinesis_streaming_out";
static const string TMP_KINESIS_STREAMING_INWARD = "/tmp/kinesis_streaming_in";
static bool kinesis_dual_streaming = false;
static int kinesis_req_cam_id = -1;
void livestream_framedrop_cb(int cam_num);
static volatile bool live_stream_force_stop_inward = false;

static void* gst_thread_func(void *ptr);

static bool first_after_boot = false;

static volatile bool cam_check_thread_created = false;
pthread_t cam_check;
pthread_t mode_check;

static volatile bool should_move_partial_files = false;
pthread_t check_dms_connection_th;

void *zmq_context;
void *zmq_publisher;

void *zmq_context_dms;
void *zmq_publisher_dms;

//a frame is changed to valid when it's the first iframe after continuous black frame sending in livestreaming
static const bool VALID_FRAME = false;
//a frame is changed to invalid when after black frames the next frames are not i-frames
static const bool INVALID_FRAME = true;
//kinesis
enum live_stream_event_t {
    START = 0,
    END = 1,
    MAX = 2
};

static int live_stream_duration = -1;
static const string audio_socket = "tcp://127.0.0.1:6385";

static const string default_live_stream_inward_start_file = "/home/ubuntu/autocam/audio/nd_debug2/live_streaming_inward_start.wav";
static const string default_live_stream_inward_end_file = "/home/ubuntu/autocam/audio/nd_debug2/live_streaming_inward_end.wav";

void *zmq_context_audio = NULL;
void *zmq_publisher_audio = NULL;
string live_stream_inward_start_file = "";
string live_stream_inward_end_file = "";
bool live_stream_audio_notification = false;

int32_t file_inward_fd;
int32_t file_left_fd;
int32_t file_right_fd;
int32_t file_inward_ld_fd;
int32_t file_dms_fd;
int32_t file_dms_ld_fd;
volatile static bool session_change_inward = false;
volatile static bool session_change_inward_ld = false;
volatile static bool session_change_left = false;
volatile static bool session_change_right = false;
volatile static bool session_change_dms = false;
volatile static bool session_change_dms_ld = false;

volatile static bool drop_inward_rt_frames = false;
volatile static bool drop_dms_rt_frames = false;

static const int DMS_IRLED_STATUS_CLEAR_WAIT_TIME_IN_SECS = 1; // 1 second sleep after DMS streaming starts to clear the IRLED status
static const int DMS_IRLED_STATUS_FAULTY_WAIT_TIME_IN_SECS = 2;
static const int DMS_LED_COLOR_WAIT_TIME_IN_SECS = 5; // Wait for DMS streaming to start for a maximum of 5 seconds to set the DMS LED color.
static const int DMS_LED_COLOR_SLEEP_TIME_IN_USECS = 100000; // 100 milliseconds
static const int DMS_HEALTH_INFO_INTERVAL_IN_SECS = 60; // 1 minute interval to send DMS health info to bagheera
static const int DMS_CONNECTION_MONITORING_THREAD_SLEEP_TIME_IN_SECS = 5; // To handle DMS connection after bootup, check every 5 seconds
static const unsigned char DMS_IRLED_MIN_VOLTAGE_THRESHOLD = 0x14;
static const unsigned char DMS_IRLED_MAX_VOLTAGE_THRESHOLD = 0x29;

// This mask is created based on request from HW team as mentioned in comments in DMS-109
#define DMS_IS_ANY_FAULT_BIT_SET_IN_REG1(val) ((val) & (0x80 | 0x10 | 0x02 | 0x01))
#define DMS_IS_ANY_FAULT_BIT_SET_IN_REG2(val) ((val) & (0x04))

#ifdef DMS_CAMERA_SUPPORTED
// socket specific declarations
#define SOCKET_PATH "/tmp/fd-share-bagheera.socket"
int sfd, cfd; // server and client fds
bool is_bagheera_connected = false;
void handle_bagheera_disconnection(int client_fd);
#endif

static const string CAMREC_FILE_BASE_PATH = "/home/iriscli/files/";

bool inward_privacy_status = false;
static const string black_frame_inward_LS = "/home/ubuntu/.nddevice/black_frame_inward_LS.avc";
binary_data_t *black_frame_ls = NULL;

#define ND_SOCKET_INI "/home/ubuntu/.nddevice/latest/nd_core_common.ini"

/* QR scanner specific declarations start*/
using nd::interface::IQRScanner;
using nd::device::QRScannerFactory;
// Flag to indicate if driver login QR scanning feature is enabled
static bool is_driverlogin_qr_enabled = false;
// Global flag to control QR scanning operation state
bool qr_scan_started = false;
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
static int qrscan_buffer_width = 1920;
// Height of the buffer used for QR scanning image data
static int qrscan_buffer_height = 1080;
// frame rate (in FPS) at which QR scanning is performed
static int qr_scan_fps = 1;
// Pointer to Y-plane data buffer used for QR code detection
uint8_t *y_planes = NULL;
// Maximum number of Y-plane buffers that can be queued for processing
static int max_queued_yplanes = 2;
// Flag to enable/disable dumping invalid QR scan image data to file for debugging
bool invalid_qr_image_dump_enabled = false;
// File path for storing invalid QR scan image raw data for debugging
static const string invalid_qr_image_raw_data_file = "/dev/shm/invalid_qr_image_raw_buffer.yuv";
static const string qr_scan_started_file = "/dev/shm/qr_scan_started_file.bin";

string qr_messenger_socket = "";
string qr_messenger_topic = "";
NDMessenger::ServerBuilder qr_scan_data_publisher;

static int QR_MIN_INTRA_FRAME_INTERVAL_NS;

typedef enum qr_scan_status {
  QR_SCAN_NO_QR,
  QR_SCAN_QR_DETECTED,
  QR_SCAN_QR_DECODED,
  QR_SCAN_TIMEDOUT
} qr_scan_status;
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
            qr_scan_started = false;
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

bool start_QR_scan(char tags[kQRScanMaxTagPatterns][kQRScanMaxTagPatternDataLen])
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
    qr_scan_started = true;

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

bool stop_QR_scan()
{
    qr_scan_started = false;

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

bool check_side_cam_enable_disable(std::string property_str, int i)
{
    std::vector<prop_data_t> crash_entries;
    if (get_all_property_entries_DB(property_str, crash_entries, db_handle_camera_crash, SIDE_CAM_CRASH_INFO_DB_TABLE)) {
        int num_entries = crash_entries.size();
        for (int idx = 0; idx < num_entries; ++idx) {
            LOG_I(TAG, "Entry %d: value=%s,montonic_time=%lld", idx, crash_entries[idx].value.c_str(), crash_entries[idx].monotonic_time);
        }
        if (num_entries >= max_cam_crash_count) {
            stringstream crash_file;
            crash_file << "/dev/shm/nd_files_c/cam" << i << "_crashfile";
            //checking the time difference between last and max_cam_crash_count-th last crash entry for a particular camera fetched from camera_crash_db
            //so if the camera crashes happened more than or equal to max_cam_crash_count times within the defined threshold time limit then we disale camera
            if ((crash_entries[num_entries - 1].monotonic_time - crash_entries[num_entries - max_cam_crash_count].monotonic_time) < CRASH_TIME_DIFF_THRESHOLD_MS) {
                LOG_I(TAG, "Disabling cam %d due to frequent crashes (within %lld ms)", i, CRASH_TIME_DIFF_THRESHOLD_MS);
                if (file_is_present(crash_file.str().c_str()) == false) {
                    LOG_E(TAG, "camera %d crashed for %d times; disabling camera", i, cam_crash_count[i]);
                    cam_record_service_obj->send_err_msg(SM_E_NDC_CAM_DISABLED, i, "Camera disabled");
                    file_touch(crash_file.str().c_str());
                }
                return true;
            }
            else {
                LOG_I(TAG, "Cam %d crash frequency is within acceptable limits", i);
                return false;
            }
        }
    } else {
        LOG_E(TAG, "Failed to get crash entries for cam %d (property: %s)", i, property_str.c_str());
        return false;
    }
}


static void init_live_streaming_audio_files()
{
    bool is_val_overridden = false;
    Config_parser c(BAGHEERACONFIG_INI);

    if (c.getParseStatus()) {
        if ((live_stream_inward_start_file = c.getConfig("live_streaming",
                        "inward_stream_start_file", "", true, is_val_overridden)) == "") {

            live_stream_inward_start_file.assign(default_live_stream_inward_start_file);
        }
        if ((live_stream_inward_end_file = c.getConfig("live_streaming",
                        "inward_stream_end_file", "", true, is_val_overridden)) == "") {

            live_stream_inward_end_file.assign(default_live_stream_inward_end_file);
        }
        if ("true" == c.getConfig("live_streaming",
                        "audio_notification", "false", true, is_val_overridden)) {
            live_stream_audio_notification = true;
        }
    }
    LOG_I(TAG, "Live streaming inward start: %s", live_stream_inward_start_file.c_str());
    LOG_I(TAG, "Live streaming inward end: %s", live_stream_inward_end_file.c_str());
    LOG_I(TAG, "Live streaming audio notification: %d", live_stream_audio_notification);
}

bool create_audio_socket()
{
    LOG_I(TAG, "Create audio ZMQ socket");
    if ((zmq_publisher_audio == NULL) && (zmq_context_audio == NULL))
    {
        zmq_context_audio = zmq_ctx_new ();
        if( zmq_context_audio == NULL ){
            LOG_E(TAG, "zmq_context_audio == NULL; errno: %d, zmq_error:   %s", errno, zmq_strerror(zmq_errno()));
            return false;
        }
        zmq_publisher_audio = zmq_socket (zmq_context_audio, ZMQ_PUB);
        if( zmq_publisher_audio == NULL ){
            LOG_E(TAG, "zmq_publisher_audio == NULL; errno: %d, zmq_error:   %s", errno, zmq_strerror(zmq_errno()));
            return false;
        }

        int max_q_sz = MAX_PUBLISHER_Q_SZ;
        if( zmq_setsockopt(zmq_publisher_audio, ZMQ_SNDHWM, &max_q_sz, sizeof(int)) ) {
            LOG_E(TAG, "zmq_setsockopt failed; errno: %d, zmq_error:   %s", errno, zmq_strerror(zmq_errno()));
            return false;
        }

        int trail_count = 0, rc = -1;
        do {
            trail_count++;
            usleep(100*1000); // sleep for 100MS before binding : can this avoid FAILED TO CREATE with err 98 ?
            rc = zmq_connect (zmq_publisher_audio, audio_socket.c_str());
            if(rc != 0) {
                LOG_E(TAG, "FAILED TO CREATE zmq_socket rc: %d , errno: %d,   zmq_error no:   %s",
                        rc, errno, zmq_strerror(zmq_errno()));
            }
            else {
                LOG_I(TAG, "SUCCESS IN CREATE zmq_socket");
                break;
            }
        } while( trail_count < 5 );

        if( rc != 0 ) {
            LOG_C(TAG, "after %d re trails FAILED TO CREATE zmq_socket rc: %d ", trail_count, rc);

            zmq_close (zmq_publisher_audio);
            zmq_ctx_destroy (zmq_context_audio);
            zmq_publisher_audio = NULL;
            zmq_context_audio = NULL;

            return false;
        }
    }
    LOG_I(TAG, "Created audio ZMQ socket for inward camera !!!!");
    return true;
}

static void remove_leading_trailing_spaces (string &s)
{
    // Return immediately if string is empty
    if(s == "") {
        return;
    }
    size_t start, end;

    start = s.find_first_not_of(" \t\n\v\r\f");
    end = s.find_last_not_of (" \t\n\v\r\f");
    s = s.substr (start, end+1);
    LOG_D (TAG, "remove_leading_trailing_spaces: %s", s.c_str());
}

static bool send_audio_play(live_stream_event_t event)
{
    LOG_I(TAG, "send audio play for inward camera");
    nd_audio::AudioData audio_data;
    nd_audio::AudioMessage audio_message;

    string session_name = "Live_streaming";
    string alert_type = "LiveSt";
    string event_code = "eventCode";
    string uuid = "uuid";
    uint64_t curr_time = (uint64_t)get_system_time();

    audio_data.set_session_name(session_name);
    audio_data.set_alert_type(alert_type);
    audio_data.set_event_code(event_code);
    audio_data.set_uuid(uuid);
    audio_data.set_frame_gen_time(curr_time);
    audio_data.set_issue_time(curr_time);
    audio_data.set_acceptable_latency(5000);
    audio_data.set_volume(100);
    audio_data.set_play_audio(true);

    string file_name = "";
    if (event == START) {
        file_name = live_stream_inward_start_file;
    } else if (event == END) {
        file_name = live_stream_inward_end_file;
    } else {
        LOG_E(TAG, "Incorrect event number: %d, FAILED to send audio message for inward camera", event);
        return false;
    }

    audio_data.set_audio_file_name(file_name);

    audio_message.set_allocated_audio_data(&audio_data);

    int zmq_result = -1;
    string audio_msg_str;
    audio_message.SerializeToString(&audio_msg_str);

    printf("%s\n", audio_msg_str.c_str());
    int n = audio_msg_str.length();

    // declaring character array
    char char_array[n + 1];

    // copying the contents of the string to char array
    strcpy(char_array, audio_msg_str.c_str());
    zmq_result = zmq_send(zmq_publisher_audio, char_array, (n+1), 0);
    if (zmq_result == -1) {
        LOG_E(TAG, "FAILED to send audio message for inward camera");
        return false;
    }

    audio_message.release_audio_data();
    return true;
}

bool stopkinesis (req_livestreaming_data_t *kinesis_req_msg)
{
    LOG_I("kinesis", "Enter stop kinesis for inward camera");
    //Deleting both files because stopkinesis will not be called for the camera which is disabled or privacy is enabled
    file_delete(TMP_KINESIS_STREAMING_INWARD);
    file_delete(TMP_KINESIS_STREAMING_OUTWARD);
    cam_record_ctxt *context = (cam_record_ctxt *)&cam_rec_ctx[CAMERA_POSITION_BACK];
    context->prevFrameState = VALID_FRAME;
    if (streaming == false)
    	streaming = true;

    // Stop streaming
    context->kinesis_stream_info.first_time = false;

    /* free the frame buffer */
    if(context->kinesis_stream_info.frame.frameData != NULL)
      free(context->kinesis_stream_info.frame.frameData);

    stopKinesisVideoStream(context->kinesis_stream_info.streamHandle);
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

    LOG_I("kinesis", "Exit stop kinesis for inward camera");
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

    string temp = bagheera_config->getConfig("camera","video_encryption", "true", get_override_val, is_val_overridden);
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

    string ld_enabled_str = "false";
    if (context->cam_pos == CAMERA_POSITION_BACK) {
        ld_enabled_str = bagheera_config->getConfig("camera","inward_ld_enabled", "true", get_override_val, is_val_overridden);
    } else if (context->cam_pos == CAMERA_POSITION_DMS) {
        ld_enabled_str = bagheera_config->getConfig("camera","dms_ld_enabled", "true", get_override_val, is_val_overridden);
    }
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

bool startkinesis(req_livestreaming_data_t *kinesis_req_msg)
{
    if (!g_live_streaming) {
        LOG_E("kinesis","LIVE stream is not enabled. Check config\n");
        return false;
    }

    int memfd = -1;
    unsigned char *decrypted_key_buf = nullptr;
    size_t decrypted_key_len = 0;
    bool dec_status = false;
    string priv_key_fd_path;
    int priv_key_fd = -1;

    cam_record_ctxt *context = (cam_record_ctxt *)&cam_rec_ctx[CAMERA_POSITION_BACK];
    string cloud_config_path = CLOUD_CONFIG_INI;
    string device_config_path = DEVICE_CONFIG_INI;
    string device_id, cloud_server;
    std::stringstream iot_thing_name_ss;
    string iot_thing_name;
    unsigned int ret = -1;
    STATUS retStatus = STATUS_SUCCESS;
    int attempt = 1;

    black_frame_ls = read_file(black_frame_inward_LS.c_str());
    LOG_I("kinesis", "black_frame_ls size for inward camera: %ld", black_frame_ls->size);
    LOG_I("kinesis", "Enter start kinesis for inward camera");

    Config_parser cloud_config_parser(cloud_config_path);
    Config_parser device_config_parser(device_config_path);
    if (cloud_config_parser.getParseStatus() && device_config_parser.getParseStatus()) {
        device_id = device_config_parser.getConfig("identity","deviceId","");
        if ( device_id == "" ) {
          device_id = device_config_parser.getConfig("identity","deviceid","");
        }

        cloud_server = cloud_config_parser.getConfig("cloud","server","prod");
        if ( cloud_server == "prod")
          cloud_server = "production";
        iot_thing_name_ss << cloud_server.c_str() << "-" << device_id.c_str();
        iot_thing_name = iot_thing_name_ss.str();
        nd_strncpy(context->kinesis_stream_info.iot_str, iot_thing_name.c_str(), sizeof(context->kinesis_stream_info.iot_str));
        LOG_I("*********** kinesis","#*#*#*#*#*# LIVE stream iot_thing_name = %s\n", context->kinesis_stream_info.iot_str);
    } else {
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

    LOG_I("kinesis","LIVE stream starting for kinesis_req_cam_id = %d", kinesis_req_cam_id);

    // Create default device info, default storage size is 128MB.
    CHK_STATUS(createDefaultDeviceInfo(&context->kinesis_stream_info.pDeviceInfo));

    context->kinesis_stream_info.pDeviceInfo->clientInfo.loggerLogLevel = LOG_LEVEL_DEBUG;
    context->kinesis_stream_info.pDeviceInfo->storageInfo.storageSize = DEFAULT_STORAGE_SIZE;

    LOG_I("kinesis", "createRealtimeVideoStreamInfoProvider");

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

    LOG_I("kinesis", "setStreamInfoBasedOnStorageSize");
    CHK_STATUS(setStreamInfoBasedOnStorageSize(DEFAULT_STORAGE_SIZE, live_streaming_param.bitrate, 1, context->kinesis_stream_info.pStreamInfo));

    /* Iot certificate validation */
/*  LOG_I("kinesis","LIVE stream createDefaultCallbacksProviderWithIotCertificate calling\n");
    LOG_I("kinesis","endpoint: %s", context->kinesis_stream_info.req_stream.endpoint);
    LOG_I("kinesis","IOT_CERT_PATH: %s", IOT_CERT_PATH);
    LOG_I("kinesis","IOT_CERT_PRIVATE_KEY_PATH: %s", IOT_CERT_PRIVATE_KEY_PATH);
    LOG_I("kinesis","CA_CERT_PATH: %s", CA_CERT_PATH);
    LOG_I("kinesis","IOT_ROLE_ALIAS: %s", IOT_ROLE_ALIAS);
    LOG_I("kinesis","iot_str: %s", context->kinesis_stream_info.iot_str);
    LOG_I("kinesis","DEFAULT_AWS_REGION: %s", DEFAULT_AWS_REGION);*/
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
        string keyData(reinterpret_cast<const char*>(decrypted_key_buf), decrypted_key_len);

        bool memfd_status = createMemfdFromBuffer(keyData, priv_key_fd, "cam_recorder_bagh_priv_key");

        if (memfd_status == false || priv_key_fd == -1) {
            cerr << "Failed to create in-memory file from decoded buffer" << endl;
            free(decrypted_key_buf);
            return false;
        }
        
        lseek(priv_key_fd, 0, SEEK_SET);
        std::ostringstream oss;
        oss << "/proc/self/fd/" << priv_key_fd;
        priv_key_fd_path = oss.str();
    } catch (const exception& ex) {
        LOG_E("kinesis", "memfd error: %s", ex.what());
        free(decrypted_key_buf);
        return false;
    }
    free(decrypted_key_buf);


    while (attempt <= 3) {
        ret = createDefaultCallbacksProviderWithIotCertificate(context->kinesis_stream_info.req_stream.endpoint, \
            IOT_CERT_PATH, (PCHAR)priv_key_fd_path.c_str(), CA_CERT_PATH, IOT_ROLE_ALIAS, context->kinesis_stream_info.iot_str, DEFAULT_AWS_REGION, NULL, NULL, &context->kinesis_stream_info.pClientCallbacks);
        if (STATUS_SUCCESS != ret) {
            LOG_E("kinesis","*** LIVE stream Failed to createDefaultCallbacksProviderWithIotCertificate err - %d", ret);
            if (attempt == 3) {
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

#ifdef DEBUG
  addFileLoggerPlatformCallbacksProvider(context->kinesis_stream_info.pClientCallbacks, FILE_LOGGING_BUFFER_SIZE, MAX_NUMBER_OF_LOG_FILES, \
                                                                 (PCHAR) QMMF_FILE_LOGGER_LOG_FILE_DIRECTORY_PATH, \
                                                                 TRUE);
#endif
    LOG_I("kinesis", "createStreamCallbacks calling");
    ret = createStreamCallbacks(&context->kinesis_stream_info.pStreamCallbacks);
    if(STATUS_SUCCESS != ret){
        LOG_E("kinesis","LIVE stream Failed createStreamCallbacks");
        kinesis_req_msg->error = LS_ERR_BAD_LTE;
        CHK(FALSE, ret);
    }

    LOG_I("kinesis", "addStreamCallbacks calling");
    ret = addStreamCallbacks(context->kinesis_stream_info.pClientCallbacks, context->kinesis_stream_info.pStreamCallbacks);
    if(STATUS_SUCCESS != ret){
        LOG_E("kinesis","LIVE stream Failed addStreamCallbacks");
        kinesis_req_msg->error = LS_ERR_BAD_LTE;
        CHK(FALSE, ret);
    }
    LOG_I("kinesis", "createKinesisVideoClient calling");
    /* Initializing and configuring KinesisVideoClient and KinesisVideoStream for the pipeline */
    ret = createKinesisVideoClient(context->kinesis_stream_info.pDeviceInfo, context->kinesis_stream_info.pClientCallbacks, &context->kinesis_stream_info.clientHandle);
    if(STATUS_SUCCESS != ret){
        LOG_E("kinesis","LIVE stream Failed createKinesisVideoClient");
        kinesis_req_msg->error = LS_ERR_BAD_LTE;
        CHK(FALSE, ret);
    }

    LOG_I("kinesis", "createKinesisVideoStreamSync calling");
    ret = createKinesisVideoStreamSync(context->kinesis_stream_info.clientHandle, context->kinesis_stream_info.pStreamInfo, &context->kinesis_stream_info.streamHandle);
    if(STATUS_SUCCESS != ret){
        LOG_E("kinesis","LIVE stream Failed createKinesisVideoStreamSync");
        kinesis_req_msg->error = LS_ERR_BAD_LTE;
        CHK(FALSE, ret);
    }

    /* allocate memory for frame */
    if (context->kinesis_stream_info.frame.frameData == NULL)
        context->kinesis_stream_info.frame.frameData = (BYTE *) calloc(sizeof(char), MAX_FRAME_BUFFER_SIZE);

    //Enable streaming
    context->kinesis_stream_info.first_time = true;
    streaming = false;
    // TODO: Touch file for inward camera stream in /tmp
    kinesis_dual_streaming = kinesis_req_msg->dual_streaming;
    if(kinesis_req_msg->dual_streaming){
        kinesis_dual_streaming = true;
        file_touch(TMP_KINESIS_STREAMING_INWARD);
        write_string_to_file(TMP_KINESIS_STREAMING_INWARD, "1");
    }
CleanUp:
    if (STATUS_FAILED(retStatus)) {
        if(kinesis_req_msg->error != LS_ERR_BAD_LTE){
            kinesis_req_msg->error = LS_ERR_UNKNOWN;
        }
        LOG_E("kinesis","Failed with status 0x%08x\n", retStatus);
        stopkinesis(kinesis_req_msg);
        return false;
    }

    LOG_I("kinesis", "Exit start kinesis for inward camera");
    return true;
}

static bool start_kinesis(req_livestreaming_data_t *start_kinesis_msg)
{
    if (false == startkinesis(start_kinesis_msg)) {
        return false;
    }

    LOG_I(TAG, "kinesis started for inward camera");

    if (live_stream_audio_notification){
        //If dual streaming is on and outward camera is not streaming, then play audio for inward camera
        if(!(start_kinesis_msg->dual_streaming && start_kinesis_msg->dual_streaming_active == 1)){
            send_audio_play(START);
        }
    }

    return true;
}

static bool stop_kinesis(req_livestreaming_data_t *stop_kinesis_msg)
{
    if (false == stopkinesis(stop_kinesis_msg)) {
        return false;
    }

    LOG_I(TAG, "kinesis stopped for inward camera");

    if (live_stream_audio_notification){
        //If dual streaming is on and outward camera is not streaming, then play audio for inward camera
        if(!(stop_kinesis_msg->dual_streaming && stop_kinesis_msg->dual_streaming_active == 1)){
            send_audio_play(END);
        }
    }

    if(black_frame_ls != NULL) {
        free(black_frame_ls);
    }

    return true;
}

static void livestreaming_cb(GstAppSink *object)
{
    // check if context camera position is matching with kinesis req; change camera
    unsigned int kinesis_ret = -1;
    cam_record_ctxt *context = (cam_record_ctxt *)&cam_rec_ctx[CAMERA_POSITION_BACK];
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

    if (!streaming) {
        if (kinesis_req_cam_id == context->cam_pos) {
            if (map.data != NULL) {
                if (context->kinesis_stream_info.frame.frameData == NULL)
                    context->kinesis_stream_info.frame.frameData = (BYTE *) calloc(sizeof(char), MAX_FRAME_BUFFER_SIZE);

                if (map.size > MAX_FRAME_BUFFER_SIZE) {
                    free(context->kinesis_stream_info.frame.frameData);
                    context->kinesis_stream_info.frame.frameData = (BYTE *) calloc(sizeof(char), map.size);
                }
                if (context->kinesis_stream_info.frame.frameData != NULL) {
                    if (inward_privacy_status) {
                        // we are sending black iframe whenever privacy is enabled
                        // but once privacy is disabled, we have to wait for the next key frame for the normal frame
                        // so prevFrameState helps us to check when do we need to wait.
                        context->prevFrameState = INVALID_FRAME;
                        memcpy((context->kinesis_stream_info.frame.frameData), (BYTE *)black_frame_ls->data, black_frame_ls->size);
                        context->kinesis_stream_info.frame.size = black_frame_ls->size;
                    }
                    else {
                        //privacy is disabled, so we can send the actual frame but still have to wauit for the next key frame 
                        //so till then we will send black frame
                        if (context->prevFrameState == INVALID_FRAME && GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT)) {
                            memcpy((context->kinesis_stream_info.frame.frameData), (BYTE *)black_frame_ls->data, black_frame_ls->size);
                            context->kinesis_stream_info.frame.size = black_frame_ls->size;
                        }
                        else {
                            //sending the actual frame
                            context->prevFrameState = VALID_FRAME;
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
                context->kinesis_stream_info.frame.index = 0;
                context->kinesis_stream_info.first_time = false;
            }

            bool delta = GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT);

            FRAME_FLAGS kinesis_video_flags = delta ? FRAME_FLAG_NONE : FRAME_FLAG_KEY_FRAME;

            if (CHECK_FRAME_FLAG_KEY_FRAME(kinesis_video_flags)) {
                // LOG_I(log_tag[context->cam_pos], ">>>>> Key frame is available\n");
                context->kinesis_stream_info.frame.flags = FRAME_FLAG_KEY_FRAME;
            } else {
                context->kinesis_stream_info.frame.flags = FRAME_FLAG_NONE;
            }

#if 0
            FILE *fptr = NULL;
            fptr = fopen("/home/iriscli/files/aws_kvs_0001.h264", "a+");
            if (fptr != NULL) {
                fwrite(context->kinesis_stream_info.frame.frameData, sizeof(char), map.size, fptr);
                fclose(fptr);
            }
            LOG_I(log_tag[context->cam_pos], "Calling putKinesisVideoFrame\n");
#endif
            context->kinesis_stream_info.frame.decodingTs = static_cast<UINT64>(buffer->dts) / DEFAULT_TIME_UNIT_IN_NANOS;
            context->kinesis_stream_info.frame.presentationTs = static_cast<UINT64>(buffer->pts) / DEFAULT_TIME_UNIT_IN_NANOS;
            // If dual_streaming is on and outward cam stream file is created, then start inward cam streaming
            if ((kinesis_dual_streaming && file_is_present(TMP_KINESIS_STREAMING_OUTWARD)) || !kinesis_dual_streaming) {
                kinesis_ret = putKinesisVideoFrame(context->kinesis_stream_info.streamHandle, &context->kinesis_stream_info.frame);
            }
            context->kinesis_stream_info.frame.index++;
        }
    } else {
        context->kinesis_stream_info.frameIndex = 0;
        context->kinesis_stream_info.frame.flags = FRAME_FLAG_NONE;
        context->kinesis_stream_info.first_time = false;
    }

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

    // Initial cam check interval is more to accomodate delay in starting the camera pipelines
    int cam_check_interval = INIT_CAM_CRASH_THREAD_INTERVAL_IN_SECS;
    while (1) {
        sleep(cam_check_interval);
        cam_check_interval = CAM_CRASH_THREAD_INTERVAL_IN_SECS;
        curr_time = g_get_monotonic_time ();

		for (int i=1; i < CAMERA_POSITION_MAX; i++) {
			if (cams_enabled[i]) {

                if (i == CAMERA_POSITION_DMS) {
                   // Skipping frame checking if DMS is not connected or initializing or within grace-window after late-connect/disconnect-reconnect case.
                    if (!is_dms_connected || is_dms_initializing || 
                        ((g_get_monotonic_time() - dms_init_complete_time_us) < DMS_RECONNECT_GRACE_WINDOW_US)) {
                                if(!is_dms_connected)
                                    LOG_I(log_tag[i], "Skipping cam check: DMS is not connected.");
                                else
                                    LOG_I(log_tag[i], "Skipping cam check: %s", is_dms_initializing ? "DMS is initializing." : "within grace-window after late-connect/disconnect-reconnect case.");
                        continue;
                    }
                }

                // Skipping frame checking if stop_recording is received
                if (cam_rec_ctx[i].stop_recording_received) {
                    continue;
                }

				g_mutex_lock(&cam_rec_ctx[i].cam_frame_mutex);
				if (cam_rec_ctx[i].cam_frame_count == 0) {
					LOG_E(log_tag[i], "No frames generated from camera, do error callback");
					cam_crash_status[i] = true;
				} else if (cam_rec_ctx[i].cam_frame_count != 0) {
					LOG_I(log_tag[i], "Frame count received: %d", cam_rec_ctx[i].cam_frame_count);

					// time diff is in micro seconds
					fps = (cam_rec_ctx[i].cam_frame_count * 1000 * 1000.0) / (curr_time - prev_time);
					if (fps < FPS_THRESHOLD_FOR_CAM_CRASH) {
						cam_crash_status[i] = true;
						LOG_I(log_tag[i], "cam is not giving frames, do error callback: count = %d, fps = %f", cam_rec_ctx[i].cam_frame_count, fps);
					}
					cam_rec_ctx[i].cam_frame_count = 0;
				}
				g_mutex_unlock(&cam_rec_ctx[i].cam_frame_mutex);
			}
		}

        for (int i = 1; i < CAMERA_POSITION_MAX; i++) {
            if ((cams_enabled[i] == true) && (cam_crash_status[i] == true)) {
                send_cam_crash_error_cb_to_bagheera(Q_NAME, cam_crash_status);
                camrec_service_exiting = true;
                sleep(1); //sleeping here for 1 sec to give enough time to zmq publisher to close the socket for graceful exit
                LOG_E(TAG, "exiting from cam_rec service with _exit(0) command on account of cam_crash_status: %d %d %d %d",
                            cam_crash_status[CAMERA_POSITION_BACK], cam_crash_status[CAMERA_POSITION_LEFT], cam_crash_status[CAMERA_POSITION_RIGHT], cam_crash_status[CAMERA_POSITION_DMS]);
                _exit(0);
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

void overrun_handler_queue_appsink (void *queue, gpointer user_data)
{
    cam_record_ctxt *context = (cam_record_ctxt *)(user_data);
    LOG_E(log_tag[context->cam_pos], "Overrun for appsink queue");
}

#ifdef DMS_CAMERA_SUPPORTED
static void appsink_driverfacing_dms_cb(GstAppSink *object, gpointer user_data)
{
    int dmabuf_fd = 0;
    cam_record_ctxt *context = (cam_record_ctxt *)user_data;

    // Create gstreamer resources
    GstAppSink *app_sink = (GstAppSink *)object;
    GstSample *sample = gst_app_sink_pull_sample(app_sink);
    GstBuffer *buffer = gst_sample_get_buffer(sample);
    GstMapInfo map;
    gst_buffer_map (buffer, &map, GST_MAP_READ);

    if (drop_dms_rt_frames == true) {
        LOG_E (log_tag[context->cam_pos], "Dropping DMS cam RT frames since analytics service has restarted.");
        // Clean up gstreamer resources
        gst_buffer_unmap(buffer, &map);
        gst_sample_unref(sample);
        return;
    }

    if (is_bagheera_connected ==  true) {
        if (camrec_service_exiting == true) {
            LOG_I(TAG, "since camrec service is in the process of exiting, closing the socket for graceful exit");
            close(cfd);
            close(sfd);
            unlink(SOCKET_PATH);
            is_bagheera_connected = false;
            // Clean up gstreamer resources
            gst_buffer_unmap(buffer, &map);
            gst_sample_unref(sample);
            return;
        } else {
            struct pollfd fds[1];
            fds[0].fd = cfd;
            fds[0].events = POLLOUT;
            // printf("Before poll, fds[0].revents: %d\n", fds[0].revents);
            int ret = poll(fds, 1, 0);  // Poll with zero timeout
            if (ret < 0) {
                LOG_E(log_tag[context->cam_pos], "poll api call failed with error: %s", strerror(errno));
                handle_bagheera_disconnection(cfd);
                // Clean up gstreamer resources
                gst_buffer_unmap(buffer, &map);
                gst_sample_unref(sample);
                return;
            }
            //  printf("After poll, fds[0].revents: %d\n", fds[0].revents);

            if (fds[0].revents & POLLERR) {
                // Error on the socket, such as client crash or unexpected disconnection
                LOG_E(log_tag[context->cam_pos], "Error on client socket fd %d", cfd);
                handle_bagheera_disconnection(cfd);
                // Clean up gstreamer resources
                gst_buffer_unmap(buffer, &map);
                gst_sample_unref(sample);
                return;
            } else if (fds[0].revents & POLLHUP) {
                // The file descriptor has been closed (hung up)
                LOG_E(log_tag[context->cam_pos], "client socket fd %d has been hung up (closed)", cfd);
                handle_bagheera_disconnection(cfd);
                // Clean up gstreamer resources
                gst_buffer_unmap(buffer, &map);
                gst_sample_unref(sample);
                return;
            }
            ExtractFdFromNvBuffer((void *)map.data, &dmabuf_fd);

            dms_rt_metadata_t dms_rt_meta;
            dms_rt_meta.pts = GST_BUFFER_PTS(buffer);
            NvBufferGetParamsEx(dmabuf_fd, &(dms_rt_meta.paramsEx));

            struct iovec io = {
                .iov_base = &dms_rt_meta,
                .iov_len = sizeof(dms_rt_metadata_t)
            };
            // Control message buffer for sending file descriptor
            char control_buf[CMSG_SPACE(sizeof(int))];
            memset(control_buf, '\0', sizeof(control_buf));
            struct msghdr msg = {0};
            msg.msg_iov = &io;
            msg.msg_iovlen = 1;
            msg.msg_control = control_buf;
            msg.msg_controllen = sizeof(control_buf);

            // Set up the control message header to send file descriptor
            struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
            cmsg->cmsg_level = SOL_SOCKET;
            cmsg->cmsg_type = SCM_RIGHTS;
            cmsg->cmsg_len = CMSG_LEN(sizeof(int));
            memcpy((int *) CMSG_DATA(cmsg), &dmabuf_fd, sizeof(int));

            int bytes_sent = sendmsg(cfd, &msg, 0);
            if (bytes_sent < 0) {
                LOG_E(log_tag[context->cam_pos], "sendmsg api call failed with error: %s", strerror(errno));
                handle_bagheera_disconnection(cfd);
                // Clean up gstreamer resources
                gst_buffer_unmap(buffer, &map);
                gst_sample_unref(sample);
                return;
            }
            //printf("sendmsg: bytes_sent = %d\n", bytes_sent);
        }
    } else {
        LOG_I(log_tag[context->cam_pos], "Dropping since bagheera service is not connected through socket");
    }

    // Clean up gstreamer resources
    gst_buffer_unmap(buffer, &map);
    gst_sample_unref(sample);

    return;
}
#endif

static void appsink_driverfacing_cb(GstAppSink *object, gpointer user_data)
{
    cam_record_ctxt *context = (cam_record_ctxt *)user_data;

    GstAppSink* app_sink = (GstAppSink*) object;
    GstSample *sample = gst_app_sink_pull_sample(app_sink);
    GstBuffer *buffer = gst_sample_get_buffer(sample);
    GstMapInfo map;
    gst_buffer_map (buffer, &map, GST_MAP_READ);

    if (drop_inward_rt_frames == true) {
        LOG_E (log_tag[context->cam_pos], "Dropping inward cam RT frames because shared memory will be recreated.");
        gst_sample_unref(sample);
        gst_buffer_unmap(buffer, &map);
        return;
    }

    LOG_D(log_tag[context->cam_pos], "SHM_DEBUG_INWARD :: inwardcam_yuvbuf_size = %d, mapped_buffer_size = %d", context->yuvbuf_size_rt, map.size);
    if (map.size == context->yuvbuf_size_rt) {
        int smb_id = -1;
        int64_t uid = -1;
        int64_t data_len = -1;
        bool bIsExit = false;
        g_mutex_lock(&context->shm_writer_mutex);
        if (context->shm_writer) {
            context->frame_data_ptr = map.data;
            inward_rt_buf_size = map.size;
            if (!context->shm_writer->get_free_smb(context, smb_id, uid, data_len, context->cam_pos, bIsExit)) {
                LOG_E(log_tag[context->cam_pos], "failed to write data to smb, dropping frame");

                /* commenting the following lines as there is a possibility of replenishing
                 * the shared memory buffer once the analytics service restarts.
                 */
#if 0
                if (bIsExit) {
                    cam_record_service_obj->send_err_msg(SM_E_NDC_CAM_SHM_FAIL, CAMERA_POSITION_BACK, "Camera SHM fail");

                    if (zmq_publisher != NULL)
                        zmq_close (zmq_publisher);

                    if (zmq_context != NULL)
                        zmq_ctx_destroy (zmq_context);

                    zmq_publisher = NULL;
                    zmq_context = NULL;
                    LOG_E(TAG, "exiting from cam_rec with _exit(0) command on account of shared memory specific error");
                    _exit(0);
                }
#endif
            } else {
                if (camrec_service_exiting == true) {
                    LOG_I(TAG, "since camrec service is in the process of exiting, closing the zmq publisher for graceful exit");

                    if (zmq_publisher != NULL)
                        zmq_close (zmq_publisher);

                    if (zmq_context != NULL)
                        zmq_ctx_destroy (zmq_context);

                    zmq_publisher = NULL;
                    zmq_context = NULL;
                } else {
                    LOG_D(log_tag[context->cam_pos], "SHM_DEBUG :: smbid = %d", smb_id);

                    rt_metadata_t inward_rt_meta;
                    inward_rt_meta.pts = GST_BUFFER_PTS(buffer);
                    inward_rt_meta.uid = uid;
                    inward_rt_meta.smb_id = smb_id;

                    if (zmq_publisher != NULL)
                        zmq_send(zmq_publisher, (char *)&inward_rt_meta, sizeof(inward_rt_meta), 0);
                }
            }
        } else {
            LOG_E(log_tag[context->cam_pos], "shm writer null, so not triggering shm write callback");
        }
        g_mutex_unlock(&context->shm_writer_mutex);
    } else {
        LOG_E(log_tag[context->cam_pos], "Data buffer received inaccurate.");
    }
    gst_buffer_unmap(buffer, &map);
    gst_sample_unref(sample);

    return;
}

static void appsink_inward_ld_cb(GstElement *appsink, gpointer user_data)
{
    static bool first_cb_ld = true;

    cam_record_ctxt *context = (cam_record_ctxt *)user_data;
    GstAppSink* app_sink = (GstAppSink*) appsink;
    GstSample *sample = gst_app_sink_pull_sample(app_sink);
    if (!sample) {
        LOG_E(log_tag[context->cam_pos], "appsink_inward_ld_cb: gst_app_sink_pull_sample returned NULL");
        return;
    }
    GstBuffer *buffer = gst_sample_get_buffer(sample);
    if (!buffer) {
        LOG_E(log_tag[context->cam_pos], "appsink_inward_ld_cb: gst_sample_get_buffer returned NULL");
        gst_sample_unref(sample);
        return;
    }
    GstMapInfo map;
    if (!gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        LOG_E(log_tag[context->cam_pos], "appsink_inward_ld_cb: gst_buffer_map failed");
        gst_sample_unref(sample);
        return;
    }

    //Proceed further only if both PTS and DTS are valid
    if (!((GST_BUFFER_PTS_IS_VALID(buffer)) && (GST_BUFFER_DTS_IS_VALID(buffer)))) {
        LOG_E(log_tag[context->cam_pos], "appsink_inward_ld_cb: PTS or DTS not valid. pts_ld(%lld) dts_ld(%lld)", GST_BUFFER_PTS(buffer), GST_BUFFER_DTS(buffer));
        gst_buffer_unmap(buffer, &map);
        gst_sample_unref(sample);
        return;
    }

    if (first_cb_ld || (session_change_inward_ld && !GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT))) {
        string cam_session_fname = to_string(context->cam_pos) + string(session_filename);
        string fname = CAMREC_FILE_BASE_PATH + cam_session_fname + ld_extn;
        gchar filename[GSTREAMER_NAME_LENGTH_MAX] = {0};
        nd_strncpy(filename, fname.c_str(), sizeof(filename));

        int index = cam_session_fname.find(".mp4");
        cam_session_fname = cam_session_fname.substr(0, index);

        if (first_cb_ld) {
            first_cb_ld = false;
            session_change_inward_ld = false;
        } else {
            session_change_inward_ld = false;
            g_mutex_lock(&session_change_mutex);
            context->session_end_epoch_ld = get_system_time();
            LOG_I(log_tag[context->cam_pos], "DEBUG_ANNOTATION: Inward LD record duration: %lld", (context->session_end_epoch_ld - context->session_start_epoch_ld));
            LOG_I(log_tag[context->cam_pos], "send_session_end_msg_to_bagheera with session_fname: %s", context->cur_file_name_ld);
            send_session_end_msg_to_bagheera(Q_NAME, context->cur_file_name_ld, context->cam_pos, context->session_end_epoch_ld, context->session_start_pts_ld, context->session_frame_count, true);
            g_mutex_unlock(&session_change_mutex);
        }

        if (file_inward_ld_fd > 0) {
            close(file_inward_ld_fd);
            file_inward_ld_fd = -1;
            LOG_I(log_tag[context->cam_pos], "file_inward_ld_fd closed");
        }
        file_inward_ld_fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (file_inward_ld_fd <= 0) {
            gst_buffer_unmap(buffer, &map);
            gst_sample_unref(sample);
            LOG_E(log_tag[context->cam_pos], "appsink_inward_ld_cb: File open failed! %s", filename);
            return;
        }
        memset(context->cur_file_name_ld, 0x0, GSTREAMER_NAME_LENGTH_MAX);
        nd_strncpy(context->cur_file_name_ld, cam_session_fname.c_str(), sizeof(context->cur_file_name_ld));

        g_mutex_lock(&session_change_mutex);
        LOG_I(log_tag[context->cam_pos], "DEBUG_ANNOTATION: Filename inward LD: %s", filename);
        context->session_start_epoch_ld = get_system_time();
        context->session_start_pts_ld = GST_BUFFER_PTS(buffer);
        LOG_I(log_tag[context->cam_pos], "DEBUG_ANNOTATION: session_start_pts_ld: %lld, session_start_epoch_ld: %lld", context->session_start_pts_ld, context->session_start_epoch_ld);
        LOG_I(log_tag[context->cam_pos], "appsink_inward_ld_cb: send_session_start_msg_to_bagheera with session_fname: %s", context->cur_file_name_ld);
        send_session_start_msg_to_bagheera(Q_NAME, context->cur_file_name_ld, context->cam_pos, context->session_start_epoch_ld, context->session_start_pts_ld, true);
        g_mutex_unlock(&session_change_mutex);
    }

    if (stream_encryption) {
        unsigned char* encrypted_buffer = NULL;
        size_t encrypted_buffer_len = 0;
        size_t file_len1 = nd_stream_encryption(map.data, map.size, &encrypted_buffer, &encrypted_buffer_len);
        LOG_D(log_tag[context->cam_pos], "%s:%d nd_stream_encryption return val: %zu", __func__, __LINE__, file_len1);

        if (encrypted_buffer) {
            write(file_inward_ld_fd, encrypted_buffer, encrypted_buffer_len);
            free(encrypted_buffer);
            LOG_D(log_tag[context->cam_pos], "%s:%d Freeing the encrypted buffer memory", __func__, __LINE__);
        }
    } else {
        write(file_inward_ld_fd, map.data, map.size);
    }

    gst_buffer_unmap(buffer, &map);
    gst_sample_unref(sample);
}

static void appsink_right_cb(GstElement *appsink, gpointer user_data)
{
    static bool first_cb = true;

    cam_record_ctxt *context = (cam_record_ctxt *)user_data;
    GstAppSink* app_sink = (GstAppSink*) appsink;
    GstSample *sample = gst_app_sink_pull_sample(app_sink);
    if (!sample) {
        LOG_E(log_tag[context->cam_pos], "appsink_right_cb: gst_app_sink_pull_sample returned NULL");
        return;
    }
    GstBuffer *buffer = gst_sample_get_buffer(sample);
    if (!buffer) {
        LOG_E(log_tag[context->cam_pos], "appsink_right_cb: gst_sample_get_buffer returned NULL");
        gst_sample_unref(sample);
        return;
    }
    GstMapInfo map;
    if (!gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        LOG_E(log_tag[context->cam_pos], "appsink_right_cb: gst_buffer_map failed");
        gst_sample_unref(sample);
        return;
    }

    //Proceed further only if both PTS and DTS are valid
    if (!((GST_BUFFER_PTS_IS_VALID(buffer)) && (GST_BUFFER_DTS_IS_VALID(buffer)))) {
        LOG_E(log_tag[context->cam_pos], "appsink_right_cb: PTS or DTS not valid. pts_ld(%lld) dts_ld(%lld)", GST_BUFFER_PTS(buffer), GST_BUFFER_DTS(buffer));
        gst_buffer_unmap(buffer, &map);
        gst_sample_unref(sample);
        return;
    }

    if (first_cb || (session_change_right && !GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT))) {
        string cam_session_fname = to_string(context->cam_pos) + string(session_filename);
        string fname = CAMREC_FILE_BASE_PATH + cam_session_fname;
        gchar filename[GSTREAMER_NAME_LENGTH_MAX] = {0};
        nd_strncpy(filename, fname.c_str(), sizeof(filename));

        int index = cam_session_fname.find(".mp4");
        cam_session_fname = cam_session_fname.substr(0, index);

        if (first_cb) {
            first_cb = false;
            session_change_right = false;
        } else {
            session_change_right = false;
            g_mutex_lock(&session_change_mutex);
            context->session_end_epoch = get_system_time();
            LOG_I(log_tag[context->cam_pos], "send_session_end_msg_to_bagheera with session_fname: %s", context->cur_file_name);
            send_session_end_msg_to_bagheera(Q_NAME, context->cur_file_name, context->cam_pos, context->session_end_epoch, context->session_start_pts, context->session_frame_count);
            context->session_frame_count = 0;
            g_mutex_unlock(&session_change_mutex);
        }

        if (file_right_fd > 0) {
            close(file_right_fd);
            file_right_fd = -1;
            LOG_I(log_tag[context->cam_pos], "file_right_fd closed");
        }
        file_right_fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (file_right_fd <= 0) {
            gst_buffer_unmap(buffer, &map);
            gst_sample_unref(sample);
            LOG_E(log_tag[context->cam_pos], "appsink_right_cb: File open failed! %s", filename);
            return;
        }
        memset(context->cur_file_name, 0x0, GSTREAMER_NAME_LENGTH_MAX);
        nd_strncpy(context->cur_file_name, cam_session_fname.c_str(), sizeof(context->cur_file_name));

        g_mutex_lock(&session_change_mutex);
        LOG_I(log_tag[context->cam_pos], "DEBUG_ANNOTATION: Filename right: %s", filename);
        context->session_start_epoch = get_system_time();
        context->session_start_pts = GST_BUFFER_PTS(buffer);
        LOG_I (log_tag[context->cam_pos], "DEBUG_ANNOTATION: session_start_pts = %lld, session_start_epoch: %lld", context->session_start_pts, context->session_start_epoch);
        LOG_I(log_tag[context->cam_pos], "send_session_start_msg_to_bagheera with session_fname: %s", context->cur_file_name);
        send_session_start_msg_to_bagheera(Q_NAME, context->cur_file_name, context->cam_pos, context->session_start_epoch, context->session_start_pts, false);
        g_mutex_unlock(&session_change_mutex);
    }

    if (stream_encryption) {
        unsigned char* encrypted_buffer = NULL;
        size_t encrypted_buffer_len = 0;
        size_t file_len1 = nd_stream_encryption(map.data, map.size, &encrypted_buffer, &encrypted_buffer_len);
        LOG_D(log_tag[context->cam_pos], "%s:%d nd_stream_encryption return val: %zu", __func__, __LINE__, file_len1);

        if (encrypted_buffer) {
            write(file_right_fd, encrypted_buffer, encrypted_buffer_len);
            free(encrypted_buffer);
            LOG_D(log_tag[context->cam_pos], "%s:%d Freeing the encrypted buffer memory", __func__, __LINE__);
        }
    } else {
        write(file_right_fd, map.data, map.size);
    }

    gst_buffer_unmap(buffer, &map);
    gst_sample_unref(sample);
}

static void appsink_left_cb(GstElement *appsink, gpointer user_data)
{
    static bool first_cb = true;

    cam_record_ctxt *context = (cam_record_ctxt *)user_data;
    GstAppSink* app_sink = (GstAppSink*) appsink;
    GstSample *sample = gst_app_sink_pull_sample(app_sink);
    if (!sample) {
        LOG_E(log_tag[context->cam_pos], "appsink_left_cb: gst_app_sink_pull_sample returned NULL");
        return;
    }
    GstBuffer *buffer = gst_sample_get_buffer(sample);
    if (!buffer) {
        LOG_E(log_tag[context->cam_pos], "appsink_left_cb: gst_sample_get_buffer returned NULL");
        gst_sample_unref(sample);
        return;
    }
    GstMapInfo map;
    if (!gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        LOG_E(log_tag[context->cam_pos], "appsink_left_cb: gst_buffer_map failed");
        gst_sample_unref(sample);
        return;
    }

    //Proceed further only if both PTS and DTS are valid
    if (!((GST_BUFFER_PTS_IS_VALID(buffer)) && (GST_BUFFER_DTS_IS_VALID(buffer)))) {
        LOG_E(log_tag[context->cam_pos], "appsink_left_cb: PTS or DTS not valid. pts_ld(%lld) dts_ld(%lld)", GST_BUFFER_PTS(buffer), GST_BUFFER_DTS(buffer));
        gst_buffer_unmap(buffer, &map);
        gst_sample_unref(sample);
        return;
    }

    if (first_cb || (session_change_left && !GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT))) {
        string cam_session_fname = to_string(context->cam_pos) + string(session_filename);
        string fname = CAMREC_FILE_BASE_PATH + cam_session_fname;
        gchar filename[GSTREAMER_NAME_LENGTH_MAX] = {0};
        nd_strncpy(filename, fname.c_str(), sizeof(filename));

        int index = cam_session_fname.find(".mp4");
        cam_session_fname = cam_session_fname.substr(0, index);

        if (first_cb) {
            first_cb = false;
            session_change_left = false;
        } else {
            session_change_left = false;
            g_mutex_lock(&session_change_mutex);
            context->session_end_epoch = get_system_time();
            LOG_I(log_tag[context->cam_pos], "send_session_end_msg_to_bagheera with session_fname: %s", context->cur_file_name);
            send_session_end_msg_to_bagheera(Q_NAME, context->cur_file_name, context->cam_pos, context->session_end_epoch, context->session_start_pts, context->session_frame_count);
            context->session_frame_count = 0;
            g_mutex_unlock(&session_change_mutex);
        }

        if (file_left_fd > 0) {
            close(file_left_fd);
            file_left_fd = -1;
            LOG_I(log_tag[context->cam_pos], "file_left_fd closed");
        }
        file_left_fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (file_left_fd <= 0) {
            gst_buffer_unmap(buffer, &map);
            gst_sample_unref(sample);
            LOG_E(log_tag[context->cam_pos], "appsink_left_cb: File open failed! %s", filename);
            return;
        }
        memset(context->cur_file_name, 0x0, GSTREAMER_NAME_LENGTH_MAX);
        nd_strncpy(context->cur_file_name, cam_session_fname.c_str(), sizeof(context->cur_file_name));

        g_mutex_lock(&session_change_mutex);
        LOG_I(log_tag[context->cam_pos], "DEBUG_ANNOTATION: Filename left: %s", filename);
        context->session_start_epoch = get_system_time();
        context->session_start_pts = GST_BUFFER_PTS(buffer);
        LOG_I (log_tag[context->cam_pos], "DEBUG_ANNOTATION: session_start_pts = %lld, session_start_epoch: %lld", context->session_start_pts, context->session_start_epoch);
        LOG_I(log_tag[context->cam_pos], "send_session_start_msg_to_bagheera with session_fname: %s", context->cur_file_name);
        send_session_start_msg_to_bagheera(Q_NAME, context->cur_file_name, context->cam_pos, context->session_start_epoch, context->session_start_pts, false);
        g_mutex_unlock(&session_change_mutex);
    }

    if (stream_encryption) {
        unsigned char* encrypted_buffer = NULL;
        size_t encrypted_buffer_len = 0;
        size_t file_len1 = nd_stream_encryption(map.data, map.size, &encrypted_buffer, &encrypted_buffer_len);
        LOG_D(log_tag[context->cam_pos], "%s:%d nd_stream_encryption return val: %zu", __func__, __LINE__, file_len1);

        if (encrypted_buffer) {
            write(file_left_fd, encrypted_buffer, encrypted_buffer_len);
            free(encrypted_buffer);
            LOG_D(log_tag[context->cam_pos], "%s:%d Freeing the encrypted buffer memory", __func__, __LINE__);
        }
    } else {
        write(file_left_fd, map.data, map.size);
    }

    gst_buffer_unmap(buffer, &map);
    gst_sample_unref(sample);
}

static void appsink_inward_cb(GstElement *appsink, gpointer user_data)
{
    static bool first_cb = true;

    cam_record_ctxt *context = (cam_record_ctxt *)user_data;
    GstAppSink* app_sink = (GstAppSink*) appsink;
    GstSample *sample = gst_app_sink_pull_sample(app_sink);
    if (!sample) {
        LOG_E(log_tag[context->cam_pos], "appsink_inward_cb: gst_app_sink_pull_sample returned NULL");
        return;
    }
    GstBuffer *buffer = gst_sample_get_buffer(sample);
    if (!buffer) {
        LOG_E(log_tag[context->cam_pos], "appsink_inward_cb: gst_sample_get_buffer returned NULL");
        gst_sample_unref(sample);
        return;
    }
    GstMapInfo map;
    if (!gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        LOG_E(log_tag[context->cam_pos], "appsink_inward_cb: gst_buffer_map failed");
        gst_sample_unref(sample);
        return;
    }

    //Proceed further only if both PTS and DTS are valid
    if (!((GST_BUFFER_PTS_IS_VALID(buffer)) && (GST_BUFFER_DTS_IS_VALID(buffer)))) {
        LOG_E(log_tag[context->cam_pos], "appsink_inward_cb: PTS or DTS not valid. pts_ld(%lld) dts_ld(%lld)", GST_BUFFER_PTS(buffer), GST_BUFFER_DTS(buffer));
        gst_buffer_unmap(buffer, &map);
        gst_sample_unref(sample);
        return;
    }

    if (first_cb || (session_change_inward && !GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT))) {
        string cam_session_fname = to_string(context->cam_pos) + string(session_filename);
        string fname = CAMREC_FILE_BASE_PATH + cam_session_fname;
        gchar filename[GSTREAMER_NAME_LENGTH_MAX] = {0};
        nd_strncpy(filename, fname.c_str(), sizeof(filename));

        int index = cam_session_fname.find(".mp4");
        cam_session_fname = cam_session_fname.substr(0, index);

        if (first_cb) {
            first_cb = false;
            session_change_inward = false;
        } else {
            session_change_inward = false;
            g_mutex_lock(&session_change_mutex);
            context->session_end_epoch = get_system_time();
            LOG_I(log_tag[context->cam_pos], "DEBUG_ANNOTATION: record duration: %lld", (context->session_end_epoch - context->session_start_epoch));
            LOG_I(log_tag[context->cam_pos], "send_session_end_msg_to_bagheera with session_fname: %s", context->cur_file_name);
            send_session_end_msg_to_bagheera(Q_NAME, context->cur_file_name, context->cam_pos, context->session_end_epoch, context->session_start_pts, context->session_frame_count);
            context->session_frame_count = 0;
            session_change_inward_ld = true; // To make sure session end msg goes for LD, only after HD session ends
            g_mutex_unlock(&session_change_mutex);
        }

        if (file_inward_fd > 0) {
            close(file_inward_fd);
            file_inward_fd = -1;
            LOG_I(log_tag[context->cam_pos], "file_inward_fd closed");
        }
        file_inward_fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (file_inward_fd <= 0) {
            gst_buffer_unmap(buffer, &map);
            gst_sample_unref(sample);
            LOG_E(log_tag[context->cam_pos], "appsink_inward_cb: File open failed! %s", filename);
            return;
        }
        memset(context->cur_file_name, 0x0, GSTREAMER_NAME_LENGTH_MAX);
        nd_strncpy(context->cur_file_name, cam_session_fname.c_str(), sizeof(context->cur_file_name));

        g_mutex_lock(&session_change_mutex);
        LOG_I(log_tag[context->cam_pos], "DEBUG_ANNOTATION: Filename inward HD: %s", filename);
        context->session_start_epoch = get_system_time();
        context->session_start_pts = GST_BUFFER_PTS(buffer);
        LOG_I (log_tag[context->cam_pos], "DEBUG_ANNOTATION: session_start_pts = %lld, session_start_epoch: %lld", context->session_start_pts, context->session_start_epoch);
        LOG_I(log_tag[context->cam_pos], "send_session_start_msg_to_bagheera with session_fname: %s", context->cur_file_name);
        send_session_start_msg_to_bagheera(Q_NAME, context->cur_file_name, context->cam_pos, context->session_start_epoch, context->session_start_pts, false);
        g_mutex_unlock(&session_change_mutex);
    }

    if (stream_encryption) {
        unsigned char* encrypted_buffer = NULL;
        size_t encrypted_buffer_len = 0;
        size_t file_len1 = nd_stream_encryption(map.data, map.size, &encrypted_buffer, &encrypted_buffer_len);
        LOG_D(log_tag[context->cam_pos], "%s:%d nd_stream_encryption return val: %zu", __func__, __LINE__, file_len1);

        if (encrypted_buffer) {
            write(file_inward_fd, encrypted_buffer, encrypted_buffer_len);
            free(encrypted_buffer);
            LOG_D(log_tag[context->cam_pos], "%s:%d Freeing the encrypted buffer memory", __func__, __LINE__);
        }
    } else {
        write(file_inward_fd, map.data, map.size);
    }

    gst_buffer_unmap(buffer, &map);
    gst_sample_unref(sample);
}

static void appsink_dms_cb(GstElement *appsink, gpointer user_data)
{
    static bool first_cb = true;

    cam_record_ctxt *context = (cam_record_ctxt *)user_data;
    GstAppSink* app_sink = (GstAppSink*) appsink;
    GstSample *sample = gst_app_sink_pull_sample(app_sink);
    if (!sample) {
        LOG_E(log_tag[context->cam_pos], "appsink_dms_cb: gst_app_sink_pull_sample returned NULL");
        return;
    }
    GstBuffer *buffer = gst_sample_get_buffer(sample);
    if (!buffer) {
        LOG_E(log_tag[context->cam_pos], "appsink_dms_cb: gst_sample_get_buffer returned NULL");
        gst_sample_unref(sample);
        return;
    }
    GstMapInfo map;
    if (!gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        LOG_E(log_tag[context->cam_pos], "appsink_dms_cb: gst_buffer_map failed");
        gst_sample_unref(sample);
        return;
    }

    //Proceed further only if both PTS and DTS are valid
    if (!((GST_BUFFER_PTS_IS_VALID(buffer)) && (GST_BUFFER_DTS_IS_VALID(buffer)))) {
        LOG_E(log_tag[context->cam_pos], "appsink_dms_cb: PTS or DTS not valid. pts_ld(%lld) dts_ld(%lld)", GST_BUFFER_PTS(buffer), GST_BUFFER_DTS(buffer));
        gst_buffer_unmap(buffer, &map);
        gst_sample_unref(sample);
        return;
    }

    if (first_cb || (session_change_dms && !GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT))) {
        string cam_session_fname = to_string(context->cam_pos) + string(session_filename);
        string fname = CAMREC_FILE_BASE_PATH + cam_session_fname;
        gchar filename[GSTREAMER_NAME_LENGTH_MAX] = {0};
        nd_strncpy(filename, fname.c_str(), sizeof(filename));

        int index = cam_session_fname.find(".mp4");
        cam_session_fname = cam_session_fname.substr(0, index);

        if (first_cb) {
            first_cb = false;
            session_change_dms = false;
        } else {
            session_change_dms = false;
            g_mutex_lock(&session_change_mutex);
            context->session_end_epoch = get_system_time();
            LOG_I(log_tag[context->cam_pos], "DEBUG_ANNOTATION: record duration: %lld", (context->session_end_epoch - context->session_start_epoch));
            LOG_I(log_tag[context->cam_pos], "send_session_end_msg_to_bagheera with session_fname: %s", context->cur_file_name);
            send_session_end_msg_to_bagheera(Q_NAME, context->cur_file_name, context->cam_pos, context->session_end_epoch, context->session_start_pts, context->session_frame_count);
            context->session_frame_count = 0;
            session_change_dms_ld = true; // To make sure session end msg goes for LD, only after HD session ends
            g_mutex_unlock(&session_change_mutex);
        }

        if (file_dms_fd > 0) {
            close(file_dms_fd);
            file_dms_fd = -1;
            LOG_I(log_tag[context->cam_pos], "file_dms_fd closed");
        }
        file_dms_fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (file_dms_fd <= 0) {
            gst_buffer_unmap(buffer, &map);
            gst_sample_unref(sample);
            LOG_E(log_tag[context->cam_pos], "appsink_dms_cb: File open failed! %s", filename);
            return;
        }
        memset(context->cur_file_name, 0x0, GSTREAMER_NAME_LENGTH_MAX);
        nd_strncpy(context->cur_file_name, cam_session_fname.c_str(), sizeof(context->cur_file_name));

        g_mutex_lock(&session_change_mutex);
        LOG_I(log_tag[context->cam_pos], "DEBUG_ANNOTATION: Filename dms HD: %s", filename);
        context->session_start_epoch = get_system_time();
        context->session_start_pts = GST_BUFFER_PTS(buffer);
        LOG_I (log_tag[context->cam_pos], "DEBUG_ANNOTATION: session_start_pts = %lld, session_start_epoch: %lld", context->session_start_pts, context->session_start_epoch);
        LOG_I(log_tag[context->cam_pos], "send_session_start_msg_to_bagheera with session_fname: %s", context->cur_file_name);
        send_session_start_msg_to_bagheera(Q_NAME, context->cur_file_name, context->cam_pos, context->session_start_epoch, context->session_start_pts, false);
        g_mutex_unlock(&session_change_mutex);
    }

    if (stream_encryption){
        unsigned char* encrypted_buffer = NULL;
        size_t encrypted_buffer_len = 0;
        size_t file_len1 = nd_stream_encryption(map.data, map.size, &encrypted_buffer, &encrypted_buffer_len);
        LOG_D(log_tag[context->cam_pos], "%s:%d nd_stream_encryption return val: %zu", __func__, __LINE__, file_len1);

        if (encrypted_buffer) {
            write(file_dms_fd, encrypted_buffer, encrypted_buffer_len);
            free(encrypted_buffer);
            LOG_D(log_tag[context->cam_pos], "%s:%d Freeing the encrypted buffer memory", __func__, __LINE__);
        }
    } else {
        write(file_dms_fd, map.data, map.size);
    }

    gst_buffer_unmap(buffer, &map);
    gst_sample_unref(sample);
}

static void appsink_dms_ld_cb(GstElement *appsink, gpointer user_data)
{
    static bool first_cb_ld = true;

    cam_record_ctxt *context = (cam_record_ctxt *)user_data;
    GstAppSink* app_sink = (GstAppSink*) appsink;
    GstSample *sample = gst_app_sink_pull_sample(app_sink);
    if (!sample) {
        LOG_E(log_tag[context->cam_pos], "appsink_dms_ld_cb: gst_app_sink_pull_sample returned NULL");
        return;
    }
    GstBuffer *buffer = gst_sample_get_buffer(sample);
    if (!buffer) {
        LOG_E(log_tag[context->cam_pos], "appsink_dms_ld_cb: gst_sample_get_buffer returned NULL");
        gst_sample_unref(sample);
        return;
    }
    GstMapInfo map;
    if (!gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        LOG_E(log_tag[context->cam_pos], "appsink_dms_ld_cb: gst_buffer_map failed");
        gst_sample_unref(sample);
        return;
    }

    //Proceed further only if both PTS and DTS are valid
    if (!((GST_BUFFER_PTS_IS_VALID(buffer)) && (GST_BUFFER_DTS_IS_VALID(buffer)))) {
        LOG_E(log_tag[context->cam_pos], "appsink_dms_ld_cb: PTS or DTS not valid. pts_ld(%lld) dts_ld(%lld)", GST_BUFFER_PTS(buffer), GST_BUFFER_DTS(buffer));
        gst_buffer_unmap(buffer, &map);
        gst_sample_unref(sample);
        return;
    }

    if (first_cb_ld || (session_change_dms_ld && !GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT))) {
        string cam_session_fname = to_string(context->cam_pos) + string(session_filename);
        string fname = CAMREC_FILE_BASE_PATH + cam_session_fname + ld_extn;
        gchar filename[GSTREAMER_NAME_LENGTH_MAX] = {0};
        nd_strncpy(filename, fname.c_str(), sizeof(filename));

        int index = cam_session_fname.find(".mp4");
        cam_session_fname = cam_session_fname.substr(0, index);

        if (first_cb_ld) {
            first_cb_ld = false;
            session_change_dms_ld = false;
        } else {
            session_change_dms_ld = false;
            g_mutex_lock(&session_change_mutex);
            context->session_end_epoch_ld = get_system_time();
            LOG_I(log_tag[context->cam_pos], "DEBUG_ANNOTATION: DMS LD record duration: %lld", (context->session_end_epoch_ld - context->session_start_epoch_ld));
            LOG_I(log_tag[context->cam_pos], "send_session_end_msg_to_bagheera with session_fname: %s", context->cur_file_name_ld);
            send_session_end_msg_to_bagheera(Q_NAME, context->cur_file_name_ld, context->cam_pos, context->session_end_epoch_ld, context->session_start_pts_ld, context->session_frame_count, true);
            g_mutex_unlock(&session_change_mutex);
        }

        if (file_dms_ld_fd > 0) {
            close(file_dms_ld_fd);
            file_dms_ld_fd = -1;
            LOG_I(log_tag[context->cam_pos], "file_dms_ld_fd closed");
        }
        file_dms_ld_fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (file_dms_ld_fd <= 0) {
            gst_buffer_unmap(buffer, &map);
            gst_sample_unref(sample);
            LOG_E(log_tag[context->cam_pos], "appsink_dms_ld_cb: File open failed! %s", filename);
            return;
        }
        memset(context->cur_file_name_ld, 0x0, GSTREAMER_NAME_LENGTH_MAX);
        nd_strncpy(context->cur_file_name_ld, cam_session_fname.c_str(), sizeof(context->cur_file_name_ld));

        g_mutex_lock(&session_change_mutex);
        LOG_I(log_tag[context->cam_pos], "DEBUG_ANNOTATION: Filename dms LD: %s", filename);
        context->session_start_epoch_ld = get_system_time();
        context->session_start_pts_ld = GST_BUFFER_PTS(buffer);
        LOG_I(log_tag[context->cam_pos], "DEBUG_ANNOTATION: session_start_pts_ld: %lld, session_start_epoch_ld: %lld", context->session_start_pts_ld, context->session_start_epoch_ld);
        LOG_I(log_tag[context->cam_pos], "appsink_dms_ld_cb: send_session_start_msg_to_bagheera with session_fname: %s", context->cur_file_name_ld);
        send_session_start_msg_to_bagheera(Q_NAME, context->cur_file_name_ld, context->cam_pos, context->session_start_epoch_ld, context->session_start_pts_ld, true);
        g_mutex_unlock(&session_change_mutex);
    }

    if (stream_encryption) {
        unsigned char* encrypted_buffer = NULL;
        size_t encrypted_buffer_len = 0;
        size_t file_len1 = nd_stream_encryption(map.data, map.size, &encrypted_buffer, &encrypted_buffer_len);
        LOG_D(log_tag[context->cam_pos], "%s:%d nd_stream_encryption return val: %zu", __func__, __LINE__, file_len1);

        if (encrypted_buffer) {
            write(file_dms_ld_fd, encrypted_buffer, encrypted_buffer_len);
            free(encrypted_buffer);
            LOG_D(log_tag[context->cam_pos], "%s:%d Freeing the encrypted buffer memory", __func__, __LINE__);
        }
    } else {
        write(file_dms_ld_fd, map.data, map.size);
    }

    gst_buffer_unmap(buffer, &map);
    gst_sample_unref(sample);
}

void publish_qrscan_result(unordered_map<string, vector<string>> &decoded_data, int num_qr_codes, uint64_t qr_frame_epoch, qr_scan_status status)
{
    json_t *root = json_object();
    json_t *data_obj = json_object();
    char *json_string_data = NULL;

    if (!root || !data_obj) {
        LOG_E("QR_SCAN", "Failed to create JSON objects for QR scan result");
        goto cleanup;
    }

    // Add decoded_data to JSON
    for (const auto& pair : decoded_data) {
        json_t *value_array = json_array();
        if (!value_array) {
            LOG_E("QR_SCAN", "Failed to create JSON array for key: %s", pair.first.c_str());
            continue;
        }

        // Add all values for this key
        for (const string& value : pair.second) {
            json_t *value_str = json_string(value.c_str());
            if (value_str) {
                json_array_append_new(value_array, value_str);
            } else {
                LOG_W("QR_SCAN", "Failed to create JSON string for value: %s", value.c_str());
            }
        }

        // Add the array to the data object
        json_object_set_new(data_obj, pair.first.c_str(), value_array);
    }

    // Build the final JSON structure
    json_object_set_new(root, "decoded_data", data_obj);
    json_object_set_new(root, "num_qr_codes", json_integer(num_qr_codes));
    json_object_set_new(root, "qr_frame_epoch", json_integer(qr_frame_epoch));
    json_object_set_new(root, "status", json_integer((int)status));

    // Convert to JSON string
    json_string_data = json_dumps(root, JSON_COMPACT);
    if (json_string_data) {
        if (status != QR_SCAN_NO_QR)
            LOG_I(TAG, "QR scan JSON data: %s", json_string_data);

        // Publish the JSON data
        qr_scan_data_publisher.setMessage(json_string_data).publish();

        // Free the JSON string
        free(json_string_data);
    } else {
        LOG_E(TAG, "Failed to convert QR scan data to JSON string");
    }
    
cleanup:
    if (root) {
        json_decref(root);
    }
    // Note: data_obj is cleaned up automatically when root is decremented    
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
                publish_qrscan_result(out, num_qr_codes_detected, frame_time_epoch, QR_SCAN_QR_DETECTED);
                goto next_buffer;
            }

            // Check if the decoded output is empty
            if (out.empty()) {
                LOG_W("QR_SCAN", "Decode operation returned empty result, continuing to scan");
                publish_qrscan_result(out, num_qr_codes_detected, frame_time_epoch, QR_SCAN_QR_DETECTED);
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
                    publish_qrscan_result(out, num_qr_codes_detected, frame_time_epoch, QR_SCAN_QR_DECODED);
                } else {
                    LOG_W("QR_SCAN", "Decoded QR code does not contain any valid tag fields, continuing to scan");
                    publish_qrscan_result(out, num_qr_codes_detected, frame_time_epoch, QR_SCAN_QR_DETECTED);

                    if (invalid_qr_image_dump_enabled == true) {
                        dump_invalid_qr_debug_image(y_plane, buffer_size, frame_time_epoch);
                    }
                }
            }
        } else {
            LOG_D("QR_SCAN", "No QR code detected in the current frame, continuing to scan");
            publish_qrscan_result(out, 0, 0, QR_SCAN_NO_QR);
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
        publish_qrscan_result(out, 0, 0, QR_SCAN_TIMEDOUT);
    } else {
        LOG_I("QR_SCAN", "QR decoding stopped by ndcentral request");
    }

    LOG_I("QR_SCAN", "QR decode thread exiting");
    return NULL;
}

static void appsink_qr_cb(GstElement *appsink, gpointer user_data)
{
    static bool first_cb = true;

    cam_record_ctxt *context = (cam_record_ctxt *)user_data;
    GstAppSink* app_sink = (GstAppSink*) appsink;
    GstSample *sample = gst_app_sink_pull_sample(app_sink);
    if (!sample) {
        LOG_E("QR_SCAN", "appsink_qr_cb: gst_app_sink_pull_sample returned NULL");
        return;
    }
    GstBuffer *buffer = gst_sample_get_buffer(sample);
    if (!buffer) {
        LOG_E("QR_SCAN", "appsink_qr_cb: gst_sample_get_buffer returned NULL");
        gst_sample_unref(sample);
        return;
    }
    GstMapInfo map;
    if (!gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        LOG_E("QR_SCAN", "appsink_qr_cb: gst_buffer_map failed");
        gst_sample_unref(sample);
        return;
    }

    if (qr_scan_started == true) {
        if (first_time_scan) {
            first_time_scan = false;

            // Allocate y_planes if not already allocated
            if (y_planes) {
                LOG_W("QR_SCAN", "y_planes buffer already allocated");
            } else {
                y_planes = (uint8_t *)malloc(max_queued_yplanes * (qrscan_buffer_width * qrscan_buffer_height));
                if (!y_planes) {
                    LOG_E("QR_SCAN", "Failed to allocate y_planes buffer");
                    gst_buffer_unmap(buffer, &map);
                    gst_sample_unref(sample);
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
                gst_buffer_unmap(buffer, &map);
                gst_sample_unref(sample);
                return;
            }
            qr_decode_thread_created = true;
        }

        if (!y_planes) {
            LOG_E("QR_SCAN", "y_planes is NULL, cannot copy buffer");
            gst_buffer_unmap(buffer, &map);
            gst_sample_unref(sample);
            return;
        }

        pthread_mutex_lock(&qr_scan_mutex);
        void *src_ptr = static_cast<void*>(static_cast<uint8_t*>(map.data));
        void *dst_ptr = static_cast<void*>(static_cast<uint8_t*>(y_planes + (buf_write_index * (qrscan_buffer_width * qrscan_buffer_height))));
        memcpy(reinterpret_cast<char*>(dst_ptr), reinterpret_cast<char*>(src_ptr), qrscan_buffer_width * qrscan_buffer_height);
        /* current epoch time is considered as frame_epoch_time */
        uint64_t frame_epoch_time = get_system_time(); //in milliseconds

        // Overwrite first 8 bytes of dst_ptr with frame_epoch_time
        memcpy(dst_ptr, &frame_epoch_time, sizeof(frame_epoch_time));

        is_yplane_buf_present = true;

        buf_write_index++;
        if (buf_write_index == max_queued_yplanes)
            buf_write_index = 0;

        pthread_cond_signal(&qr_scan_cond);
        pthread_mutex_unlock(&qr_scan_mutex);
    }

    gst_buffer_unmap(buffer, &map);
    gst_sample_unref(sample);
}

static GstPadProbeReturn queue_qr_callback(GstPad *pad, GstPadProbeInfo *probe, gpointer user_data)
{
    GstBuffer *buffer = NULL;
    static volatile uint64_t prev_pts = 0;
    static bool first_cb = true;
    uint64_t pts_diff = 0;
    uint64_t curr_pts = 0;

    cam_record_ctxt *context = (cam_record_ctxt *)user_data;
    buffer = GST_PAD_PROBE_INFO_BUFFER (probe);
    if (buffer == NULL) {
        LOG_E(log_tag[context->cam_pos], "Buffer NULL in queue_qr_callback");
        return GST_PAD_PROBE_REMOVE;
    }

    GstMapInfo map;
    gst_buffer_map (buffer, &map, GST_MAP_READ);

    curr_pts = GST_BUFFER_PTS (buffer);

    gst_buffer_unmap(buffer, &map);

    if (curr_pts < prev_pts) {
        LOG_E(log_tag[context->cam_pos], "%s :: Unexpected! Current PTS: %llu Prev PTS: %llu", __func__, curr_pts, prev_pts);
        return GST_PAD_PROBE_DROP;
    }

    if (qr_scan_started == false) {
        LOG_D(log_tag[context->cam_pos], "%s :: QR scan not started, dropping frame", __func__);
        return GST_PAD_PROBE_DROP;
    }

    pts_diff = curr_pts - prev_pts;
    LOG_D(log_tag[context->cam_pos], "%s :: curr_pts = %ld, prev_pts = %ld, pts_diff = %ld", __func__, curr_pts, prev_pts, pts_diff);
    if (first_cb || (pts_diff > QR_MIN_INTRA_FRAME_INTERVAL_NS)) {
        first_cb = false;
        LOG_D(log_tag[context->cam_pos], "%s :: forwarding frame with pts = %ld", __func__, curr_pts);
        prev_pts = curr_pts;
        return GST_PAD_PROBE_OK;
    }

    LOG_D(log_tag[context->cam_pos], "dropping frame");
    return GST_PAD_PROBE_DROP;
}

static GstPadProbeReturn queue_rt_callback(GstPad *pad,GstPadProbeInfo *probe,
                                            gpointer user_data)
{
    GstBuffer *buffer = NULL;
    static volatile uint64_t prev_pts_inward = 0;
    static volatile uint64_t prev_pts_dms = 0;
    static bool first_cb = true;
    uint64_t pts_diff = 0;
    uint64_t curr_pts_inward = 0;
    uint64_t curr_pts_dms = 0;

    cam_record_ctxt *context = (cam_record_ctxt *)user_data;
    buffer = GST_PAD_PROBE_INFO_BUFFER (probe);
    if (buffer == NULL) {
        LOG_E(log_tag[context->cam_pos], "Buffer NULL in queue_rt__callback");
        return GST_PAD_PROBE_REMOVE;
    }

    GstMapInfo map;
    gst_buffer_map (buffer, &map, GST_MAP_READ);

    if (context->cam_pos == CAMERA_POSITION_DMS)
        curr_pts_dms = GST_BUFFER_PTS (buffer);
    else if (context->cam_pos == CAMERA_POSITION_BACK)
        curr_pts_inward = GST_BUFFER_PTS (buffer);

    gst_buffer_unmap(buffer, &map);

    if ((context->cam_pos == CAMERA_POSITION_DMS) && (curr_pts_dms < prev_pts_dms)) {
        /* Once DMS disconnect/connect happens, PTS value from DMS camera is reinitialized from the start
           In that case the current PTS value will be smaller than the last PTS value we received before
           disconnect. To handle that case below condition is added */
        if (is_dms_disconnect_connect_happened) {
            LOG_I(log_tag[context->cam_pos], "[DMS disconnect/connect case], lets handle by assigning prev_pts_dms to curr_pts_dms");
            is_dms_disconnect_connect_happened = false;
            prev_pts_dms = curr_pts_dms;
            return GST_PAD_PROBE_OK;
        }
        LOG_E(log_tag[context->cam_pos], "Unexpected! Current PTS: %llu Prev PTS: %llu", curr_pts_dms, prev_pts_dms);
        return GST_PAD_PROBE_DROP;
    }
    if ((context->cam_pos == CAMERA_POSITION_BACK) && (curr_pts_inward < prev_pts_inward)) {
        LOG_E(log_tag[context->cam_pos], "Unexpected! Current PTS: %llu Prev PTS: %llu", curr_pts_inward, prev_pts_inward);
        return GST_PAD_PROBE_DROP;
    }

    if (context->cam_pos == CAMERA_POSITION_DMS) {
        pts_diff = curr_pts_dms - prev_pts_dms;
        LOG_D(log_tag[context->cam_pos],"curr_pts_dms = %ld, prev_pts_dms = %ld, pts_diff = %ld", curr_pts_dms, prev_pts_dms, pts_diff);
        if (first_cb || (pts_diff > DMS_MIN_INTRA_FRAME_INTERVAL_NS)) {
            first_cb = false;
            LOG_D(log_tag[context->cam_pos], "forwarding frame");
            prev_pts_dms = curr_pts_dms;
            return GST_PAD_PROBE_OK;
        }
    }
    if (context->cam_pos == CAMERA_POSITION_BACK) {
        pts_diff = curr_pts_inward - prev_pts_inward;
        LOG_D(log_tag[context->cam_pos],"curr_pts_inward = %ld, prev_pts_inward = %ld, pts_diff = %ld", curr_pts_inward, prev_pts_inward, pts_diff);
        if (first_cb || (pts_diff > INWARD_MIN_INTRA_FRAME_INTERVAL_NS)) {
            first_cb = false;
            LOG_D(log_tag[context->cam_pos], "forwarding frame");
            prev_pts_inward = curr_pts_inward;
            return GST_PAD_PROBE_OK;
        }
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
        LOG_E("ERROR","invalid param received");
        return GST_PAD_PROBE_REMOVE;
    }

    context = (cam_record_ctxt *)user_data;

    if (pad != context->keep_alive_pad) {
        LOG_E(log_tag[context->cam_pos],"Wrong probe id blocked queue source pad");
        return GST_PAD_PROBE_REMOVE;
    }

    buffer = GST_PAD_PROBE_INFO_BUFFER (probe);
    if (buffer == NULL) {
        LOG_E (log_tag[context->cam_pos], "Buffer NULL in keep_alive_callback");
        return GST_PAD_PROBE_REMOVE;
    }

    //If PTS is not valid, skip this call back and wait for next
    if (!GST_BUFFER_PTS_IS_VALID(buffer))
        return GST_PAD_PROBE_OK;

    // Reset stop_recording_received if frames are being received
    context->stop_recording_received = false;

    pts_diff = buffer->pts - previous_time[context->cam_pos];
    if (pts_diff > DROP_LIMIT)
        LOG_E(log_tag[context->cam_pos], "Time difference between frames: %llu", pts_diff);

    previous_time[context->cam_pos] = buffer->pts;

    //LOG_I(log_tag[context->cam_pos],"Keep alive callback");
    context->total_frame_count++;
    g_mutex_lock(&context->cam_frame_mutex);
    context->session_frame_count++;
    context->cam_frame_count++;
    g_mutex_unlock(&context->cam_frame_mutex);

    if (context->cam_pos == CAMERA_POSITION_BACK)
        buffer->dts = -1;

#ifdef DMS_CAMERA_SUPPORTED
    if (context->cam_pos == CAMERA_POSITION_DMS) {
        if (1 == context->total_frame_count || is_dms_disconnected_and_reconnected) {
            if (is_dms_disconnected_and_reconnected) {
                LOG_I(log_tag[context->cam_pos], "First camera frame received after dms_disconnect_connect event");
                is_dms_disconnected_and_reconnected = false;
            }

            is_dms_streaming_started = true;

            dms_irled_event_info_t dms_irled_event_info = {};
            dms_irled_event_info.event = DMS_IRLED_EVENT_STARTED_STREAMING;
            // Wait for 1 second after DMS streaming starts and clear the IRLED status, as per HW team recommendation (refer : DMS-109)
            LOG_I(log_tag[context->cam_pos], "DMS camera started streaming. Now clearing the IRLED status");
            // Wrap the struct in an nd_msg_t
            nd_msgq_t::nd_msg_t msg((char*)&dms_irled_event_info, sizeof(dms_irled_event_info), false);
            if (msgq_dms_irled) {
                if (!msgq_dms_irled->send(msg, nd_msgq_t::ND_MSG_MED)) {
                    LOG_E(log_tag[context->cam_pos], "Unable to send DMS IRLED status message to msgq");
                } else {
                    LOG_I(log_tag[context->cam_pos], "Pushed event %d to msgq: %p", DMS_IRLED_EVENT_STARTED_STREAMING, msgq_dms_irled);
                }
            }
        }
    }
#endif

    // For all cameras, we have nothing to do except for first frame
    if (context->total_frame_count > 1)
        return GST_PAD_PROBE_OK;

    LOG_I(log_tag[context->cam_pos],"First camera frame received");

    if (eBagheera_2 == nd_device_obj->getDeviceType()) {
#ifdef DEBUG_15_FPS
        if ((context->cam_pos == CAMERA_POSITION_LEFT) || (context->cam_pos == CAMERA_POSITION_RIGHT)) {
            unsigned char rval;
            LOG_I(log_tag[context->cam_pos], "Setting 15 fps for the side cameras, reg:0x%x val:0x%x", SIDE_FPS_REG, SIDE_FPS_VAL);
            if (cam_reg_write(context->cam_pos, SIDE_FPS_REG, SIDE_FPS_VAL)) {
                LOG_E(log_tag[context->cam_pos], "Setting register for the side cameras failed");
            }

            if (cam_reg_read(context->cam_pos, SIDE_FPS_REG, &rval)) {
                LOG_E(log_tag[context->cam_pos], "Side Camera Register read failed");
            } else {
                LOG_I(log_tag[context->cam_pos], "side_cam_register: 0x%x value: 0x%x", SIDE_FPS_REG, rval);
            }
        } else if (context->cam_pos == CAMERA_POSITION_BACK) {
            unsigned char rval1, rval2, rval3;
            LOG_I(log_tag[context->cam_pos], "Setting 15 fps for the inward camera, reg1:0x%x val1:0x%x, reg2:0x%x val2:0x%x, reg3:0x%x val3:0x%x", INW_FPS_REG1, INW_FPS_VAL1, INW_FPS_REG2, INW_FPS_VAL2, INW_FPS_REG3, INW_FPS_VAL3);
            if (cam_reg_write(context->cam_pos, INW_FPS_REG1, INW_FPS_VAL1) ||
                cam_reg_write(context->cam_pos, INW_FPS_REG2, INW_FPS_VAL2) ||
                cam_reg_write(context->cam_pos, INW_FPS_REG3, INW_FPS_VAL3)) {
                LOG_E(log_tag[context->cam_pos], "Setting registers for the inward camera failed");
            }

            if (cam_reg_read(context->cam_pos, INW_FPS_REG1, &rval1) ||
                cam_reg_read(context->cam_pos, INW_FPS_REG2, &rval2) ||
                cam_reg_read(context->cam_pos, INW_FPS_REG3, &rval3)) {
                LOG_E(log_tag[context->cam_pos], "Inward Camera Register read failed");
            } else {
                LOG_I(log_tag[context->cam_pos], "inw_cam_reg1: 0x%x val1: 0x%x, inw_cam_reg2: 0x%x val2: 0x%x, inw_cam_reg3: 0x%x val3: 0x%x", INW_FPS_REG1, rval1, INW_FPS_REG2, rval2, INW_FPS_REG3, rval3);
            }
        }
#endif
    }
#ifdef DISABLE_KEEP_ALIVE
    gst_pad_remove_probe (context->keep_alive_pad,context->keep_alive_probe);
    context->keep_alive_probe = 0;
#endif

    return GST_PAD_PROBE_OK;
}

bool frame_shm_write_cb_func(int smb_id, int64_t uid, void *data_ptr, int64_t data_len, void *user_data)
{
    cam_record_ctxt *context = (cam_record_ctxt *)user_data;
    int camera_num = 0;
    static int i = 0;
#ifdef ENABLE_RT_YUV_DUMP
    FILE *fp;
    char str[] = "/media/data/nd_sdcard/inwardcam_rt_dump.yuv";
#endif
    LOG_D(log_tag[context->cam_pos],"data_len: %d, inward_rt_buf_size: %d", data_len, inward_rt_buf_size);

    unsigned long int before_time = g_get_monotonic_time ();
    memcpy(data_ptr,(uint8_t *)context->frame_data_ptr,inward_rt_buf_size);
    unsigned long int after_time = g_get_monotonic_time ();
    LOG_D(log_tag[context->cam_pos],"Memcpy time taken is :%ld",after_time-before_time);

#ifdef ENABLE_RT_YUV_DUMP
    fp = fopen(str, "a+");
    if (fp != NULL) {
        fwrite(data_ptr, 1, data_len, fp);
        fclose(fp);
    } else {
        LOG_E(log_tag[context->cam_pos], "Failed to open the file for writing inward camera yuv dump");
    }
#endif

    return true;
}

void recreate_incam_rt_shared_memory()
{
    cam_record_ctxt *context = (cam_record_ctxt *)&cam_rec_ctx[CAMERA_POSITION_BACK];

    g_mutex_lock(&context->shm_writer_mutex);

    drop_inward_rt_frames = true;

    LOG_I(log_tag[context->cam_pos], "SHM_DEBUG :: Deleting older instance of inward camera shm_writer 0x%x", context->shm_writer);
    delete context->shm_writer;
    context->shm_writer = NULL;

    int64_t smb_data_size = (int64_t)(context->rt_config.width * context->rt_config.height * 3/2);
    context->shm_writer = new NdSharedMemoryWriter(context->shm_writer_name, smb_data_size, context->shm_buffers);
    if (context->shm_writer) {
        LOG_I(log_tag[context->cam_pos],"SHM_DEBUG :: Shared Memory Writer 0x%x recreated successfully for inward camera with smb_data_size: %lld and shm_buffers: %d",
                context->shm_writer, smb_data_size, context->shm_buffers);
        context->shm_writer->set_log_frequency(100);
        context->shm_writer->register_write_callback(frame_shm_write_cb_func);

        cam_record_service_obj->send_err_msg(SM_E_NDC_CAM_SHM_RECREATED, CAMERA_POSITION_BACK, "RT shared memory recreated for inward camera");
    }
    drop_inward_rt_frames = false;

    g_mutex_unlock(&context->shm_writer_mutex);
}

static void init_context_default_params(int cam_num, realtime_camera_config_t* rt_config)
{
    bool get_override_val = true, is_val_overridden = false;

    camera_pos cam_pos = (camera_pos)cam_num;
    cam_record_ctxt *context = &cam_rec_ctx[cam_pos];
    memset(context, 0, sizeof(cam_record_ctxt));

    Config_parser *bagheera_config = new Config_parser(BAGHEERA_CONFIG_INI);
    Config_parser *nd_config = new Config_parser(ND_CONFIG_INI);

    LOG_I(log_tag[context->cam_pos], "Set the context for camera %d", cam_pos);

    if ((cam_pos == CAMERA_POSITION_BACK) && (rt_config->enable_streaming == true)) {
        memcpy(&context->rt_config, rt_config, sizeof(realtime_camera_config_t));
        context->yuvbuf_size_rt = (context->rt_config.width * context->rt_config.height * 3) / 2;
        INWARD_MIN_INTRA_FRAME_INTERVAL_NS = ((1000 / context->rt_config.fps) * 1000 * 1000) - ((1000 / (30 * 2)) * 1000 * 1000);
        string temp = nd_config->getConfig("inwardRealTime", "shm_buffers", "5", get_override_val, is_val_overridden);
        if (!string_to_integer(temp.c_str(), context->shm_buffers))
            context->shm_buffers = 5;
        LOG_I(log_tag[context->cam_pos], "inwardcam_yuvbuf_size: %d, INWARD_MIN_INTRA_FRAME_INTERVAL_NS: %d", context->yuvbuf_size_rt, INWARD_MIN_INTRA_FRAME_INTERVAL_NS);
    } else if ((cam_pos == CAMERA_POSITION_DMS) && (rt_config->enable_streaming == true)) {
        memcpy(&context->rt_config, rt_config, sizeof(realtime_camera_config_t));
        context->yuvbuf_size_rt = (context->rt_config.width * context->rt_config.height * 3) / 2;
        DMS_MIN_INTRA_FRAME_INTERVAL_NS = ((1000 / context->rt_config.fps) * 1000 * 1000) - ((1000 / (30 * 2)) * 1000 * 1000);
        string temp = nd_config->getConfig("dms", "shm_buffers", "30", get_override_val, is_val_overridden);
        if (!string_to_integer(temp.c_str(), context->shm_buffers))
            context->shm_buffers = 30;
        LOG_I(log_tag[context->cam_pos], "dmscam_yuvbuf_size: %d, DMS_MIN_INTRA_FRAME_INTERVAL_NS: %d", context->yuvbuf_size_rt, DMS_MIN_INTRA_FRAME_INTERVAL_NS);
    }

    string temp = bagheera_config->getConfig("camera", "inward_nrt_width", "1920", get_override_val, is_val_overridden);
    string_to_integer(temp.c_str(), context->nrt_inward_config.width);

    temp = bagheera_config->getConfig("camera", "inward_nrt_height", "1080", get_override_val, is_val_overridden);
    string_to_integer(temp.c_str(), context->nrt_inward_config.height);

    temp = bagheera_config->getConfig("camera", "inward_nrt_fps", "15", get_override_val, is_val_overridden);
    string_to_integer(temp.c_str(), context->nrt_inward_config.fps);

    temp = bagheera_config->getConfig("camera", "inward_nrt_bitrate", "2000000", get_override_val, is_val_overridden);
    string_to_integer(temp.c_str(), context->nrt_inward_config.bitrate);

    temp = bagheera_config->getConfig("camera","inward_nrt_ld_width", "854", get_override_val, is_val_overridden);
    string_to_integer(temp.c_str(), context->nrt_ld_inward_config.width);

    temp = bagheera_config->getConfig("camera","inward_nrt_ld_height", "480", get_override_val, is_val_overridden);
    string_to_integer(temp.c_str(), context->nrt_ld_inward_config.height);

    temp = bagheera_config->getConfig("camera","inward_nrt_ld_bitrate", "500000", get_override_val, is_val_overridden);
    string_to_integer(temp.c_str(), context->nrt_ld_inward_config.bitrate);

    LOG_I(log_tag[context->cam_pos], "Inward LD config: width=%d, height=%d, bitrate=%d",
      context->nrt_ld_inward_config.width, context->nrt_ld_inward_config.height, context->nrt_ld_inward_config.bitrate);

    temp = bagheera_config->getConfig("camera", "dms_nrt_width", "1296", get_override_val, is_val_overridden);
    string_to_integer(temp.c_str(), context->nrt_dms_config.width);

    temp = bagheera_config->getConfig("camera", "dms_nrt_height", "1296", get_override_val, is_val_overridden);
    string_to_integer(temp.c_str(), context->nrt_dms_config.height);

    temp = bagheera_config->getConfig("camera", "dms_nrt_fps", "30", get_override_val, is_val_overridden);
    string_to_integer(temp.c_str(), context->nrt_dms_config.fps);

    temp= bagheera_config->getConfig("camera","dms_nrt_bitrate", "8000000", get_override_val, is_val_overridden);
    string_to_integer(temp.c_str(), context->nrt_dms_config.bitrate);

    temp = bagheera_config->getConfig("camera","dms_nrt_ld_width", "720", get_override_val, is_val_overridden);
    string_to_integer(temp.c_str(), context->nrt_ld_dms_config.width);

    temp = bagheera_config->getConfig("camera","dms_nrt_ld_height", "720", get_override_val, is_val_overridden);
    string_to_integer(temp.c_str(), context->nrt_ld_dms_config.height);

    temp = bagheera_config->getConfig("camera","dms_nrt_ld_bitrate", "1000000", get_override_val, is_val_overridden);
    string_to_integer(temp.c_str(), context->nrt_ld_dms_config.bitrate);

    LOG_I(log_tag[context->cam_pos], "DMS LD config: width=%d, height=%d, bitrate=%d",
      context->nrt_ld_dms_config.width, context->nrt_ld_dms_config.height, context->nrt_ld_dms_config.bitrate);

    temp= bagheera_config->getConfig("camera","leftcam_nrt_bitrate", "500000", get_override_val, is_val_overridden);
    string_to_integer(temp.c_str(), context->nrt_leftcam_config.bitrate);

    temp= bagheera_config->getConfig("camera","rightcam_nrt_bitrate", "500000", get_override_val, is_val_overridden);
    string_to_integer(temp.c_str(), context->nrt_rightcam_config.bitrate);

    temp = bagheera_config->getConfig("camera", "leftcam_nrt_width", "1280", get_override_val, is_val_overridden);
    string_to_integer(temp.c_str(), context->nrt_leftcam_config.width);

    temp = bagheera_config->getConfig("camera", "leftcam_nrt_height", "720", get_override_val, is_val_overridden);
    string_to_integer(temp.c_str(), context->nrt_leftcam_config.height);

    temp = bagheera_config->getConfig("camera", "rightcam_nrt_width", "1280", get_override_val, is_val_overridden);
    string_to_integer(temp.c_str(), context->nrt_rightcam_config.width);

    temp = bagheera_config->getConfig("camera", "rightcam_nrt_height", "720", get_override_val, is_val_overridden);
    string_to_integer(temp.c_str(), context->nrt_rightcam_config.height);

#ifdef LIVE_SREAMING_EN
    // reset kinesis stream info
    memset(&cam_record_ctxt[cam_pos].kinesis_stream_info, 0, sizeof(cam_record_ctxt[cam_pos].kinesis_stream_info));
#endif

    context->state  = STATE_CREATED;
    context->queue_msg = 0;
    context->keep_alive_probe = 0;
    context->cam_pos = cam_pos;
    context->total_frame_count = 0; //used by keep_alive_callback
    context->session_frame_count = 0;
    g_cond_init (&context->cond);
    context->cond_var = 0;
    g_mutex_init (&context->mutex);
    g_mutex_init (&context->cam_frame_mutex);
    g_mutex_init (&session_change_mutex);
    context->queue = g_async_queue_new();

    switch (cam_pos) {
        case CAMERA_POSITION_BACK:
            context->width  = context->nrt_inward_config.width;
            context->height = context->nrt_inward_config.height;
            context->analytics_width_driv = context->rt_config.width;
            context->analytics_height_driv = context->rt_config.height;
            break;
        case CAMERA_POSITION_LEFT:
            if (eBagheera_3 == nd_device_obj->getDeviceType()) {
                context->width  = context->nrt_leftcam_config.width;
                context->height = context->nrt_leftcam_config.height;
            } else {
                context->width  = GST_OTHER_CAMERA_WIDTH;
                context->height = GST_OTHER_CAMERA_HEIGHT;
            }
            context->nrt_leftcam_config.fps = SIDECAM_FPS;
            break;
        case CAMERA_POSITION_RIGHT:
            if (eBagheera_3 == nd_device_obj->getDeviceType()) {
                context->width  = context->nrt_rightcam_config.width;
                context->height = context->nrt_rightcam_config.height;
            } else {
                context->width  = GST_OTHER_CAMERA_WIDTH;
                context->height = GST_OTHER_CAMERA_HEIGHT;
            }
            context->nrt_rightcam_config.fps = SIDECAM_FPS;
            break;
        case CAMERA_POSITION_DMS:
            context->width  = context->nrt_dms_config.width;
            context->height = context->nrt_dms_config.height;
            context->analytics_width_driv = context->rt_config.width;
            context->analytics_height_driv = context->rt_config.height;
            break;
    }

    LOG_I(log_tag[context->cam_pos], "SHM_DEBUG :: rt_enable_streaming %d", rt_config->enable_streaming);
    if ((context->cam_pos == CAMERA_POSITION_BACK) && rt_config->enable_streaming) {
        // fill shm writer object into context and register write cb
        LOG_I(log_tag[context->cam_pos], "SHM_DEBUG :: rt_width: %d", context->rt_config.width);
        LOG_I(log_tag[context->cam_pos], "SHM_DEBUG :: rt_height: %d", context->rt_config.height);
        LOG_I(log_tag[context->cam_pos], "SHM_DEBUG :: rt_fps: %d", context->rt_config.fps);
        LOG_I(log_tag[context->cam_pos], "SHM_DEBUG :: rt_shm_size: %ld", (rt_config->width * rt_config->height * 3));
        LOG_I(log_tag[context->cam_pos], "SHM_DEBUG :: rt_shm_buffers: %d", context->shm_buffers);

        //frame size we are dividing by 2 (3/2) because the frame size will be 1.5 times of width * height
        int64_t smb_data_size = (int64_t)(rt_config->width * rt_config->height * 3/2);

        stringstream cam_name;
        cam_name << "CAM" << context->cam_pos;

        string name = cam_name.str();

        context->shm_writer = new NdSharedMemoryWriter(name, smb_data_size, context->shm_buffers);
        if (context->shm_writer) {
            LOG_I(log_tag[context->cam_pos],"SHM_DEBUG :: Shared Memory Writer 0x%x created successfully for RT camera with smb_data_size: %lld and shm_buffers: %d",
                    context->shm_writer, smb_data_size, context->shm_buffers);

            context->shm_writer->set_log_frequency(100);
            context->shm_writer->register_write_callback(frame_shm_write_cb_func);
            context->shm_writer_name = name;
        }
        g_mutex_init (&context->shm_writer_mutex);
    }
}

int init_record_session_platform(int cam_pos)
{
   int ret = TRUE;
   cam_record_ctxt *context = &cam_rec_ctx[cam_pos];

   if (context == NULL)
       return FALSE;

   g_mutex_lock(&context->mutex);

   if (context->state == STATE_CREATED) {

       LOG_I(log_tag[context->cam_pos],"creating thread for record session");

       context->thread = g_thread_new (log_tag[context->cam_pos],
                                       gst_thread_func,
                                       context);

       context->queue_msg = (int)STATE_INIT;

       LOG_D(log_tag[context->cam_pos],"Posting INIT msg to queue");

       g_async_queue_push (context->queue,&(context->queue_msg));

       while (!(context->cond_var))
           g_cond_wait (&(context->cond), &(context->mutex));

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

     LOG_I(tag,"get_sensor_map_version");

     if (tag == NULL) {
         LOG_I(tag,"TAG is NULL");
         return FALSE;
     }

     fp = fopen("/etc/vendor/version_ntdi.txt","r");

     if (fp == NULL) {
         LOG_E(tag,"File open for version_icdc failed");
         return FALSE;
     }

     if ((count = fread((void *)buffer, 1 ,VERSION_STRING_LENGTH, fp)) <= 0) {
         LOG_E(tag,"File read for version number failed");
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
             LOG_I(tag,"Version %s , Older than 1.3.0.3 found",tmp);
             version_id = SENSOR_MAP_VERSION_0_1;
        }
        else {
             LOG_I(tag,"Version 1.3.0.3 or above %s found ,sensor id changed",tmp);
             version_id = SENSOR_MAP_VERSION_1_0;
         }

         ret = TRUE;
     }
     else
     {
        LOG_E(tag,"Version number not mentioned");

     }

     return ret;
}

bool init_camera_record_platform(int cam_num, realtime_camera_config_t* rt_config)
{
    string cam_name = "";
    cam_name = "CAM" + to_string(cam_num);
    const char *tag = cam_name.c_str();
    int len = cam_name.length();

    if (version_id == SENSOR_MAP_VERSION_INVALID) {
        if (get_sensor_map_version(tag) != TRUE) {
            LOG_E(tag,"get sensor map version failed" );
            return FALSE;
        }
    }
    LOG_I(tag, "init context for camera %d", cam_num);
    strncpy(log_tag[cam_num], tag, len);
    init_context_default_params(cam_num, rt_config);

    return TRUE;
}

void overrun_handler_queue_nrt (void *queue,
               gpointer  user_data)
{
    cam_record_ctxt *context = (cam_record_ctxt *)(user_data);
    LOG_I(log_tag[context->cam_pos], "Overrun for NRT Queue");
}

void overrun_handler_queue_tc (void *queue, gpointer user_data)
{
    cam_record_ctxt *context = (cam_record_ctxt *)(user_data);
    LOG_I(log_tag[context->cam_pos], "Overrun for TC Queue");
}

void configure_RT_pipeline(cam_record_ctxt *context)
{
    GstElement *elem = NULL;

    if ((context->cam_pos == CAMERA_POSITION_DMS) || (context->cam_pos == CAMERA_POSITION_BACK)) {
        elem = gst_bin_get_by_name(GST_BIN(context->pipeline), "queue_rt");
        context->queue_rt_pad = gst_element_get_static_pad (elem, "sink");
        context->queue_rt_probe = gst_pad_add_probe(context->queue_rt_pad, GST_PAD_PROBE_TYPE_BUFFER,
                (GstPadProbeCallback) queue_rt_callback, context, NULL);
        g_object_unref (elem);
    }

    elem = gst_bin_get_by_name(GST_BIN(context->pipeline), "queue_rt");
    g_signal_connect (elem, "overrun", G_CALLBACK (overrun_handler_queue_rt), context);
    g_object_unref (elem);

    elem = gst_bin_get_by_name(GST_BIN(context->pipeline), "queue_appsink");
    g_signal_connect (elem, "overrun", G_CALLBACK (overrun_handler_queue_appsink), context);
    g_object_unref (elem);
}

void configure_NRT_pipeline(cam_record_ctxt *context)
{
    GstElement *elem = NULL;
    GstElement *muxer = NULL;

    elem = gst_bin_get_by_name(GST_BIN(context->pipeline), "queue_nrt");
    context->keep_alive_pad = gst_element_get_static_pad (elem, "sink");
    context->keep_alive_probe = gst_pad_add_probe(context->keep_alive_pad, GST_PAD_PROBE_TYPE_BUFFER,
		    (GstPadProbeCallback) keep_alive_callback, context, NULL);
    g_signal_connect (elem, "overrun", G_CALLBACK (overrun_handler_queue_nrt), context);
    g_object_unref (elem);

    if (context->cam_pos == CAMERA_POSITION_BACK) {
        elem = gst_bin_get_by_name(GST_BIN(context->pipeline), "appsink_inward");
        g_signal_connect(elem, "new-sample", G_CALLBACK (appsink_inward_cb), context);
        g_object_unref (elem);
    }

    if (context->cam_pos == CAMERA_POSITION_DMS) {
        elem = gst_bin_get_by_name(GST_BIN(context->pipeline), "appsink_dms");
        g_signal_connect(elem, "new-sample", G_CALLBACK (appsink_dms_cb), context);
        g_object_unref (elem);
    }

    if (context->cam_pos == CAMERA_POSITION_LEFT) {
        elem = gst_bin_get_by_name(GST_BIN(context->pipeline), "appsink_left_camera");
        g_signal_connect(elem, "new-sample", G_CALLBACK (appsink_left_cb), context);
        g_object_unref (elem);
    }

    if (context->cam_pos == CAMERA_POSITION_RIGHT) {
        elem = gst_bin_get_by_name(GST_BIN(context->pipeline), "appsink_right_camera");
        g_signal_connect(elem, "new-sample", G_CALLBACK (appsink_right_cb), context);
        g_object_unref (elem);
    }
}

void configure_transcode_pipeline(cam_record_ctxt *context)
{
   GstElement *elem = NULL;
   GstElement *muxer = NULL;
   GstPad     *mux_src_pad = NULL;

   elem = gst_bin_get_by_name(GST_BIN(context->pipeline), "queue_tc");
   g_signal_connect (elem, "overrun", G_CALLBACK (overrun_handler_queue_tc), context);
   g_object_unref (elem);

   if (context->cam_pos == CAMERA_POSITION_BACK) {
       elem = gst_bin_get_by_name(GST_BIN(context->pipeline), "appsink_inward_ld");
       g_signal_connect(elem, "new-sample", G_CALLBACK (appsink_inward_ld_cb), context);
       g_object_unref (elem);
   }

   if (context->cam_pos == CAMERA_POSITION_DMS) {
       elem = gst_bin_get_by_name(GST_BIN(context->pipeline), "appsink_dms_ld");
       g_signal_connect(elem, "new-sample", G_CALLBACK (appsink_dms_ld_cb), context);
       g_object_unref (elem);
   }
}

static gboolean create_other_camera_pipeline(cam_record_ctxt *context)
{
   GError *error = NULL;
   GstElement *elem;
   stringstream pipeline, nrt_pipeline;
   stringstream rt_pipeline, nrt_ld_pipeline;
   stringstream kinesis_pipeline, full_res_pipeline;
   stringstream qr_pipeline;

   gint sensor_id = -1;
   gint sensor_mode = 0;
   gchar camera_name[GSTREAMER_NAME_LENGTH_MAX] = {0};
   gchar nrt_appsink_name[GSTREAMER_NAME_LENGTH_MAX] = {0};
   int nrt_fps;
   int64_t nrt_br;
   int nrt_crop = 0;

   if (!context)
       return FALSE;

/*
 * sensor_modes
 *enum {
OV2735_MODE_1920X1080_15FPS,
OV2735_MODE_1600X900_15FPS,
OV2735_MODE_1280X720_15FPS,
OV2735_MODE_1920X1080,
OV2735_MODE_1600X900,
OV2735_MODE_START_STREAM,
OV2735_MODE_STOP_STREAM,
OV2735_MODE_TEST_PATTERN
};
 * */
   if (version_id == SENSOR_MAP_VERSION_1_0) {
       LOG_I(log_tag[context->cam_pos], "New camera sensor v1.0  mapping");

       switch (context->cam_pos) {
           case CAMERA_POSITION_LEFT:
               sensor_id = 1;
                if( (context->nrt_leftcam_config.width == GST_CAMERA_WIDTH_MODE_A) &&
                       (context->nrt_leftcam_config.height == GST_CAMERA_HEIGHT_MODE_A) )
               {
                   sensor_mode = 0;//1920x1080 15 fps
                   if(context->nrt_leftcam_config.fps != SIDECAM_FPS)
                   {
                       sensor_mode = 3;// 30 fps
                   }
               }
               else if( (context->nrt_leftcam_config.width == GST_CAMERA_WIDTH_MODE_B) &&
                       (context->nrt_leftcam_config.height == GST_CAMERA_HEIGHT_MODE_B) )
               {
                   sensor_mode = 1;//1600x0900 15 fps
                   if(context->nrt_leftcam_config.fps != SIDECAM_FPS)
                   {
                       sensor_mode = 4;// 30 fps
                   }
               } else {
                   sensor_mode = 2;//1280x0720 15 fps
               }

               snprintf(camera_name, sizeof(camera_name), "left_camera");
               nrt_fps = context->nrt_leftcam_config.fps;
               nrt_br = context->nrt_leftcam_config.bitrate;
               nrt_crop = context->nrt_leftcam_config.crop;
               snprintf(nrt_appsink_name, sizeof(nrt_appsink_name), "appsink_left_camera");
               break;

           case CAMERA_POSITION_BACK:
               sensor_id = 0;
               sensor_mode = 0;//1920x1080 15 fps
               snprintf(camera_name, sizeof(camera_name), "inward_camera");
               nrt_fps = context->nrt_inward_config.fps;
               nrt_br = context->nrt_inward_config.bitrate;
               snprintf(nrt_appsink_name, sizeof(nrt_appsink_name), "appsink_inward");
               break;

           case CAMERA_POSITION_DMS:
               snprintf(camera_name, sizeof(camera_name), "dms_camera");
               nrt_fps = context->nrt_dms_config.fps;
               nrt_br = context->nrt_dms_config.bitrate;
               snprintf(nrt_appsink_name, sizeof(nrt_appsink_name), "appsink_dms");
               break;

           case CAMERA_POSITION_RIGHT:
               sensor_id = 2;
              if( (context->nrt_rightcam_config.width == GST_CAMERA_WIDTH_MODE_A) &&
                       (context->nrt_rightcam_config.height == GST_CAMERA_HEIGHT_MODE_A) )
               {
                   sensor_mode = 0;//1920x1080 15 fps
                   if(context->nrt_rightcam_config.fps != SIDECAM_FPS)
                   {
                       sensor_mode = 3;// 30 fps
                   }
               }
               else  if( (context->nrt_rightcam_config.width == GST_CAMERA_WIDTH_MODE_B) &&
                       (context->nrt_rightcam_config.height == GST_CAMERA_HEIGHT_MODE_B) )
               {
                   sensor_mode = 1;//1600x0900 15 fps
                   if(context->nrt_rightcam_config.fps != SIDECAM_FPS)
                   {
                       sensor_mode = 4;// 30 fps
                   }
               } else {
                   sensor_mode = 2;//1280x0720 15 fps
               }

               snprintf(camera_name, sizeof(camera_name), "right_camera");
               nrt_fps = context->nrt_rightcam_config.fps;
               nrt_br = context->nrt_rightcam_config.bitrate;
               snprintf(nrt_appsink_name, sizeof(nrt_appsink_name), "appsink_right_camera");
               break;
       }
   } else {
       LOG_I(log_tag[context->cam_pos], "Old camera sensor v0.1  mapping");

       switch (context->cam_pos) {
           case CAMERA_POSITION_LEFT:
               sensor_id = 0;
               snprintf(camera_name, sizeof(camera_name), "left_camera");
               nrt_fps = context->nrt_leftcam_config.fps;
               nrt_br = context->nrt_leftcam_config.bitrate;
               break;

           case CAMERA_POSITION_BACK:
               sensor_id = 2;
               snprintf(camera_name, sizeof(camera_name), "inward_camera");
               nrt_fps = context->nrt_inward_config.fps;
               nrt_br = context->nrt_inward_config.bitrate;
               break;

           case CAMERA_POSITION_RIGHT:
               sensor_id = 1;
               snprintf(camera_name, sizeof(camera_name), "right_camera");
               nrt_fps = context->nrt_rightcam_config.fps;
               nrt_br = context->nrt_rightcam_config.bitrate;
               break;

           case CAMERA_POSITION_DMS:
               snprintf(camera_name, sizeof(camera_name), "dms_camera");
               nrt_fps = context->nrt_dms_config.fps;
               nrt_br = context->nrt_dms_config.bitrate;
               break;
       }
   }

   if (context->pipeline == NULL) {
       if (context->cam_pos == CAMERA_POSITION_DMS) {
           nrt_pipeline
               << "v4l2src name=dms_camera device=/dev/dms_h264 ! "
               << "video/x-h264, stream-format=(string)byte-stream ! nvv4l2decoder ! "
               << "video/x-raw(memory:NVMM), format=NV12, width=" << context->width << ", height=" << context->height << ", framerate=" << nrt_fps << "/1 ! "
               << "tee name=t1 ! queue name=queue_nrt leaky=2 max-size-buffers=0 max-size-bytes=0 max-size-time=4000000000 ! "
               << "nvv4l2h265enc name=h265enc control-rate=0 insert-sps-pps=true insert-vui=true bitrate=" << nrt_br << " peak-bitrate=" << nrt_br << " "
               << "preset-level=3 maxperf-enable=1 idrinterval=0 iframeinterval=" << i_interval[context->cam_pos] << " ! "
               << "h265parse ! queue name=queue_appsink_nrt leaky=2 max-size-buffers=0 max-size-bytes=0 max-size-time=4000000000 ! "
               << "appsink name=" << nrt_appsink_name << " emit-signals=TRUE sync=FALSE";

           pipeline << nrt_pipeline.str();

           if (context->ld_enabled) {
               nrt_ld_pipeline
                  << " t1. ! queue name=queue_tc leaky=2 max-size-buffers=0 max-size-bytes=0 max-size-time=4000000000 ! "
                  << "nvvidconv ! video/x-raw(memory:NVMM), format=I420, width=" << context->nrt_ld_dms_config.width << ", height=" << context->nrt_ld_dms_config.height << " ! "
                  << "nvv4l2h265enc name=h265enc_ld control-rate=0 insert-sps-pps=true insert-vui=true bitrate=" << context->nrt_ld_dms_config.bitrate << " peak-bitrate=" << context->nrt_ld_dms_config.bitrate << " preset-level=3 maxperf-enable=1 idrinterval=0 iframeinterval=30 ! "
                  << "h265parse ! appsink name=appsink_dms_ld emit-signals=TRUE sync=FALSE";

               pipeline << nrt_ld_pipeline.str();
           }
       } else {
           if (nd_device_obj->is_sensor_mode_supported()) {
               nrt_pipeline << "nvarguscamerasrc name=" << camera_name << " sensor-id=" << sensor_id << " sensor-mode=" << sensor_mode << " ! ";
          } else {
              nrt_pipeline << "nvarguscamerasrc name=" << camera_name << " sensor-id=" << sensor_id << " ! ";
          }

          nrt_pipeline
              << "video/x-raw(memory:NVMM), format=NV12, width=" << context->width << ", height=" << context->height << ", framerate=" << nrt_fps << "/1 ! "
              << "tee name=t1 ! queue name=queue_nrt leaky=2 max-size-buffers=0 max-size-bytes=0 max-size-time=4000000000 ! "
              << "videorate name=vrconv_nrt max-rate=" << nrt_fps << " ! "
              << "nvv4l2h265enc name=h265enc control-rate=0 insert-sps-pps=true insert-vui=true bitrate=" << nrt_br << " peak-bitrate=" << nrt_br << " "
              << "preset-level=3 maxperf-enable=1 idrinterval=0 iframeinterval=" << i_interval[context->cam_pos] << " ! "
              << "h265parse ! appsink name=" << nrt_appsink_name << " emit-signals=TRUE sync=FALSE";

          pipeline << nrt_pipeline.str();

          if (context->ld_enabled) {
	          nrt_ld_pipeline
                  << " t1. ! queue name=queue_tc leaky=2 max-size-buffers=0 max-size-bytes=0 max-size-time=4000000000 ! "
                  << "nvvidconv ! video/x-raw(memory:NVMM), format=I420, width=" << context->nrt_ld_inward_config.width << ", height=" << context->nrt_ld_inward_config.height << " ! "
                  << "videorate name=vrconv_tc max-rate=" << nrt_fps << " ! "
                  << "nvv4l2h265enc name=h265enc_ld control-rate=0 insert-sps-pps=true insert-vui=true bitrate=" << context->nrt_ld_inward_config.bitrate << " peak-bitrate=" << context->nrt_ld_inward_config.bitrate << " preset-level=3 maxperf-enable=1 idrinterval=0 iframeinterval=15 ! "
                  << "h265parse ! appsink name=appsink_inward_ld emit-signals=TRUE sync=FALSE";

                pipeline << nrt_ld_pipeline.str();
          }
       }

	   if (context->rt_config.enable_streaming == true) {
           if (context->cam_pos == CAMERA_POSITION_DMS) {
              rt_pipeline
                  << " t1. ! queue name=queue_rt leaky=2 max-size-buffers=0 max-size-bytes=0 max-size-time=3000000000 ! "
                  << "appsink name=appsink_rt emit-signals=TRUE max-buffers=3 async=FALSE drop=TRUE";
           } else {
              rt_pipeline
                  << " t1. ! queue name=queue_rt leaky=2 max-size-buffers=0 max-size-bytes=0 max-size-time=3000000000 ! "
                  << "nvvidconv output-buffers=20 ! "
                  << "video/x-raw, format=NV12, width=" << context->analytics_width_driv << ", height=" << context->analytics_height_driv << " ! "
                  << "queue name=queue_appsink leaky=2 max-size-buffers=0 max-size-bytes=0 max-size-time=1000000000 ! "
                  << "appsink name=appsink_rt emit-signals=TRUE max-buffers=3 async=FALSE drop=TRUE";
           }

           pipeline << rt_pipeline.str();
	   }

       /* Live streaming is not there for DMS */
       if (g_live_streaming == true && context->cam_pos == CAMERA_POSITION_BACK) {
           kinesis_pipeline
               << " t1. ! queue name=queue_kinesis leaky=2 max-size-buffers=0 max-size-bytes=0 max-size-time=1000000000 ! "
               << "nvvidconv ! video/x-raw(memory:NVMM), format=NV12, width=" << live_streaming_param.width << ", height=" << live_streaming_param.height << " ! "
               << "videorate max-rate=" << live_streaming_param.fps << " ! "
               << "nvv4l2h264enc control-rate=0 bitrate=" << live_streaming_param.bitrate << " peak-bitrate=" << live_streaming_param.bitrate << " preset-level=3 maxperf-enable=1 insert-sps-pps=true "
               << "iframeinterval=" << live_streaming_param.iframeinterval << " idrinterval=" << live_streaming_param.iframeinterval << " ! "
               << "h264parse ! video/x-h264, stream-format=byte-stream ! "
               << "queue name=queue_appsink_kinesis leaky=2 max-size-buffers=0 max-size-bytes=0 max-size-time=1000000000 ! "
               << "appsink name=appsink_kinesis emit-signals=TRUE sync=FALSE";

           pipeline << kinesis_pipeline.str();
       }

	   if ((is_driverlogin_qr_enabled == true) && (context->cam_pos == CAMERA_POSITION_BACK)) {
           qr_pipeline
               << " t1. ! queue name=queue_qr leaky=2 max-size-buffers=0 max-size-bytes=0 max-size-time=3000000000 ! "
               << "nvvidconv ! "
               << "video/x-raw, format=GRAY8, width=" << qrscan_buffer_width << ", height=" << qrscan_buffer_height << " ! "
               << "appsink name=appsink_qr emit-signals=TRUE max-buffers=3 async=FALSE drop=TRUE";

           pipeline << qr_pipeline.str();
	   }

	   printf("For %s, using launch string: %s\n", camera_name, pipeline.str().c_str());

	   context->pipeline = gst_parse_launch (pipeline.str().c_str(), &error);
	   if (!context->pipeline) {
		   LOG_E(log_tag[context->cam_pos], "Pipeline creation failed with parse error: %s", error->message);
		   return FALSE;
	   }
   }

   if (context->rt_config.enable_streaming == true) {
       configure_RT_pipeline(context);

       elem = gst_bin_get_by_name(GST_BIN(context->pipeline), "appsink_rt");
       if (context->cam_pos == CAMERA_POSITION_BACK)
           g_signal_connect (elem, "new-sample", G_CALLBACK (appsink_driverfacing_cb), context);
#ifdef DMS_CAMERA_SUPPORTED
       else if (context->cam_pos == CAMERA_POSITION_DMS)
           g_signal_connect (elem, "new-sample", G_CALLBACK (appsink_driverfacing_dms_cb), context);
#endif
       g_object_unref (elem);
   }

   if (context->ld_enabled) {
	   configure_transcode_pipeline(context);
   }

   /* configure appsink for live stream */
   if (g_live_streaming == true && context->cam_pos == CAMERA_POSITION_BACK) {
       GstElement *elem_kinesis = gst_bin_get_by_name(GST_BIN(context->pipeline), "appsink_kinesis");
       g_signal_connect(elem_kinesis, "new-sample", G_CALLBACK (livestreaming_cb), context);
       g_object_unref (elem_kinesis);
   }

   if ((is_driverlogin_qr_enabled == true) && (context->cam_pos == CAMERA_POSITION_BACK)) {
        elem = gst_bin_get_by_name(GST_BIN(context->pipeline), "queue_qr");
        gst_pad_add_probe(gst_element_get_static_pad (elem, "sink"), GST_PAD_PROBE_TYPE_BUFFER,
                (GstPadProbeCallback) queue_qr_callback, context, NULL);
        g_object_unref (elem);

       elem = gst_bin_get_by_name(GST_BIN(context->pipeline), "appsink_qr");
       g_signal_connect(elem, "new-sample", G_CALLBACK (appsink_qr_cb), context);
       g_object_unref (elem);
   }

   configure_NRT_pipeline(context);

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

            LOG_E (log_tag[context->cam_pos], "Error received from element %s: %s",  GST_OBJECT_NAME (msg->src), err->message);
            LOG_E (log_tag[context->cam_pos], "Debugging information: %s", debug_info ? debug_info : "none");

            if (!strcmp(err->message, "DISCONNECTED") || !strcmp(err->message, "TIMEOUT") || !strcmp(err->message, "CANCELLED")) {
                if (!strcmp(GST_OBJECT_NAME (msg->src), "inward_camera")) {
                    LOG_I (log_tag[context->cam_pos], "Setting cam crash status");
                    cam_crash_bus_status[CAMERA_POSITION_BACK] = true;
                } else if (!strcmp(GST_OBJECT_NAME (msg->src), "left_camera")) {
                    LOG_I (log_tag[context->cam_pos], "Setting cam crash status");
                    cam_crash_bus_status[CAMERA_POSITION_LEFT] = true;
                } else if (!strcmp(GST_OBJECT_NAME (msg->src), "right_camera")) {
                    LOG_I (log_tag[context->cam_pos], "Setting cam crash status");
                    cam_crash_bus_status[CAMERA_POSITION_RIGHT] = true;
                } else if (!strcmp(GST_OBJECT_NAME (msg->src), "dms_camera")) {
                    LOG_I (log_tag[context->cam_pos], "Setting cam crash status");
                    cam_crash_bus_status[CAMERA_POSITION_DMS] = true;
                }
            }
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

static void init_qr_scan()
{
    Config_parser bagheera_config_parser(BAGHEERACONFIG_INI);
    bool is_val_overridden = false;

    if (bagheera_config_parser.getParseStatus()) {
        if ("true" == bagheera_config_parser.getConfig("driverlogin_v2", "qr_enabled", "false", true, is_val_overridden))
            is_driverlogin_qr_enabled = true;

        if (is_driverlogin_qr_enabled == true) {
            LOG_I("QR_SCAN", "Driver login through QR code scanning is enabled");

            string temp = bagheera_config_parser.getConfig("driverlogin_v2", "max_qr_scan_duration", "3600", true, is_val_overridden);
            string_to_integer(temp.c_str(), max_scan_duration); // maximum scan duration
            
            if ("true" == bagheera_config_parser.getConfig("driverlogin_v2", "enable_invalid_qr_image_dump", "false", true, is_val_overridden)) {
                LOG_I("QR_SCAN", "Invalid QR image dump is enabled");
                invalid_qr_image_dump_enabled = true;
            } else {
                LOG_I("QR_SCAN", "Invalid QR image dump is disabled");
                invalid_qr_image_dump_enabled = false;
            }

            pthread_mutex_init(&qr_scan_mutex, NULL);
            pthread_cond_init(&qr_scan_cond, NULL);

            Config_parser sock_addr_config(ND_SOCKET_INI);
            qr_messenger_socket = sock_addr_config.getConfig("messenger_sockets", "qr_ndcentral", "", true, is_val_overridden);
            qr_messenger_topic = sock_addr_config.getConfig("messenger_topics", "qr_ndcentral", "", true, is_val_overridden);
            qr_scan_data_publisher.setServer(qr_messenger_socket);
            qr_scan_data_publisher.setTopic(qr_messenger_topic);

            LOG_I("QR_SCAN", "%s, %s", qr_messenger_socket.c_str(), qr_messenger_topic.c_str() );

            QR_MIN_INTRA_FRAME_INTERVAL_NS = ((1000 / qr_scan_fps) * 1000 * 1000) - ((1000 / (30 * 2)) * 1000 * 1000);
        } else {
            LOG_I("QR_SCAN", "Driver login through QR code scanning is disabled");
        }
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

   // read QR scan config info from bagheera config
   if (context->cam_pos == CAMERA_POSITION_BACK)
       init_qr_scan();

   if (!create_other_camera_pipeline(context))
	   goto END;

   bus = gst_pipeline_get_bus (GST_PIPELINE (context->pipeline));
   gst_bus_add_signal_watch (bus);
   gst_bus_set_sync_handler (bus, gst_bus_sync_signal_handler, context, NULL);
   g_object_connect (bus, "signal::sync-message", G_CALLBACK (cb_bus_message), context, NULL);

   gst_object_unref (bus);

   LOG_I(log_tag[context->cam_pos], "Setting pipeline to PAUSED ...");

   ret = gst_element_set_state (context->pipeline, GST_STATE_PAUSED);

   switch (ret) {
    case GST_STATE_CHANGE_FAILURE:
        LOG_C(log_tag[context->cam_pos], "ERROR: Pipeline doesn't want to pause.");
        break;

    case GST_STATE_CHANGE_ASYNC:
        LOG_C(log_tag[context->cam_pos], "Pipeline is PREROLLING ...");
        break;

	case GST_STATE_CHANGE_NO_PREROLL:
	    LOG_I(log_tag[context->cam_pos], "Pipeline is live and does not need PREROLL ...");
        result = TRUE;
	    break;

	case GST_STATE_CHANGE_SUCCESS:
	    LOG_I(log_tag[context->cam_pos], "Pipeline is PREROLLED ...");
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

   if (NULL == context)
        return NULL;

   if (!gst_init_check(NULL,NULL,&err)) {
        LOG_C(log_tag[context->cam_pos],"gstreamer init failed %s",err->message);
        return NULL;
   }
   NDDeviceTypeT type = ND_DeviceFactory::getBuildDeviceType();
   if((NDDeviceTypeT::bagheera2 == type) || (NDDeviceTypeT::bagheera3 == type)) {
       int n_cpus = get_nprocs();
       int n_cpus_conf = get_nprocs_conf();
       LOG_I(log_tag[context->cam_pos],"n_cpus : %d , n_cpus_conf : %d", n_cpus, n_cpus_conf);
       if(n_cpus_conf == n_cpus){

           if ((CAMERA_POSITION_DMS == context->cam_pos) || (CAMERA_POSITION_LEFT == context->cam_pos) || (CAMERA_POSITION_RIGHT == context->cam_pos)) {
               cpu_set_t cpuset; //the CPU we want to use
               CPU_ZERO(&cpuset);//clears the cpuset
               vector<int> cores_to_be_set = {CPU_CORE_3};
               for(int i = 0; i < cores_to_be_set.size(); i++){
                   CPU_SET(cores_to_be_set[i], &cpuset);
               }
               pid_t threadID = syscall(SYS_gettid);
               /*
                * sched_setaffinity sets cpu affinity for the calling thread
                * first parameter is the pid, 0 = calling thread
                * second parameter is the size of your cpuset
                * third param is the cpuset in which your thread will be placed
                */
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
                   cam_record_service_obj->send_err_msg(SM_E_CPU_CORE_ERROR, context->cam_pos, "Failed to set affinity for camera");
               }

           } else if(CAMERA_POSITION_BACK == context->cam_pos){
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
                   cam_record_service_obj->send_err_msg(SM_E_CPU_CORE_ERROR, context->cam_pos, "Failed to set affinity for camera");
               }
           }
       } else {
           string err_msg = "Cores available don't match cores online. n_cpus : " + to_string(n_cpus) + " n_cpus_conf : " + to_string(n_cpus_conf);
           cam_record_service_obj->send_err_msg(SM_E_CPU_CORE_ERROR, context->cam_pos, err_msg);

       }
   }

   while ((msg = (pipeline_state *)g_async_queue_pop((GAsyncQueue *)context->queue)) != NULL) {
        result = TRUE;
        isExit = FALSE;

        g_mutex_lock(&context->mutex);

        switch (*msg) {
            case STATE_INIT:
            	if (!isExit) {
            		LOG_I(log_tag[context->cam_pos],"Initialising pipeline ..");
            		if (create_record_pipeline_initialize(context)) {
            			LOG_I(log_tag[context->cam_pos],"Succesfully Initialised pipeline ..");
            			context->state = STATE_INIT;
		            } else {
		                result = FALSE;
		                LOG_E(log_tag[context->cam_pos],"Failed in initialising pipeline ..");
		            }
                }
                context->cond_var = 1;
                g_cond_signal(&context->cond);
                break;

            case STATE_PLAYING:
            	if (!isExit) {
                    LOG_I(log_tag[context->cam_pos],"PLAYING pipeline ..");
                    ret = gst_element_set_state (context->pipeline, GST_STATE_PLAYING);
                    if (ret == GST_STATE_CHANGE_FAILURE) {
                        LOG_I(log_tag[context->cam_pos],"Failed to set PLAYING ..");
                        result = FALSE;
                        break;
                    }

                    context->state = STATE_PLAYING;
                }
                break;

            case STATE_STOP:
            	if (!isExit) {
                    LOG_I(log_tag[context->cam_pos],"Stopping pipeline ..");
                    context->state = STATE_STOP;
                 }
                 isExit = TRUE;
                 break;
        }

        if (isExit)
            context->state = STATE_EXIT;

        g_mutex_unlock(&context->mutex);

        if (isExit) {
            LOG_I(log_tag[context->cam_pos],"Breaking out of msg queue check");
            isExit = false;
            break;
        }
    }

   LOG_I(log_tag[context->cam_pos],"cleaning camera context");

   cleanup_gstreamer_context(context);

   LOG_I(log_tag[context->cam_pos],"Gst thread exiting");

   g_thread_exit(NULL);

   return NULL;
}

#ifdef DMS_CAMERA_SUPPORTED
// Function to set socket to non-blocking mode
void set_socket_non_blocking(int sockfd)
{
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags == -1) {
        LOG_E("socket_setup", "fcntl F_GETFL api call failed with error: %s", strerror(errno));
        return;
    }
    if (fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) == -1) {
        LOG_E("socket_setup", "fcntl F_SETFL api call failed with error: %s", strerror(errno));
        return;
    }
}

/* Setting up the unix domain socket to interact with bagheera service */
void *socket_setup(void *arg)
{
    struct sockaddr_un addr;

    // Remove the socket file if it exists
    unlink(SOCKET_PATH);

    // Create the server socket
    sfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sfd == -1) {
        LOG_E("socket_setup", "socket api call failed with error: %s", strerror(errno));
        return NULL;
    }

    // Set up the server address structure
    memset(&addr, 0, sizeof(struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    // Bind the socket
    if (bind(sfd, (struct sockaddr *)&addr, sizeof(struct sockaddr_un)) == -1) {
        LOG_E("socket_setup", "bind api call failed with error: %s", strerror(errno));
        close(sfd);
        return NULL;
    }

    // Start listening on the socket
    if (listen(sfd, 1) == -1) {
        LOG_E("socket_setup", "listen api call failed with error: %s", strerror(errno));
        close(sfd);
        return NULL;
    }

    // Set server socket to non-blocking mode
    set_socket_non_blocking(sfd);

    // Polling setup
    struct pollfd fds[1];
    fds[0].fd = sfd;
    fds[0].events = POLLIN;  // Server socket is for accepting connections

    while (1) {
        LOG_I("socket_setup", "Polling the server socket %d for accepting connection from bagheera service", sfd);
        int ret = poll(fds, 1, -1);  // -1 means infinite timeout
        if (ret < 0) {
            LOG_E("socket_setup", "poll api call failed with error: %s", strerror(errno));
            close(sfd);
            return NULL;
        }

        // Check if the server socket is ready to accept new connections
        if (fds[0].revents & POLLIN) {
            cfd = accept(sfd, NULL, NULL);
            if (cfd < 0) {
                LOG_E("socket_setup", "accept api call failed with error: %s", strerror(errno));
                continue;
            }
            set_socket_non_blocking(cfd);
            is_bagheera_connected = true;
            LOG_I("socket_setup", "bagheera service is connected to cam_rec service over the socket with client_fd %d", cfd);
            return NULL;
        }
    }
}

void handle_bagheera_disconnection(int client_fd)
{
    close(client_fd);
    close(sfd);
    is_bagheera_connected = false;

    /* Set up a Unix Domain socket to accept connection from bagheera service*/
    LOG_I(TAG, "Setting a unix domain socket for accepting connection from bagheera service");
    pthread_t socket_setup_th;
    if (pthread_create(&socket_setup_th, NULL, socket_setup, NULL) == 0) {
        LOG_I(TAG, "Thread created : socket_setup");
        // Detach the thread to release resources automatically after it finishes
        pthread_detach(socket_setup_th);
    } else {
        LOG_E(TAG, "Failed to create thread : socket_setup");
    }
}
#endif

int start_record_session_platform(int cam_pos)
{
    int ret = FALSE;

    cam_record_ctxt *context = &cam_rec_ctx[cam_pos];

    if (NULL == context)
         return FALSE;

    g_mutex_lock(&context->mutex);

    if (STATE_INIT == context->state) {

        if (cam_check_thread_created == false) {
            if (!pthread_create (&cam_check, NULL, cam_check_thread, NULL))
                cam_check_thread_created = true;
            else
                LOG_E (log_tag[context->cam_pos], "cam_check_thread creation failed");
        }

        context->queue_msg = (int)STATE_PLAYING;

        LOG_D(log_tag[context->cam_pos],"Posting start PLAYING msg to queue");

        g_async_queue_push (context->queue,&(context->queue_msg));

        ret = TRUE;
    }

    g_mutex_unlock(&context->mutex);

    return ret;
}

int stop_record_session_platform (int cam_pos)
{
    int ret = FALSE;

    cam_record_ctxt *context = &cam_rec_ctx[cam_pos];

    if (NULL == context)
         return FALSE;

    LOG_I(log_tag[context->cam_pos],"stop camera record");

    if (CAMERA_POSITION_DMS == context->cam_pos) {
        LOG_I(log_tag[context->cam_pos],"Setting is_dms_streaming_started to false");
        is_dms_streaming_started = false;
    }

    g_mutex_lock(&context->mutex);

    if ((context->state != STATE_UNINITIALIZED) && (context->state != STATE_ERROR)
        && (context->state != STATE_EXIT)) {

        context->queue_msg = (int)STATE_STOP;

        LOG_I(log_tag[context->cam_pos],"Posting STOP msg to queue");

        g_async_queue_push (context->queue,&(context->queue_msg));
    }
    g_mutex_unlock(&context->mutex);

    LOG_I(log_tag[context->cam_pos],"Waiting for main thread to exit");

    g_thread_join(context->thread);

    LOG_I(log_tag[context->cam_pos],"STOP msg handling done");

    return TRUE;
}

void cam_record_service_exit (void)
{
    //Only if main thread PID matches print the message
    if( main_thread_pid == getpid() ) {
        LOG_E (TAG, "Camera recording service exited, this shouldn't happen");
        return;
    }
}

#ifdef DMS_CAMERA_SUPPORTED
/* Fill up the DMS camera RT config data structure using bag_config, nd_config and bag_override */
int fill_dms_rt_config(Config_parser* bag_conf, Config_parser* nd_config, realtime_camera_config_t* rt_config)
{
    bool get_override_val = true;
    bool is_val_overridden = false;
    int dms_cam_rt_streaming_enabled = 0;

    memset(rt_config, 0x00, sizeof(realtime_camera_config_t));

    string streamin_flag = bag_conf->getConfig("streaming", "enable_streaming", "true");
    string_to_integer(nd_config->getConfig("dms_drowsy", "enabled", "0", get_override_val, is_val_overridden), dms_cam_rt_streaming_enabled);

    LOG_I(TAG, "bag_conf :: streaming_flag %s, dms_streaming_flag %d", streamin_flag.c_str(), dms_cam_rt_streaming_enabled);

    if (streamin_flag == "false") {
        LOG_E(TAG, "streamin_flag == false");
        return -1;
    }
    if (!dms_cam_rt_streaming_enabled) {
        LOG_E(TAG, "dms_streamin_flag == false");
        return 0;
    }
    rt_config->enable_streaming = true;

    if (file_is_present("/dev/shm/dms_late_connect")) {
        rt_config->enable_streaming = false;
        LOG_I(TAG, "/dev/shm/dms_late_connect file present, setting DMS enable_streaming to false to force only NRT pipeline");
    }

    get_override_val = true;
    is_val_overridden = false;

    string_to_integer(nd_config->getConfig("dms", "width", "1296", get_override_val, is_val_overridden), rt_config->width);
    string_to_integer(nd_config->getConfig("dms", "height", "1296", get_override_val, is_val_overridden), rt_config->height);

    int frame_rate = 30;
    string_to_integer(nd_config->getConfig("dms", "frame_rate", "30", get_override_val, is_val_overridden), frame_rate);

    int subsample_factor = 3;
    string_to_integer(nd_config->getConfig("dms_drowsy", "subsample_factor", "3", get_override_val, is_val_overridden), subsample_factor);

    LOG_I(TAG, "dms_nrt_fps: %d, dms_rt_subsample_factor: %d", frame_rate, subsample_factor);

    rt_config->fps = (frame_rate / subsample_factor);

    LOG_I(TAG, "dms_rt_width: %d, dms_rt_height: %d, dms_rt_fps: %d", rt_config->width, rt_config->height, rt_config->fps);

    return 0;
}
#endif

/* Fill up the inward camera RT config data structure using bag_config, nd_config and bag_override */
int fill_inward_rt_config(Config_parser* bag_conf, Config_parser* nd_config, realtime_camera_config_t* rt_config)
{
    bool get_override_val = true;
    bool is_val_overridden = false;
    int drowsy_enabled = 0;

    memset(rt_config, 0x00, sizeof(realtime_camera_config_t));

    string streamin_flag = bag_conf->getConfig("streaming", "enable_streaming", "true");
    string inward_streamin_flag = bag_conf->getConfig("streaming", "inwardcam_streaming", "false", get_override_val, is_val_overridden);

    LOG_I(TAG, "bag_conf :: streaming_flag %s, inward_streaming_flag %s", streamin_flag.c_str(), inward_streamin_flag.c_str());

    if (streamin_flag == "false") {
        LOG_E(TAG, "streamin_flag == false");
        return -1;
    }
    if (inward_streamin_flag == "false") {
        LOG_E(TAG, "inward_streamin_flag == false");
        return 0;
    }
    rt_config->enable_streaming = true;

    get_override_val = true;
    is_val_overridden = false;

    string_to_integer(nd_config->getConfig("drowsy", "enabled", "0",
                        get_override_val, is_val_overridden), drowsy_enabled);

#ifdef DMS_CAMERA_SUPPORTED
    int dms_cam_rt_streaming_enabled = 0;
    string_to_integer(nd_config->getConfig("dms_drowsy", "enabled", "0", get_override_val, is_val_overridden), dms_cam_rt_streaming_enabled);

    if (dms_cam_rt_streaming_enabled) {
        ifstream dms_connection_status_file_fd(dms_connection_status_file);
        if (!dms_connection_status_file_fd) {
            LOG_E(TAG, "error opening %s file", dms_connection_status_file.c_str());
        } else {
            string line;
            getline(dms_connection_status_file_fd, line);
            if (line == "0") {
                LOG_E(TAG, "DMS is enabled and Disconnected: Fallback to ADD");
                drowsy_enabled = 1;
            } else if (line == "1") {
                LOG_E(TAG, "DMS is enabled and Connected: Disable ADD");
                drowsy_enabled = 0;
            }
        }
    }
#endif

    if (drowsy_enabled) {
        string_to_integer(nd_config->getConfig("inwardcam_rt_streaming", "width_drowsy", "1920",
                    get_override_val, is_val_overridden), rt_config->width);
        string_to_integer(nd_config->getConfig("inwardcam_rt_streaming", "height_drowsy", "1080",
                    get_override_val, is_val_overridden), rt_config->height);

        int frame_rate = 15;
        string_to_integer(nd_config->getConfig("inwardRealTime", "frame_rate", "15", get_override_val, is_val_overridden), frame_rate);
        int subsample_factor_drowsy = 1;
        string_to_integer(nd_config->getConfig("inwardRealTime", "subsample_factor_drowsy", "1", get_override_val, is_val_overridden), subsample_factor_drowsy);

        LOG_I(TAG, "INWARD_RT frame_rate: %d, INWARD_RT subsample_factor_drowsy: %d", frame_rate, subsample_factor_drowsy);

        rt_config->fps = (frame_rate / subsample_factor_drowsy);
    } else {
        string_to_integer(nd_config->getConfig("inwardcam_rt_streaming", "width", "640",
                    get_override_val, is_val_overridden), rt_config->width);
        string_to_integer(nd_config->getConfig("inwardcam_rt_streaming", "height", "360",
                    get_override_val, is_val_overridden), rt_config->height);
        rt_config->fps = 5;
    }

    LOG_I(TAG, "inward_rt_width: %d, inward_rt_height: %d, inward_rt_fps: %d", rt_config->width, rt_config->height, rt_config->fps);

    return 0;
}

/* Set up the ZMQ socket for communicating the rt camera
 * raw content from cam_rec service to bagheera service
 */
bool cam_rec_zmq_init(camera_pos cam_pos, realtime_camera_config_t* rt_config)
{
    int max_q_sz = MAX_PUBLISHER_Q_SZ;
    int linger_val = 100 ; // millisecond
    int trial_count = 0, rc = -1;

    if (cam_pos == CAMERA_POSITION_BACK) {
        string socket = ZMQ_SOCKET_IN_RT;
        zmq_context = zmq_ctx_new();
        if (zmq_context == NULL) {
            LOG_E(TAG, "zmq_context == NULL; errno: %d, zmq_error: %s", errno, zmq_strerror(zmq_errno()));
            return false;
        }

        zmq_publisher = zmq_socket(zmq_context, ZMQ_PUB);
        if (zmq_publisher == NULL) {
            LOG_E(TAG, "zmq_publisher == NULL; errno: %d, zmq_error: %s", errno, zmq_strerror(zmq_errno()));
            return false;
        }

        if (zmq_setsockopt(zmq_publisher, ZMQ_SNDHWM, &max_q_sz, sizeof(int))) {
            LOG_E(TAG, "zmq_setsockopt ZMQ_SNDHWM failed; errno: %d, zmq_error: %s", errno, zmq_strerror(zmq_errno()));
            return false;
        }
        if (zmq_setsockopt(zmq_publisher, ZMQ_LINGER, &linger_val, sizeof(int))) {
            LOG_E(TAG, "zmq_setsockopt ZMQ_LINGER failed; errno: %d, zmq_error: %s", errno, zmq_strerror(zmq_errno()));
            return false;
        }

        strncpy(&rt_config->socket[0], socket.c_str(), MAX_SOCKET_NAME_LEN);

        do {
            trial_count++;
            usleep(100*1000); // sleep for 100MS before binding : can this avoid FAILED TO CREATE with err 98 ?
            rc = zmq_bind(zmq_publisher, rt_config->socket);
            if (rc == -1) {
                LOG_E(TAG, "FAILED TO CREATE zmq_socket %d; errno: %d, zmq_error: %s", rc, errno, zmq_strerror(zmq_errno()));
                rt_config->enable_streaming = false;
            } else if (rc == 0) {
                LOG_I(TAG, "Successfully created ZMQ_SOCKET_IN_RT");
                rt_config->enable_streaming = true;
                break;
            }
        } while(trial_count < 5);

        if (strncmp(rt_config->socket, "ipc://", sizeof("ipc://") - 1) == 0) {
            LOG_I(TAG, "chmod for file %s" , rt_config->socket + sizeof("ipc://") - 1);

            if (chmod(rt_config->socket + sizeof("ipc://") - 1 , 0777) < 0) {
                LOG_E(TAG, "Could not create socket permissions %s", strerror(errno));
            }
        }

        if (rc != 0) {
            LOG_C(TAG, "after %d retries FAILED TO CREATE zmq_socket rc: %d ", trial_count, rc);

            zmq_close (zmq_publisher);
            zmq_ctx_destroy (zmq_context);
            zmq_publisher = NULL;
            zmq_context = NULL;

            return false;
        }
        LOG_I(TAG, "ZMQ publisher for cam_rec service created successfully, send message to bagheera service");
        send_zmqpub_create_msg_to_bagheera(Q_NAME, cam_pos);
    }

    return true;
}

static bool get_inward_boot_status_tt (void *args)
{
    FILE *fp = NULL;
    char buffer[10] = {'\0'};
    string boot_status = "";

    //command to set the boot status value refer BGR2-628
    fp = popen (inward_boot_status_set_cmd.c_str(), "r");
    if (!fp) {
        LOG_E (TAG,"Failed to execute %s", inward_boot_status_cmd.c_str());
        return false;
    }
    pclose (fp);
    usleep (CAMERA_RESET_DURATION);

    //command to get the boot status value refer BGR2-628
    fp = popen (inward_boot_status_cmd.c_str(), "r");
    if (!fp) {
        LOG_E (TAG,"Failed to execute %s", inward_boot_status_cmd.c_str());
        return false;
    }
    if (fgets (buffer, sizeof (buffer), fp)) {
        boot_status = string (buffer);
        LOG_D (TAG, "boot_status: %s", boot_status.c_str());
        //Remove any surrounding whitespaces
        remove_leading_trailing_spaces(boot_status);
    }
    pclose (fp);

    //Status value should match with 2 values 0x27 and 0x35
    //so adding both the check refer BGR2-628
    if ((boot_status.find("0x27") != string::npos))
    {
	    LOG_E (TAG,"Checking 2nd status");
	    fp = popen (inward_boot_status2_cmd.c_str(), "r");
	    if (!fp) {
		    LOG_E (TAG,"Failed to execute %s", inward_boot_status2_cmd.c_str());
		    return false;
	    }
	    if (fgets (buffer, sizeof (buffer), fp)) {
		    boot_status = string (buffer);
		    LOG_D (TAG, "boot_status: %s", boot_status.c_str());
		    //Remove any surrounding whitespaces
		    remove_leading_trailing_spaces(boot_status);
	    }
	    pclose (fp);
	    return (boot_status.find("0x35") != string::npos);
    }
    return false;
}

static bool get_inward_boot_status()
{
    task_result_t task_result = nd_timed_task(get_inward_boot_status_tt,
                        CAM_BOOT_STATUS_CHECK_TIMEOUT, NULL, "inward cam boot status");
    if (task_result == TASK_TIMEOUT) {
        LOG_E (TAG, "nd_timed_task for get_inward_boot_status_tt timedout");
        cam_record_service_obj->send_err_msg(SM_E_NDC_CAM_STATUS_FAIL, 1, "get_inward_boot_status_tt timedout");
    }
    LOG_I(TAG, "get_inward_boot_status_tt return status %d", task_result);
    return (task_result == TASK_SUCCESS);
}

static bool get_left_cam_boot_status_tt (void *args)
{
    FILE *fp = NULL;
    char buffer[10] = {'\0'};
    string boot_status = "";

    fp = popen (left_cam_boot_status_cmd.c_str(), "r");
    if (!fp) {
        LOG_E (TAG,"Failed to execute %s", left_cam_boot_status_cmd.c_str());
        return false;
    }
    if (fgets (buffer, sizeof (buffer), fp)) {
        boot_status = string (buffer);
        LOG_D (TAG, "boot_status: %s", boot_status.c_str());
        //Remove any surrounding whitespaces
        remove_leading_trailing_spaces(boot_status);
    }
    pclose (fp);
    if (boot_status.find("0x97") != string::npos)
    {
	    LOG_E (TAG,"Checking 2nd status");
	    fp = popen (left_cam_boot_status2_cmd.c_str(), "r");
	    if (!fp) {
		    LOG_E (TAG,"Failed to execute %s", left_cam_boot_status_cmd.c_str());
		    return false;
	    }
	    if (fgets (buffer, sizeof (buffer), fp)) {
		    boot_status = string (buffer);
		    LOG_D (TAG, "boot_status: %s", boot_status.c_str());
		    //Remove any surrounding whitespaces
		    remove_leading_trailing_spaces(boot_status);
	    }
	    pclose (fp);
	    return (boot_status.find("0x32") != string::npos);
    }
    return false;
}

static bool get_left_cam_boot_status()
{
    task_result_t task_result = nd_timed_task(get_left_cam_boot_status_tt,
                        CAM_BOOT_STATUS_CHECK_TIMEOUT, NULL, "side cams boot status");
    if (task_result == TASK_TIMEOUT) {
        LOG_E (TAG, "nd_timed_task for get_left_cam_boot_status_tt timedout");
        cam_record_service_obj->send_err_msg(SM_E_NDC_CAM_STATUS_FAIL, 2, "get_left_cam_boot_status_tt timedout");
    }
    LOG_I(TAG, "get_left_cam_boot_status_tt return status %d", task_result);
    return (task_result == TASK_SUCCESS);
}


static bool get_right_cam_boot_status_tt (void *args)
{
    FILE *fp = NULL;
    char buffer[10] = {'\0'};
    string boot_status = "";

    fp = popen (right_cam_boot_status_cmd.c_str(), "r");
    if (!fp) {
        LOG_E (TAG,"Failed to execute %s", right_cam_boot_status_cmd.c_str());
        return false;
    }
    if (fgets (buffer, sizeof (buffer), fp)) {
        boot_status = string (buffer);
        LOG_D (TAG, "boot_status: %s", boot_status.c_str());
        //Remove any surrounding whitespaces
        remove_leading_trailing_spaces(boot_status);
    }
    pclose (fp);
    if (boot_status.find("0x97") != string::npos)
    {
	    LOG_E (TAG,"Checking 2nd status");
	    fp = popen (right_cam_boot_status2_cmd.c_str(), "r");
	    if (!fp) {
		    LOG_E (TAG,"Failed to execute %s", right_cam_boot_status_cmd.c_str());
		    return false;
	    }
	    if (fgets (buffer, sizeof (buffer), fp)) {
		    boot_status = string (buffer);
		    LOG_D (TAG, "boot_status: %s", boot_status.c_str());
		    //Remove any surrounding whitespaces
		    remove_leading_trailing_spaces(boot_status);
	    }
	    pclose (fp);
	    return (boot_status.find("0x32") != string::npos);
    }
    return false;
}

static void reset_inward()
{
    system_execute("INWARD RESET", inward_cam_reset_cmd);
    usleep (CAMERA_RESET_DURATION);
    system_execute("INWARD OUTOF RESET", inward_cam_oo_reset_cmd);
}

static void reset_left_cam()
{
    system_execute("SIDE CAMS RESET", side_cams_reset_cmd);
    usleep (CAMERA_RESET_DURATION);
    system_execute("SIDE CAMS OUTOF RESET", side_cams_oo_reset_cmd);
}

static bool check_inward_boot_status() {
    bool inward_status = false;
    int count = 0;
    bool boot_status = false;

    do {
        boot_status = get_inward_boot_status();
        if (boot_status == true)
        {
            LOG_I (TAG, "Inward boot status is 1");
            inward_status = true;
            break;
        }
        LOG_E (TAG, "Inward boot status is not 1 - %d", boot_status);
        reset_inward();
        sleep (DELAY_CAMERA_BOOT_STATUS_CHECK);
        count++;
    } while (count < NUM_CAMERA_BOOT_STATUS_RETRY);

    return inward_status;

}

static bool check_left_cam_boot_status()
{
    bool left_cam_status = false;
    int count = 0;
    bool boot_status = false;

    do {
        boot_status = get_left_cam_boot_status();
        if (boot_status == true) {
            LOG_I (TAG, "Side cams boot status is 1");
            left_cam_status = true;
            break;
        }
        LOG_E (TAG, "Side cams boot status is not 1 - %d", boot_status);
        reset_left_cam();
        sleep (DELAY_SIDE_CAMERA_BOOT_STATUS_CHECK);
        count++;
    } while (count < NUM_SIDE_CAMERA_BOOT_STATUS_RETRY);

    return left_cam_status;
}

static bool get_right_cam_boot_status()
{
    task_result_t task_result = nd_timed_task(get_right_cam_boot_status_tt,
                        CAM_BOOT_STATUS_CHECK_TIMEOUT, NULL, "right cams boot status");
    if (task_result == TASK_TIMEOUT) {
        LOG_E (TAG, "nd_timed_task for get_side_cams_boot_status_tt timedout");
        cam_record_service_obj->send_err_msg(SM_E_NDC_CAM_STATUS_FAIL, 3, "get_right_cams_boot_status_tt timedout");
    }
    LOG_I(TAG, "get_right_cams_boot_status_tt return status %d", task_result);
    return (task_result == TASK_SUCCESS);
}

static bool get_right_mipi_retimer_status_tt (void *args)
{
    FILE *fp = NULL;
    char buffer[30] = {'\0'};
    string boot_status = "";
    //for retimers 8 register values has to be checked refer BGR2-628
    string register_values = "44 50 48 59 31 30 30 20";

    fp = popen (right_mipi_status_cmd.c_str(), "r");
    if (!fp) {
        LOG_E (TAG,"Failed to execute %s", right_mipi_status_cmd.c_str());
        return false;
    }
    if (fgets (buffer, sizeof (buffer), fp)) {
        boot_status = string (buffer);
        LOG_D (TAG, "boot_status: %s", boot_status.c_str());
    }
    pclose (fp);
    return (boot_status.find(register_values.c_str()) != string::npos);
}

static bool get_right_mipi_retimer_status()
{
    task_result_t task_result = nd_timed_task(get_right_mipi_retimer_status_tt,
            CAM_BOOT_STATUS_CHECK_TIMEOUT, NULL, "side cams boot status");
    if (task_result == TASK_TIMEOUT) {
        LOG_E (TAG, "nd_timed_task for mipi retimer_status_tt timedout");
        cam_record_service_obj->send_err_msg(SM_E_NDC_CAM_STATUS_FAIL, 3, "get_mipi_retimer_status_tt timedout");
    }
    LOG_I(TAG, "get_mipi_retimer_status_tt return status %d", task_result);
    return (task_result == TASK_SUCCESS);
}

static bool get_left_mipi_retimer_status_tt (void *args)
{
    FILE *fp = NULL;
    char buffer[30] = {'\0'};
    string boot_status = "";
    //for retimers 8 register values has to be checked refer BGR2-628
    string register_values = "44 50 48 59 31 30 30 20";

    fp = popen (left_mipi_status_cmd.c_str(), "r");
    if (!fp) {
        LOG_E (TAG,"Failed to execute %s", left_mipi_status_cmd.c_str());
        return false;
    }
    if (fgets (buffer, sizeof (buffer), fp)) {
        boot_status = string (buffer);
        LOG_D (TAG, "boot_status: %s", boot_status.c_str());
    }
    pclose (fp);
    return (boot_status.find(register_values.c_str()) != string::npos);
}

static bool get_left_mipi_retimer_status()
{
    task_result_t task_result = nd_timed_task(get_left_mipi_retimer_status_tt,
            CAM_BOOT_STATUS_CHECK_TIMEOUT, NULL, "left mipi retimer status");
    if (task_result == TASK_TIMEOUT) {
        LOG_E (TAG, "nd_timed_task for mipi retimer_status_tt timedout");
        cam_record_service_obj->send_err_msg(SM_E_NDC_CAM_STATUS_FAIL, 2, "get_mipi_retimer_status_tt timedout");
    }
    LOG_I(TAG, "get_left_mipi_retimer_status_tt return status %d", task_result);
    return (task_result == TASK_SUCCESS);
}

static void reset_left_mipi_retimer ()
{
    system_execute ("LEFT RETIMER RESET", left_cam_retimer_cmd);
    usleep (CAMERA_RESET_DURATION);
    system_execute ("LEFT RETIMER OUTOF RESET", left_cam_oo_retimer_cmd);
}

static void reset_right_mipi_retimer ()
{
    system_execute ("RIGHT RETIMER RESET", right_cam_retimer_cmd);
    usleep (CAMERA_RESET_DURATION);
    system_execute ("RIGHT RETIMER OUTOF RESET", right_cam_oo_retimer_cmd);
}

static bool check_left_mipi_retimer_status()
{
    bool mipi_retimer_status = false;
    int count = 0;
    bool boot_status = false;

    do {
        boot_status = get_left_mipi_retimer_status();
        if (boot_status == true) {
            LOG_I (TAG, "Side cams boot status is 1");
            mipi_retimer_status = true;
            break;
        }
        LOG_E (TAG, "left mipi retimer status is not 1 - %d", boot_status);
        reset_left_mipi_retimer();
        sleep (DELAY_CAMERA_BOOT_STATUS_CHECK);
        count++;
    } while (count < NUM_CAMERA_BOOT_STATUS_RETRY);

    return mipi_retimer_status;
}

static bool check_right_mipi_retimer_status()
{
    bool mipi_retimer_status = false;
    int count = 0;
    bool boot_status = false;

    do {
        boot_status = get_right_mipi_retimer_status();
        if (boot_status == true) {
            LOG_I (TAG, "Side cams boot status is 1");
            mipi_retimer_status = true;
            break;
        }
        LOG_E (TAG, "right mipi retimer status is not 1 - %d", boot_status);
        reset_right_mipi_retimer();
        sleep (DELAY_CAMERA_BOOT_STATUS_CHECK);
        count++;
    } while (count < NUM_CAMERA_BOOT_STATUS_RETRY);

    return mipi_retimer_status;
}

static void reset_right_cam ()
{
    system_execute ("SIDE CAMS RESET", side_cams_reset_cmd);
    usleep (CAMERA_RESET_DURATION);
    system_execute ("SIDE CAMS OUTOF RESET", side_cams_oo_reset_cmd);
}

static bool check_right_cam_boot_status()
{
    bool right_cam_status = false;
    int count = 0;
    bool boot_status = false;

    do {
        boot_status = get_right_cam_boot_status();
        if (boot_status == true) {
            LOG_I (TAG, "Side cams boot status is 1");
            right_cam_status = true;
            break;
        }
        LOG_E (TAG, "Side cams boot status is not 1 - %d", boot_status);
        reset_right_cam();
        sleep (DELAY_SIDE_CAMERA_BOOT_STATUS_CHECK);
        count++;
    } while (count < NUM_SIDE_CAMERA_BOOT_STATUS_RETRY);

    return right_cam_status;
}

static int check_camera_boot_status ()
{
    int count = 0;
    bool inward_status = false, side_cams_status = false;

    // ret_status[0] - ISP, ret_status[1] - inward, ret_status[2] - side_cams
    int ret_status = 0x0;
#ifdef BAGHEERA
    LOG_I(TAG, "Resetting Camera");
    reset_camera();
#endif
    //sleep (DELAY_CAMERA_BOOT_STATUS_CHECK);

    if ((cams_enabled[1] == false) || ((cams_enabled[1] == true) && nd_device_obj->check_inward_boot_status())) {
        ret_status |= 1<<1;
    }

    if ((cams_enabled[2] == false) || ((cams_enabled[2] == true) && nd_device_obj->check_left_cam_boot_status())) {
        ret_status |= 1<<2;
    }

    if ((cams_enabled[3] == false) || ((cams_enabled[3] == true) && nd_device_obj->check_right_cam_boot_status())) {
        ret_status |= 1<<3;
    }

    if ((cams_enabled[3] == true) && nd_device_obj->check_right_mipi_retimer_status()) {
        ret_status |= 1<<4;
    }
    if ((cams_enabled[2] == true) && nd_device_obj->check_left_mipi_retimer_status()) {
        ret_status |= 1<<5;
    }

    LOG_I (TAG, "Inward and side camera boot status is %d", ret_status);

    return ret_status;
}

static bool isDMSsupportedInDevice(int deviceType)
{
    return (deviceType == eBagheera_3);
}

static bool isDMSconnected()
{
    return file_is_present(DMS_NODE);
}

#ifdef DMS_CAMERA_SUPPORTED
//Thread to make sure DMS pipeline is re-initiated when DMS disconnection + reconnection happens during runtime.
//This thread does not handle the case of DMS being disconnected during bootup, and gets connected for the first time during runtime.
//That case is handled in dms_connection_monitor_thread_func().
static void *check_dms_connection(void *arg) {
    while (1) {
        if (file_is_present(DMS_NODE)) {
            /* Restart the DMS camera pipeline */
            LOG_I(TAG, "DMS camera connected back\n");
            cam_record_service_obj->send_err_msg(SM_E_NDC_CAM_CONNECTED, CAMERA_POSITION_DMS, "DMS camera connected back");
            is_dms_connected = true;

            is_dms_initializing = true;

            is_dms_disconnect_connect_happened = true;
            is_dms_disconnected_and_reconnected = true;
            send_dms_connection_status_to_bagheera(Q_NAME, is_dms_connected);

            g_mutex_lock(&dms_disconnect_connect_mutex);
            deinit_dms_camera();
            if (init_dms_camera() == false) {
                LOG_E(TAG, "DMS camera initialization failed");
                cam_record_service_obj->send_err_msg(SM_E_NDC_DMS_INIT_FAIL, CAMERA_POSITION_DMS, "DMS Initialization Fail: dmsInit camera failed on disconnect-reconnect case");
            }
            LOG_I(TAG, "dmsInit completed.");
            is_dms_initializing = false;
            dms_init_complete_time_us = g_get_monotonic_time();

            g_mutex_unlock(&dms_disconnect_connect_mutex);

            send_split_now_msg_to_bagheera(Q_NAME, cams_enabled, false);
            restart_dms_recording();
            break;
        }
        sleep(1);
    }
    return NULL;
}

//Thread to make sure DMS pipeline is initiated when DMS was disconnected during bootup, and gets connected for the first time during runtime.
static void* dms_connection_monitor_thread_func(void* arg) {
    LOG_I(TAG, "DMS connection monitor thread started");
    while (1) {
        if (!is_dms_connected && file_is_present(DMS_NODE)) {
            /* Start the DMS camera pipeline */
            LOG_I(TAG, "DMS camera connected for the first time after cam_rec service has started.\n");
            cam_record_service_obj->send_err_msg(SM_E_NDC_CAM_CONNECTED, CAMERA_POSITION_DMS, "DMS camera connected for the first time after cam_rec service has started.");

            cams_enabled[CAMERA_POSITION_DMS] = true;
            file_touch("/dev/shm/dms_late_connect");
            is_dms_connected = true;

            is_dms_initializing = true;

            g_mutex_lock(&dms_disconnect_connect_mutex);

            if (init_dms_camera() == false) {
                LOG_E(TAG, "DMS camera initialization failed");
                cam_record_service_obj->send_err_msg(SM_E_NDC_DMS_INIT_FAIL, CAMERA_POSITION_DMS, "DMS Initialization Fail: dmsInit camera failed on first time DMS connection after bootup");
            }
            LOG_I(TAG, "dmsInit completed.");
            is_dms_initializing = false;
            dms_init_complete_time_us = g_get_monotonic_time();

            g_mutex_unlock(&dms_disconnect_connect_mutex);

            if(cams_enabled[CAMERA_POSITION_DMS] == false) {
                LOG_I(TAG, "DMS camera initialization failed, hence not proceeding with DMS recording");
                is_dms_connected = false;
                break;
            }

            send_split_now_msg_to_bagheera(Q_NAME, cams_enabled, false);

            Config_parser bag_conf(BAGHEERACONFIG_INI);
            Config_parser nd_conf_analytics(ND_CONFIG_ANALYTICS);
            realtime_camera_config_t rt_config_dms = {0};

            // Fill DMS RT config
            fill_dms_rt_config(&bag_conf, &nd_conf_analytics, &rt_config_dms);
            // Force only NRT pipeline in this case, because analytics would have started ADD (instead of DMS drowsy) if DMS was not connected during bootup.
            rt_config_dms.enable_streaming = false; 

            // Initialize camera record platform for DMS
            if (!init_camera_record_platform(CAMERA_POSITION_DMS, &rt_config_dms)) {
                LOG_E(TAG, "init_camera_record_platform failed for DMS. Cleaning up.");
                is_dms_connected = false;

                break;
            }

            // Now proceed with session initialization and start
            cam_record_ctxt *context = &cam_rec_ctx[CAMERA_POSITION_DMS];
            context->state = STATE_CREATED;

            if (!init_record_session_platform(CAMERA_POSITION_DMS)) {
                LOG_E(TAG, "init_record_session_platform failed for DMS. Cleaning up.");
                is_dms_connected = false;

                stop_record_session_platform(CAMERA_POSITION_DMS);
                cleanup_gstreamer_context(context);

                break;
            }

            if (!start_record_session_platform(CAMERA_POSITION_DMS)) {
                LOG_E(TAG, "start_record_session_platform failed for DMS. Cleaning up.");
                is_dms_connected = false;

                stop_record_session_platform(CAMERA_POSITION_DMS);
                cleanup_gstreamer_context(context);

                break;
            }

            send_dms_connection_status_to_bagheera(Q_NAME, is_dms_connected);

            break;
        }
        sleep(DMS_CONNECTION_MONITORING_THREAD_SLEEP_TIME_IN_SECS);
    }
    return NULL;
}

static string log_dms_irled_status( const dmsirledinfo_t& dms_irled_status_info, 
                                    int dms_sensor_temperature,
                                    const string& session_filename)
{
    char buf[GSTREAMER_NAME_LENGTH_MAX];
    snprintf(
        buf, sizeof(buf),
        "s:%d,t:%d"
        "[%x,%x,%x,%x,%x]"
        "[%x,%x,%x,%x]"
        "%s",
        dms_irled_status_info.irled_status,
        dms_sensor_temperature,
        dms_irled_status_info.flt_reg_status[0],
        dms_irled_status_info.flt_reg_status[1],
        dms_irled_status_info.flt_reg_status[2],
        dms_irled_status_info.flt_reg_status[3],
        dms_irled_status_info.flt_reg_status[4],
        dms_irled_status_info.cfg_reg_status[0],
        dms_irled_status_info.cfg_reg_status[1],
        dms_irled_status_info.cfg_reg_status[2],
        dms_irled_status_info.cfg_reg_status[3],
        session_filename.c_str()
    );
    LOG_I(log_tag[CAMERA_POSITION_DMS], "DMS IRLED status: %s", buf);
    return string(buf);
}

//Function to handle the case when DMS IRLED status is FAULTY, during DMS recording.
//This function tries to clear the DMS IRLED status.
static void handle_dms_irled_status_faulty(const dmsirledinfo_t& dms_irled_status_info,
                                           int dms_sensor_temperature,
                                           const dms_irled_event_info_t* dms_irled_msg) {
    LOG_E(log_tag[CAMERA_POSITION_DMS], "DMS IRLED is FAULTY for the session: %s", dms_irled_msg->session_filename);
    //Handling for Severe Faults in DMS IRLED Status
    if (DMS_IS_ANY_FAULT_BIT_SET_IN_REG1(dms_irled_status_info.reg_status1) || DMS_IS_ANY_FAULT_BIT_SET_IN_REG2(dms_irled_status_info.reg_status2)) {
        bool is_dms_irled_status_clearable = false;
        int retry_count = 0;
        const int MAX_RETRY_ATTEMPTS = 3;
        LOG_E(log_tag[CAMERA_POSITION_DMS], "Severe Faults detected. reg_status1 : 0x%x, reg_status2: 0x%x. Retrying %d times to clear the status",
            dms_irled_status_info.reg_status1, dms_irled_status_info.reg_status2, MAX_RETRY_ATTEMPTS);

        do {
            int ret_dms = dmsIrledDriverSetup(&dmsCam, dmsIrledDriverDisable);
            if (ret_dms < 0) {
                LOG_E(log_tag[CAMERA_POSITION_DMS], "Could not disable the DMS IRLED Driver. ret:%d", ret_dms);

                //Send critical info for DMS IRLED API Failure cases
                cam_record_service_obj->send_err_msg(SM_E_NDC_DMS_IRLED_API_FAILURE,CAMERA_POSITION_DMS,"DMS IRLED API Failure: dmsIrledDriverSetup: Disable Driver. ret:" + std::to_string(ret_dms));

                LOG_I(log_tag[CAMERA_POSITION_DMS], "Sent Critical Info for failure in disabling DMS IRLED Driver. ret:%d", ret_dms);

            } else {
                LOG_I(log_tag[CAMERA_POSITION_DMS], "DMS IRLED Driver disabled successfully. ret:%d", ret_dms);
            }
            sleep(DMS_IRLED_STATUS_CLEAR_WAIT_TIME_IN_SECS);

            ret_dms = dmsIrledDriverSetup(&dmsCam, dmsIrledDriverEnable);
            if (ret_dms < 0) {
                LOG_E(log_tag[CAMERA_POSITION_DMS], "Could not enable the DMS IRLED Driver. ret:%d", ret_dms);

                //Send critical info for DMS IRLED API Failure cases
                cam_record_service_obj->send_err_msg(SM_E_NDC_DMS_IRLED_API_FAILURE,CAMERA_POSITION_DMS,"DMS IRLED API Failure: dmsIrledDriverSetup: Enable Driver. ret:" + std::to_string(ret_dms));

                LOG_I(log_tag[CAMERA_POSITION_DMS], "Sent Critical Info for failure in enabling DMS IRLED Driver. ret:%d", ret_dms);

            } else {
                LOG_I(log_tag[CAMERA_POSITION_DMS], "DMS IRLED Driver enabled successfully. ret:%d", ret_dms);
            }
            sleep(DMS_IRLED_STATUS_CLEAR_WAIT_TIME_IN_SECS);

            ret_dms = dmsIRLEDStatusClear(&dmsCam);
            if (ret_dms < 0) {
                LOG_E(log_tag[CAMERA_POSITION_DMS], "Could not clear the DMS IRLED Status. ret:%d", ret_dms);

                //Send critical info for DMS IRLED API Failure cases
                cam_record_service_obj->send_err_msg(SM_E_NDC_DMS_IRLED_API_FAILURE,CAMERA_POSITION_DMS,"DMS IRLED API Failure: dmsIRLEDStatusClear. ret:" + std::to_string(ret_dms));

                LOG_I(log_tag[CAMERA_POSITION_DMS], "Sent Critical Info for failure in clearing DMS IRLED Status. ret:%d", ret_dms);

            } else {
                LOG_I(log_tag[CAMERA_POSITION_DMS], "DMS IRLED Status cleared successfully. ret:%d", ret_dms);
            }

            sleep(DMS_IRLED_STATUS_FAULTY_WAIT_TIME_IN_SECS);

            dmsirledinfo_t dms_irled_status = {0};
            ret_dms = dmsIRLEDStatus(&dmsCam, &dms_irled_status);
            if (ret_dms < 0) {
                LOG_E(log_tag[CAMERA_POSITION_DMS], "Could not get DMS IRLED status. ret:%d", ret_dms);

                //Send critical info for DMS IRLED API Failure cases
                cam_record_service_obj->send_err_msg(SM_E_NDC_DMS_IRLED_API_FAILURE,CAMERA_POSITION_DMS,"DMS IRLED API Failure: dmsIRLEDStatus. ret:" + std::to_string(ret_dms));

                LOG_I(log_tag[CAMERA_POSITION_DMS], "Sent Critical Info for failure in getting DMS IRLED Status. ret:%d", ret_dms);

            } else {
                LOG_I(log_tag[CAMERA_POSITION_DMS], "DMS IRLED status: irled_status: %d, reg_status1: 0x%x, reg_status2:0x%x, reg_status3: 0x%x",
                    dms_irled_status.irled_status,
                    dms_irled_status.reg_status1,
                    dms_irled_status.reg_status2,
                    dms_irled_status.reg_status3);
                
                if ((DMS_IS_ANY_FAULT_BIT_SET_IN_REG1(dms_irled_status.reg_status1)
                || DMS_IS_ANY_FAULT_BIT_SET_IN_REG2(dms_irled_status.reg_status2))
                && DMS_IRLED_FAULT == dms_irled_status.irled_status) {
                    LOG_E(log_tag[CAMERA_POSITION_DMS], "DMS IRLED status is still faulty after clearing. retrying:%d", retry_count);
                    if (retry_count == MAX_RETRY_ATTEMPTS - 1) {
                        LOG_E(log_tag[CAMERA_POSITION_DMS], "DMS IRLED status is still faulty after retrying %d", retry_count);
                        is_dms_irled_status_clearable = false;
                        break;
                    }
                } else {
                    is_dms_irled_status_clearable = true;
                    LOG_I(log_tag[CAMERA_POSITION_DMS], "DMS IRLED status cleared successfully after retrying:%d", retry_count);
                    break;
                }
            }
            retry_count++;
        } while (retry_count < MAX_RETRY_ATTEMPTS);

        if (is_dms_irled_status_clearable) {
            std::stringstream ss;
            ss << "DFR:" //DFR: DMS Faulty Recovered
               << log_dms_irled_status(dms_irled_status_info, dms_sensor_temperature, dms_irled_msg->session_filename);

            cam_record_service_obj->send_err_msg(SM_E_NDC_DMS_IRLED_STATUS_CLEAR_FAILED_RECOVERED,
                                                CAMERA_POSITION_DMS,
                                                ss.str());
            LOG_I(log_tag[CAMERA_POSITION_DMS],
                 "Sent Critical Info : SM_E_NDC_DMS_IRLED_STATUS_CLEAR_FAILED_RECOVERED: %s",
                  ss.str().c_str());
        } else {
            LOG_E(log_tag[CAMERA_POSITION_DMS], "DMS IRLED status is still faulty after retrying %d times.", MAX_RETRY_ATTEMPTS);
            std::stringstream ss;
            ss << "DFN:" //DFN: DMS Faulty Not Recovered
            << log_dms_irled_status(dms_irled_status_info, dms_sensor_temperature, dms_irled_msg->session_filename);

            cam_record_service_obj->send_err_msg(SM_E_NDC_DMS_IRLED_STATUS_CLEAR_FAILED_NON_RECOVERABLE,
                                                CAMERA_POSITION_DMS,
                                                ss.str());

            LOG_I(log_tag[CAMERA_POSITION_DMS],
                 "Sent Critical Info : SM_E_NDC_DMS_IRLED_STATUS_CLEAR_FAILED_NON_RECOVERABLE: %s",
                  ss.str().c_str());
        }

    // As per design discussion in DMS-109, we need to clear DMS IRLED status if the status is Faulty irrespective of register values.
    } else {
        std::stringstream ss;
        ss << "DF:" //DF: DMS Faulty, but not severe faults in reg_status1 and reg_status2
           << log_dms_irled_status(dms_irled_status_info, dms_sensor_temperature, dms_irled_msg->session_filename);

        cam_record_service_obj->send_err_msg(SM_E_NDC_DMS_IRLED_STATUS_FAULT_DETECTED,
                                            CAMERA_POSITION_DMS,
                                            ss.str());

        LOG_I(log_tag[CAMERA_POSITION_DMS],
             "Sent Critical Info : SM_E_NDC_DMS_IRLED_STATUS_FAULT_DETECTED: %s",
              ss.str().c_str());

        int ret_dms = dmsIRLEDStatusClear(&dmsCam);
        if (ret_dms < 0) {
            LOG_E(log_tag[CAMERA_POSITION_DMS], "Could not clear the DMS IRLED Status. ret:%d", ret_dms);

            //Send critical info for DMS IRLED API Failure cases
            cam_record_service_obj->send_err_msg(SM_E_NDC_DMS_IRLED_API_FAILURE,CAMERA_POSITION_DMS,"DMS IRLED API Failure: dmsIRLEDStatusClear. ret:" + std::to_string(ret_dms));

            LOG_I(log_tag[CAMERA_POSITION_DMS], "Sent Critical Info for failure in clearing DMS IRLED Status. ret:%d", ret_dms);

        } else {
            LOG_I(log_tag[CAMERA_POSITION_DMS], "DMS IRLED Status cleared successfully. ret:%d", ret_dms);
        }
    }
}

static void handle_dms_irled_status_off(const dmsirledinfo_t& dms_irled_status_info,
                                        int dms_sensor_temperature,
                                        const dms_irled_event_info_t* dms_irled_msg)
{
    LOG_E(log_tag[CAMERA_POSITION_DMS], "DMS IRLED status is OFF");

    std::stringstream ss;
    ss << "DOF:" //DOF: DMS Off
       << log_dms_irled_status(dms_irled_status_info, dms_sensor_temperature, dms_irled_msg->session_filename);

    cam_record_service_obj->send_err_msg(
        SM_E_NDC_DMS_IRLED_STATUS_OFF,
        CAMERA_POSITION_DMS,
        ss.str());

    LOG_I(log_tag[CAMERA_POSITION_DMS],
          "Sent Critical Info : SM_E_NDC_DMS_IRLED_STATUS_OFF: %s",
          ss.str().c_str());
}

static void handle_dms_irled_voltage_out_of_limits(const dmsirledinfo_t& dms_irled_status_info,
                                                   int dms_sensor_temperature,
                                                   const dms_irled_event_info_t* dms_irled_msg)
{
    // Log voltage out of limits
    LOG_E(log_tag[CAMERA_POSITION_DMS], "DMS irled voltage out of limits. %d", dms_irled_status_info.reg_status3);

    std::stringstream ss;
    ss << "DVO:" //DVO: DMS Voltage Out of limits
       << log_dms_irled_status(dms_irled_status_info, dms_sensor_temperature, dms_irled_msg->session_filename);

    cam_record_service_obj->send_err_msg(
        SM_E_NDC_DMS_IRLED_STATUS_VOLTAGE_OUT_OF_LIMITS,
        CAMERA_POSITION_DMS,
        ss.str());

    LOG_I(log_tag[CAMERA_POSITION_DMS],
         "Sent Critical Info : SM_E_NDC_DMS_IRLED_STATUS_VOLTAGE_OUT_OF_LIMITS: %s",
                                         ss.str().c_str());
}

static void send_dms_health_info_to_bagheera_from_status(
    const dmsirledinfo_t& dms_irled_status_info,
    int dms_sensor_temperature,
    const std::string& session_filename)
{
    dms_fault_registers_t fault_register_values = {};
    dms_config_registers_t config_register_values = {};

    fault_register_values.reg_0x0A = dms_irled_status_info.flt_reg_status[0];
    fault_register_values.reg_0x0B = dms_irled_status_info.flt_reg_status[1];
    fault_register_values.reg_0x0C = dms_irled_status_info.flt_reg_status[2];
    fault_register_values.reg_0x0D = dms_irled_status_info.flt_reg_status[3];
    fault_register_values.reg_0x0E = dms_irled_status_info.flt_reg_status[4];

    config_register_values.reg_0x02 = dms_irled_status_info.cfg_reg_status[0];
    config_register_values.reg_0x03 = dms_irled_status_info.cfg_reg_status[1];
    config_register_values.reg_0x04 = dms_irled_status_info.cfg_reg_status[2];
    config_register_values.reg_0x05 = dms_irled_status_info.cfg_reg_status[3];

    LOG_I(log_tag[CAMERA_POSITION_DMS],
          "Sending DMS health_info to bagheera: dms_irled_status:%d, SN:%s, dms_sensor_temperature:%d fault_registers:[0x%x 0x%x 0x%x 0x%x 0x%x], config_registers:[0x%x 0x%x 0x%x 0x%x]",
          dms_irled_status_info.irled_status, dmsCam_SN, dms_sensor_temperature,
          fault_register_values.reg_0x0A, fault_register_values.reg_0x0B, fault_register_values.reg_0x0C, fault_register_values.reg_0x0D, fault_register_values.reg_0x0E,
          config_register_values.reg_0x02, config_register_values.reg_0x03, config_register_values.reg_0x04, config_register_values.reg_0x05);

    send_dms_health_info_to_bagheera(
        Q_NAME,
        dms_irled_status_info.irled_status,
        dmsCam_SN,
        dms_sensor_temperature,
        fault_register_values,
        config_register_values,
        session_filename
    );
}

//Refer DMS-109 for design details
static void* dms_irled_thread_func(void* arg) {
    LOG_I(log_tag[CAMERA_POSITION_DMS], "dms_irled_thread_func: started");
    while (1) {
        nd_msgq_t::nd_msg_t* received = msgq_dms_irled->receive();
        if (NULL == received) {
            LOG_E(log_tag[CAMERA_POSITION_DMS], "Received is NULL, retrying\n");
            continue;
        }
        dms_irled_event_info_t *dms_irled_msg = (dms_irled_event_info_t *)received->get_buffer();
        if (dms_irled_msg == NULL) {
            LOG_E(log_tag[CAMERA_POSITION_DMS], "Received NULL dms_irled_event_info_t.\n");
            if(received) {
                delete received;
            }
            continue;
        }
        dms_irled_event_t event = dms_irled_msg->event;
        
        LOG_I(log_tag[CAMERA_POSITION_DMS], "dms_irled_event received: %d, from msgq_dms_irled: %p", event, msgq_dms_irled);

        if (!cams_enabled[CAMERA_POSITION_DMS] || !isDMSconnected()) {
            LOG_E(log_tag[CAMERA_POSITION_DMS], "DMS not connected !! Hence Not processing any DMS IRLED events.");
            // Handle the case of DMS disconnection during runtime. We still need to send DMS health_info with status as DMS_IRLED_STATUS_INVALID
            if(DMS_IRLED_EVENT_CHECK_STATUS == event) {
                LOG_I(log_tag[CAMERA_POSITION_DMS], "Sending DMS health_info to bagheera with DMS_IRLED_STATUS_INVALID as DMS is not connected: SN:%s", dmsCam_SN);
                dmsirledinfo_t dms_irled_status_info = {};
                dms_irled_status_info.irled_status = DMS_IRLED_STATUS_INVALID;
                int dms_sensor_temperature = -1; // Invalid temperature value
                // Send DMS health_info to bagheera
                send_dms_health_info_to_bagheera_from_status(dms_irled_status_info, dms_sensor_temperature, dms_irled_msg->session_filename);
            }
            if(received) {
                delete received;
            }
            continue;
        }
        int ret_dms = -1;
        switch (event) {
            case DMS_IRLED_EVENT_STARTED_STREAMING:
            {
                LOG_I(log_tag[CAMERA_POSITION_DMS], "Handling DMS_IRLED_EVENT_STARTED_STREAMING event");
                // Wait for 1 second after DMS streaming starts and clear the IRLED status
                sleep(DMS_IRLED_STATUS_CLEAR_WAIT_TIME_IN_SECS);

                ret_dms = dmsIrledDriverSetup(&dmsCam, dmsIrledDriverEnable);
                if (ret_dms < 0) {
                    LOG_E(log_tag[CAMERA_POSITION_DMS], "Could not enable the DMS IRLED Driver. ret:%d", ret_dms);

                    //Send critical info for DMS IRLED API Failure cases
                    cam_record_service_obj->send_err_msg(SM_E_NDC_DMS_IRLED_API_FAILURE,CAMERA_POSITION_DMS,"DMS IRLED API Failure: dmsIrledDriverSetup: Enable driver. ret:" + std::to_string(ret_dms));

                    LOG_I(log_tag[CAMERA_POSITION_DMS], "Sent Critical Info for failure in enabling DMS IRLED driver. ret:%d", ret_dms);

                } else {
                    LOG_I(log_tag[CAMERA_POSITION_DMS], "DMS IRLED Driver enabled successfully. ret:%d", ret_dms);
                }

                sleep(DMS_IRLED_STATUS_CLEAR_WAIT_TIME_IN_SECS);

                ret_dms = dmsIRLEDStatusClear(&dmsCam);
                if (ret_dms < 0) {
                    LOG_E(log_tag[CAMERA_POSITION_DMS], "Could not clear the DMS IRLED Status. ret:%d", ret_dms);

                    //Send critical info for DMS IRLED API Failure cases
                    cam_record_service_obj->send_err_msg(SM_E_NDC_DMS_IRLED_API_FAILURE,CAMERA_POSITION_DMS,"DMS IRLED API Failure: dmsIRLEDStatusClear. ret:" + std::to_string(ret_dms));

                    LOG_I(log_tag[CAMERA_POSITION_DMS], "Sent Critical Info for failure in clearing DMS IRLED Status. ret:%d", ret_dms);

                } else {
                    LOG_I(log_tag[CAMERA_POSITION_DMS], "DMS IRLED Status cleared successfully. ret:%d", ret_dms);
                }

                break;
            }

            case DMS_IRLED_EVENT_SET_LED_COLOR:
            {
                LOG_I(log_tag[CAMERA_POSITION_DMS], "Handling DMS_IRLED_EVENT_SET_LED_COLOR event");

                if (!is_dms_streaming_started) {
                    LOG_E(log_tag[CAMERA_POSITION_DMS], "DMS streaming not started yet. Waiting for a max of %d seconds to set LED Color.",
                                                         DMS_LED_COLOR_WAIT_TIME_IN_SECS);
                }
                int64_t start_time = get_system_monotonic_time();
                while (!is_dms_streaming_started && ((get_system_monotonic_time() - start_time) < DMS_LED_COLOR_WAIT_TIME_IN_SECS * 1000)) {
                    usleep(DMS_LED_COLOR_SLEEP_TIME_IN_USECS);
                    if(is_dms_streaming_started) {
                        LOG_I(log_tag[CAMERA_POSITION_DMS], "DMS streaming started now. Waited for %lld milliseconds to set LED Color.", 
                            (get_system_monotonic_time() - start_time));
                        break;
                    }
                }

                if (is_dms_streaming_started) {
                    ret_dms = dmsSetLedColor(&dmsCam, dms_irled_msg->led_colour);
                    if (ret_dms < 0) {
                        LOG_E(log_tag[CAMERA_POSITION_DMS], "Could not set DMS LED Color. ret:%d", ret_dms);

                        //Send critical info for DMS IRLED API Failure cases
                        cam_record_service_obj->send_err_msg(SM_E_NDC_DMS_IRLED_API_FAILURE,CAMERA_POSITION_DMS,"DMS IRLED API Failure: dmsSetLedColor. ret:" + std::to_string(ret_dms));

                        LOG_I(log_tag[CAMERA_POSITION_DMS], "Sent Critical Info for failure in setting DMS LED Color. ret:%d", ret_dms);

                    } else {
                        LOG_I(log_tag[CAMERA_POSITION_DMS], "DMS LED Colour set successfully to: %d", dms_irled_msg->led_colour);
                    }
                } else {
                    LOG_E(log_tag[CAMERA_POSITION_DMS], "DMS streaming not start within %d seconds. Cannot set LED color", DMS_LED_COLOR_WAIT_TIME_IN_SECS);
                }

                break;
            }

            case DMS_IRLED_EVENT_CHECK_STATUS:
            {
                LOG_I(log_tag[CAMERA_POSITION_DMS], "Handling DMS_IRLED_EVENT_CHECK_STATUS event");

                // Check DMS sensor temperature
                int dms_sensor_temperature = -1; // Initializing to invalid temperature value
                
                // As of now, libsys API to get DMS sensor temperature is resulting in more frequent DMS IRLED faults.
                // Hence commenting out the API call for now.
                // Will re-visit this in future.
                /*
                int ret_status = dmsGetSensorTemperature(&dmsCam, &dms_sensor_temperature);
                if (ret_status < 0) {
                    LOG_E(log_tag[CAMERA_POSITION_DMS], "Could not get DMS temperature. ret:%d\n", ret_status);
                }
                else {
                    LOG_I(log_tag[CAMERA_POSITION_DMS], "DMS sensor temperature: %d\n", dms_sensor_temperature);
                }
                */

                dmsirledinfo_t dms_irled_status_info = {};

                dms_irled_status_info.irled_status = DMS_IRLED_STATUS_INVALID; // initialising to default value

                int ret_dms = dmsIRLEDStatus(&dmsCam, &dms_irled_status_info);
                if (ret_dms < 0) {
                    LOG_E(log_tag[CAMERA_POSITION_DMS], "Could not get DMS IRLED status. ret:%d\n", ret_dms);

                    //Send critical info for DMS IRLED API Failure cases
                    cam_record_service_obj->send_err_msg(SM_E_NDC_DMS_IRLED_API_FAILURE,CAMERA_POSITION_DMS,"DMS IRLED API Failure: dmsIRLEDStatus. ret:" + std::to_string(ret_dms));

                    LOG_I(log_tag[CAMERA_POSITION_DMS], "Sent Critical Info for failure in getting DMS IRLED Status. ret:%d", ret_dms);

                }
                else {
                    log_dms_irled_status(dms_irled_status_info, dms_sensor_temperature, dms_irled_msg->session_filename);
                
                    /* Handle DMS IRLED OFF status */
                    if (DMS_IRLED_OFF == dms_irled_status_info.irled_status) {
                        handle_dms_irled_status_off(dms_irled_status_info, dms_sensor_temperature, dms_irled_msg);
                    }

                    /* Monitor IRLED Voltage value every time. */
                    // Refer to Jira DMS-109 more more details on this event.
                    LOG_I(log_tag[CAMERA_POSITION_DMS], "Handling DMS_IRLED_EVENT_CHECK_VOLTAGE event");
                    if (dms_irled_status_info.reg_status3 < DMS_IRLED_MIN_VOLTAGE_THRESHOLD || dms_irled_status_info.reg_status3 > DMS_IRLED_MAX_VOLTAGE_THRESHOLD) {
                        handle_dms_irled_voltage_out_of_limits(dms_irled_status_info, dms_sensor_temperature, dms_irled_msg);
                    } else {
                        LOG_I(log_tag[CAMERA_POSITION_DMS], "DMS irled voltage is within limits. 0x%x", dms_irled_status_info.reg_status3);
                    }

                    /* Handle DMS IRLED FAULT status */
                    if (DMS_IRLED_FAULT == dms_irled_status_info.irled_status) {
                        LOG_E(log_tag[CAMERA_POSITION_DMS], "DMS IRLED is FAULTY for the session: %s", dms_irled_msg->session_filename);
                        handle_dms_irled_status_faulty(dms_irled_status_info, dms_sensor_temperature, dms_irled_msg);
                    }
                }
                
                // Send DMS health_info to bagheera
                send_dms_health_info_to_bagheera_from_status(dms_irled_status_info, dms_sensor_temperature, dms_irled_msg->session_filename);

                break;
            }

            default:
                LOG_E(log_tag[CAMERA_POSITION_DMS], "Unknown dms_irled_event_t: %d", event);
                break;
        }
        delete received; // Clean up
        LOG_I(log_tag[CAMERA_POSITION_DMS], "dms_irled_event %d Processed successfully.", event);
    }
    return NULL;
}

void dms_usb_connect_cb(int32_t sig_type) {
    switch(sig_type) {
        case DMS_STATUS_CONNECTED:
        {
            /* As per BSP we should not block this CB thread, that is the reason instead
            of restarting the DMS pipeline here, we are doing it in a separate thread
            "check_dms_connection_th" */
            break;
        }
        case DMS_STATUS_DISCONNECTED:
        {
            if (cams_enabled[CAMERA_POSITION_DMS]) {
                is_dms_connected = false;
                LOG_I(TAG, "DMS camera Disconnected during recording, sending critical info to cloud\n");
                if (!pthread_create (&check_dms_connection_th, NULL, check_dms_connection, NULL))
                    LOG_I(TAG, "check_dms_connection thread creation success");
	            else
	                LOG_E (TAG, "check_dms_connection thread creation failed");
                cam_record_service_obj->send_err_msg(SM_E_NDC_CAM_DISCONNECTED, CAMERA_POSITION_DMS, "DMS camera disconnected");
                send_dms_connection_status_to_bagheera(Q_NAME, is_dms_connected);
            }
            break;
        }
        default:
            LOG_E(TAG, "Unknown signal type: %d\n", sig_type);
            break;
    }
    return;
}

bool getDMS_SN_from_Installer_app(string& dms_sn) {
    using nd::device::Accessory;
    using nd::device::AccessoryDB;
    constexpr char Dms_Accessory[] = "DMS_CAMERA";
    std::vector<Accessory> accessories;

    const auto get_status = AccessoryDB::GetAllOfType(Dms_Accessory, accessories);

    if (0 != get_status.first) {
        LOG_E(TAG, "getDMS_SN_from_Installer_app: GetAllOfType failed with error: %d:%s", get_status.first, get_status.second.c_str());
        return false;
    }

    if (accessories.empty()) {
        LOG_I(TAG, "No DMS Camera found");
        return false;
    }

    json_error_t error{};
    json_t *root = json_loads(accessories[0].data_.c_str(), 0, &error);

    if (nullptr == root) {
        LOG_E(TAG, "getDMS_SN_from_Installer_app: json_loads failed for acc: %s with error: %s", accessories[0].data_.c_str(), error.text);
        return false;
    }

    if (json_is_string(json_object_get(root, "SN"))) {
        dms_sn = json_string_value(json_object_get(root, "SN"));
    } else {
        LOG_E(TAG, "getDMS_SN_from_Installer_app:entry is not a string in accessory DB");
        return false;
    }
    return true;
}

bool deinit_dms_camera_tt(void *args)
{
    if (dmsUnInit(&dmsCam) < 0) {
        LOG_E(TAG, "DMS camera uninit failed\n");
        return false;
    }
    return true;
}

bool deinit_dms_camera()
{
    task_result_t task_result = nd_timed_task(deinit_dms_camera_tt, CAM_BOOT_STATUS_CHECK_TIMEOUT, NULL, "dms uninit");
    if (task_result == TASK_TIMEOUT) {
        LOG_E (TAG, "nd_timed_task for dmsUnInit timedout");
        cam_record_service_obj->send_err_msg(SM_E_NDC_CAM_STATUS_FAIL, 8, "dmsUnInit TIMEDOUT");
        return false;
    } else if (task_result == TASK_FAIL) {
        LOG_E (TAG, "Failed to do dmsUnInit");
        cam_record_service_obj->send_err_msg(SM_E_NDC_CAM_STATUS_FAIL, 8, "dmsUnInit failed");
        return false;
    }
    return true;
}

bool init_dms_camera()
{
    memset(&dmsCam, 0 , sizeof(dmsCam_t));
    memset(dmsCam_SN, 0 , sizeof(dmsCam_SN));

    dmsCam.args.devPath = "/dev/dms_h264";
    dmsCam.args.ir_level = DMS_IR_L1;
    dmsCam.args.width = 1296;
    dmsCam.args.height = 1296;
    dmsCam.args.fps = 30;
    dmsCam.args.bitrate = 8000000;
    dmsCam.args.gop = 29;
    /* For USB Connect/Disconnect callback */
    dmsCam.args.pid = 0x0c45;
    dmsCam.args.vid = 0x6366;
    dmsCam.args.cb = dms_usb_connect_cb;

    if (dmsInit(&dmsCam) < 0) {
        LOG_E(TAG, "Could not find/open sonix device, DMS camera open fails");
        cams_enabled[CAMERA_POSITION_DMS] = false;
        if (first_after_boot == true) {
            dms_connection_status_file_write_fd << 0 << endl;
        }
        return false;
    } else {
        if (dmsGetSN(&dmsCam, dmsCam_SN, SN_LEN) < 0) {
            LOG_E(TAG, "dms SN Eeprom Get ERROR! \n");
        } else {
            int retry_count = 3;
            while ((strlen((const char*)dmsCam_SN) < SN_LEN) && (retry_count > 0)) {
                LOG_E(TAG, "dmsGetSN API returns erroneous SN, will retry \n");
                sleep(1);
                if (dmsGetSN(&dmsCam, dmsCam_SN, SN_LEN) < 0) {
                    LOG_E(TAG, "dms SN Eeprom Get ERROR! \n");
                    break;
                }
                retry_count--;
            }
            dmsCam_SN[SN_LEN] = '\0';
            LOG_I(TAG, "dms SN Eeprom Verify %s\n", dmsCam_SN);
        }
        string dms_sn = "";
        bool ret = getDMS_SN_from_Installer_app(dms_sn);
        if (ret == true) {
            if (!strcmp(dms_sn.c_str(), (const char*)dmsCam_SN)) {
                LOG_I(TAG, "dms SN matches, which is provided by Installer App");
                if (first_after_boot)
                    dms_connection_status_file_write_fd << 1 << endl;
            } else {
                LOG_E(TAG, "dms SN %s doesn't match, which is provided by Installer App", dms_sn.c_str());
                cams_enabled[CAMERA_POSITION_DMS] = false;
                if (first_after_boot)
                    dms_connection_status_file_write_fd << 0 << endl;
            }
        } else {
            cams_enabled[CAMERA_POSITION_DMS] = false;
            if (first_after_boot)
                dms_connection_status_file_write_fd << 0 << endl;
        }

        if (dmsSetH264Control(&dmsCam, dmsH264GOP, 29) < 0) {
            LOG_E(TAG, "Could not set GOP to 29");
        }

        /* Lets get the flip property in DB, which would have set during DMS installation */
        stringstream property_str;
        property_str << "CAM" << CAMERA_POSITION_DMS << "_ROTATE";
        prop_data_t entry;

        if (db_handle != NULL) {
            if (!get_property_DB(property_str.str(), &entry, db_handle)) {
                LOG_E(TAG, "get_property_DB failed to get dms_rotate");
            }
        }

        if (entry.value == "1")
            dms_camera_flip = 1;
        else if (entry.value == "0")
            dms_camera_flip = 0;

        if (dmsCamSetVflip(&dmsCam, dms_camera_flip) < 0) {
            LOG_E(TAG, "Failed to set vertical flip");
        }
        if (dmsCamSetHflip(&dmsCam, dms_camera_flip) < 0) {
            LOG_E(TAG, "Failed to set horizontal flip");
        }
    }
    LOG_E(TAG, "init_dms_camera exit\n");
    return true;
}

bool restart_dms_recording()
{
    cam_record_ctxt *context = &cam_rec_ctx[CAMERA_POSITION_DMS];
    stop_record_session_platform(CAMERA_POSITION_DMS);
    context->state = STATE_CREATED;
    init_record_session_platform(CAMERA_POSITION_DMS);
    start_record_session_platform(CAMERA_POSITION_DMS);

    return true;
}

void* send_critical_info_for_DMS_enabled_but_disconnected(void* arg) {
    int elapsed_time = 0;
    int max_wait_time = MAX_WAIT_TIME_FOR_SENDING_CRITICAL_INFO;

    while (elapsed_time < max_wait_time) {
        bool send_status = cam_record_service_obj->send_err_msg(SM_E_NDC_DMS_ENABLED_BUT_DISCONNECTED, CAMERA_POSITION_DMS,
            "DMS camera is enabled in nd_config.ini but disconnected");

        if (send_status) {
            LOG_I(TAG, "Successfully sent critical info for case when DMS is enabled in nd_config.ini but disconnected");
            return NULL;
        }
        sleep(1); // Wait for 1 second before trying again
        elapsed_time++;
    }

    // If the maximum wait time is reached
    LOG_E(TAG, "Failed to send critical for DMS camera (enabled in nd_config.ini but disconnected) after waiting for %d seconds", max_wait_time);
    return NULL;
}

void* send_critical_info_for_dmsInit_failure(void* arg) {
    int elapsed_time = 0;
    int max_wait_time = MAX_WAIT_TIME_FOR_SENDING_CRITICAL_INFO;

    while (elapsed_time < max_wait_time) {
        bool send_status = cam_record_service_obj->send_err_msg(SM_E_NDC_DMS_INIT_FAIL, CAMERA_POSITION_DMS,
            "DMS Initialization Fail: dmsInit camera failed when DMS already connected at bootup");

        if (send_status) {
            LOG_I(TAG, "Successfully sent critical info for case when DMS initialization failed when DMS connected at bootup");
            return NULL;
        }
        sleep(1); // Wait for 1 second before trying again
        elapsed_time++;
    }

    // If the maximum wait time is reached
    LOG_E(TAG, "Failed to send critical info for DMS initialization fail at bootup (DMS connected) after waiting for %d seconds", max_wait_time);
    return NULL;
}
#endif

static bool get_cams_enabled()
{
    // Marks which all cameras need to be enabled after reading the config file.

    // First check over ride file
    Config_parser *c = new Config_parser (CAMERA_OVERRIDE_INI);
    if (c == NULL) {
        LOG_E (TAG,"Allocating object for camera override file failed");
        return false;
    }

    if (c->getParseStatus()) {
        LOG_I (TAG,"Reading camera config from cam_override.ini.");
    } else {
        LOG_I(TAG,"Reading camra config from bagheera_config.ini");
        delete (c);
        c = NULL;

        c = new Config_parser (BAGHEERACONFIG_INI);
    }

    if (c->getParseStatus() != true)
        return false;

    if (c->isPresent ("camera","back")) {
        if (c->getConfig("camera","back","") == "enable")
            cams_enabled[CAMERA_POSITION_BACK] = true;
        else
            cams_enabled[CAMERA_POSITION_BACK] = false;
    } else {
        LOG_E (TAG, "No configuration for back camera");
        cams_enabled[CAMERA_POSITION_BACK] = false;
    }

    if (c->isPresent ("camera","left")) {
        if (c->getConfig("camera","left","") == "enable")
            cams_enabled[CAMERA_POSITION_LEFT] = true;
        else
            cams_enabled[CAMERA_POSITION_LEFT] = false;
    } else {
        LOG_E (TAG, "No configuration for left camera");
        cams_enabled[CAMERA_POSITION_LEFT] = false;
    }

    if (c->isPresent ("camera","right")) {
        if (c->getConfig("camera","right","") == "enable")
            cams_enabled[CAMERA_POSITION_RIGHT] = true;
        else
            cams_enabled[CAMERA_POSITION_RIGHT] = false;
    } else {
        LOG_E (TAG, "No configuration for right camera");
        cams_enabled[CAMERA_POSITION_RIGHT] = false;
    }

    Config_parser temp (BAGHEERACONFIG_INI);
    Config_parser temp_nd (ND_CONFIG_ANALYTICS);
    bool get_override_val = true;
    bool is_val_overridden = false;

    if (temp.getParseStatus()) {
        string_to_int64(temp.getConfig("camera", "side_cam_disable_threshold_time","", get_override_val, is_val_overridden),CRASH_TIME_DIFF_THRESHOLD_MS);
        LOG_I(TAG, "CRASH_TIME_DIFF_THRESHOLD_MS set to %ld ms", CRASH_TIME_DIFF_THRESHOLD_MS);
    }
#ifdef DMS_CAMERA_SUPPORTED
    /* DMS is only supported for Bagheera3 as of now */
    int dms_drowsy = 0;
    if (temp_nd.getParseStatus()) {
        string_to_integer(temp_nd.getConfig("dms_drowsy", "enabled", "0", get_override_val, is_val_overridden), dms_drowsy);
    }

    if (isDMSsupportedInDevice(nd_device_obj->getDeviceType())) {
        /* if dms_drowsy is enabled in nd_config.ini, we will enable DMS recording by default
         * If it is disabled, then only we will go ahead and check for dms_camera config for
         * dms recording only without drowsy */
        if (dms_drowsy) {
            LOG_I(TAG, "dms_drowsy is enabled in nd_config.ini, enable dms recording also");
            cams_enabled[CAMERA_POSITION_DMS] = true;
        }
        if (!dms_drowsy && temp.getParseStatus()) {
            if (temp.getConfig("dms_camera","enabled","", get_override_val, is_val_overridden) == "true") {
                LOG_I(TAG, "dms_drowsy is disabled in nd_config.ini but dms_camera is enabled in override, enable dms recording");
                cams_enabled[CAMERA_POSITION_DMS] = true;
            } else {
                LOG_I(TAG, "DMS camera is disabled");
                cams_enabled[CAMERA_POSITION_DMS] = false;
            }
        }
        if (cams_enabled[CAMERA_POSITION_DMS] && temp.getParseStatus()) {
            /* if DMS camera is enabled, lets get the property of flip from config */
            if (temp.getConfig("INSTALLER_APP", "dms_camera_flip", "", get_override_val, is_val_overridden) == "1") {
                LOG_I(TAG, "DMS camera flip is enabled");
                dms_camera_flip = 1;
            } else {
                LOG_I(TAG, "DMS camera flip is disabled");
                dms_camera_flip = 0;
            }
        }
    }

    is_dms_enabled_in_config = cams_enabled[CAMERA_POSITION_DMS];
    LOG_I(TAG, "is_dms_enabled_in_config: %d", is_dms_enabled_in_config);

    // This check is for Bagheera3 device when DMS is enabled in config but disconnected
    if (is_dms_enabled_in_config && !isDMSconnected()) {
        if (first_after_boot && dms_drowsy) {
            LOG_E(TAG, "dms_drowsy is enabled in nd_config.ini, but DMS camera is not connected");
            // Create a thread to send critical info when DMS is enabled in nd_config.ini but disconnected. 
            // Thread is created to ensure service monitor is started before calling critical info API (send_err_msg())
            static pthread_t critical_info_thread = (pthread_t)-1;
            // This check is to ensure that we send critical info only once incase get_cams_enabled is called multiple times
            if (critical_info_thread == (pthread_t)-1) {
                if (pthread_create(&critical_info_thread, NULL, send_critical_info_for_DMS_enabled_but_disconnected, NULL) == 0) {
                    LOG_I(TAG, "Thread created to send critical info when DMS is enabled in nd_config but disconnected");
                    // Detach the thread to release resources automatically after it finishes
                    pthread_detach(critical_info_thread);
                } else {
                    LOG_E(TAG, "Failed to create thread to send critical info when DMS is enabled in nd_config but disconnected");
                }
            }
        }
    }

    if (first_after_boot)
        dms_connection_status_file_write_fd.open(dms_connection_status_file);

    if (cams_enabled[CAMERA_POSITION_DMS]) {
        is_dms_initializing = true;
        g_mutex_init(&dms_disconnect_connect_mutex);
        if (init_dms_camera() == false && file_is_present(DMS_NODE)) {
            LOG_E(TAG, "DMS camera initialization failed");
            static pthread_t dms_init_critical_info_th = (pthread_t)-1;
            if (dms_init_critical_info_th == (pthread_t)-1) {
                if (pthread_create(&dms_init_critical_info_th, NULL, send_critical_info_for_dmsInit_failure, NULL) == 0) {
                    LOG_I(TAG, "Thread created to send critical info when DMS initialization fails during bootup");
                    // Detach the thread to release resources automatically after it finishes
                    pthread_detach(dms_init_critical_info_th);
                } else {
                    LOG_E(TAG, "Failed to create thread to send critical info when DMS initialization fails during bootup");
                }
            }
        }
        LOG_I(TAG, "dmsInit completed.");
        is_dms_initializing = false;
        dms_init_complete_time_us = g_get_monotonic_time();

    } else {
        if (first_after_boot == true) {
            dms_connection_status_file_write_fd << 0 << endl;
        }
    }

    if (first_after_boot)
        dms_connection_status_file_write_fd.close();
#endif

    is_dms_connected = cams_enabled[CAMERA_POSITION_DMS];
    if (temp.getParseStatus()) {
        string max_crash_count_str = "";
        if ((max_crash_count_str = temp.getConfig("camera", "max_cam_crash_count", "", get_override_val, is_val_overridden)) == "") {
            LOG_I (TAG, "Max crash count would be default  %d", max_cam_crash_count);
        } else {
            int max_crash_count_int;
            if (string_to_integer (max_crash_count_str, max_crash_count_int)) {
                max_cam_crash_count = max_crash_count_int;
                LOG_I (TAG, "Max crash count set to  %d", max_cam_crash_count);
            }
        }

        for (int i = 1; i < CAMERA_POSITION_MAX; i++) {
            stringstream property_str;
            property_str << "CAM" << i << "_CRASH_COUNT";
            prop_data_t entry;

            if (first_after_boot == false) {
                if (db_handle_camera_crash != NULL) {
                    if (get_property_DB(property_str.str(), &entry, db_handle_camera_crash, CAMERA_CRASH_DB_TABLE)) {
                        string_to_integer(entry.value, cam_crash_count[i]);
                        LOG_I(TAG, "crash count for cam %d is at %d", i, cam_crash_count[i]);
                    }
                }
            }
            // disable only left and right cameras
            if ((i == CAMERA_POSITION_LEFT) || (i == CAMERA_POSITION_RIGHT))
            {
                if (cam_crash_count[i] >= max_cam_crash_count && db_handle_camera_crash != NULL && cams_enabled[i]) {
                    std::string property_str_str = property_str.str();
                    if (check_side_cam_enable_disable(property_str_str, i)) {
                        cams_enabled[i] = false;
                    }
                }
            }
        }
    } else {
        LOG_E(TAG, "Can't parse bagheera_config.ini");
    }

    return true;
}

void move_partial_files()
{
    vector<string> vec;

    if (!get_files(CAMREC_FILE_BASE_PATH, vec)) {
        LOG_E(TAG, "Failed to get file details from given path %s", CAMREC_FILE_BASE_PATH.c_str());
    } else {
        for (vector<string>::iterator iter= vec.begin(), end = vec.end(); iter!=end; iter++) {
            /* should call send_move_partial_files_msg_to_bagheera only for HD file
             * and bagheera should take care of moving both HD and LD  */
            if (((*iter).find("1_trip") != std::string::npos) && ((*iter).find("ld.mp4") == std::string::npos)) {
                LOG_I(TAG, "send msg to bagheera service to move inward cam partial file %s to circular buffer folder and also add to DB", (*iter).c_str());
                send_move_partial_files_msg_to_bagheera(Q_NAME, CAMERA_POSITION_BACK, (*iter).c_str());
            } else if ((*iter).find("2_trip") != std::string::npos) {
                LOG_I(TAG, "send msg to bagheera service to move left cam partial file %s to circular buffer folder and also add to DB", (*iter).c_str());
                send_move_partial_files_msg_to_bagheera(Q_NAME, CAMERA_POSITION_LEFT, (*iter).c_str());
            } else if ((*iter).find("3_trip") != std::string::npos) {
                LOG_I(TAG, "send msg to bagheera service to move right cam partial file %s to circular buffer folder and also add to DB", (*iter).c_str());
                send_move_partial_files_msg_to_bagheera(Q_NAME, CAMERA_POSITION_RIGHT, (*iter).c_str());
            } else if (((*iter).find("8_trip") != std::string::npos) && ((*iter).find("ld.mp4") == std::string::npos)) {
                LOG_I(TAG, "send msg to bagheera service to move dms cam partial file %s to circular buffer folder and also add to DB", (*iter).c_str());
                send_move_partial_files_msg_to_bagheera(Q_NAME, CAMERA_POSITION_DMS, (*iter).c_str());
            }
        }
    }
}

static bool init_camera()
{
    int i=0;
    int cam_boot_status = 0;
    bool is_cam_boot_failed = false;

    get_cams_enabled();

    cam_boot_status = check_camera_boot_status();

    if (cams_enabled[CAMERA_POSITION_BACK] && (!(cam_boot_status & INWARD_CAM_STATUS_MASK))) {
        LOG_E (TAG, "Inward cam boot is not successful");
        cam_record_service_obj->send_err_msg(SM_E_NDC_INIT_CAM_FAIL, CAMERA_POSITION_BACK, "Inward cam Fail");
        cam_crash_status[CAMERA_POSITION_BACK] = true;
        is_cam_boot_failed = true;
    }

    if (cams_enabled[CAMERA_POSITION_LEFT] && (!(cam_boot_status & LEFT_CAM_STATUS_MASK))) {
        LOG_E (TAG, "Left cam boot is not successful");
        cam_record_service_obj->send_err_msg(SM_E_NDC_INIT_CAM_FAIL, CAMERA_POSITION_LEFT, "Left cam Fail");
        cam_crash_status[CAMERA_POSITION_LEFT] = true;
        is_cam_boot_failed = true;
    }

    if (cams_enabled[CAMERA_POSITION_RIGHT] && (!(cam_boot_status & RIGHT_CAM_STATUS_MASK))) {
        LOG_E (TAG, "Right cam boot is not successful");
        cam_record_service_obj->send_err_msg(SM_E_NDC_INIT_CAM_FAIL, CAMERA_POSITION_RIGHT, "Right cam Fail");
        cam_crash_status[CAMERA_POSITION_RIGHT] = true;
        is_cam_boot_failed = true;
    }

    if (is_cam_boot_failed == true) {
        send_cam_crash_error_cb_to_bagheera(Q_NAME, cam_crash_status);
        return false;
    } else {
        return true;
    }
}

static bool init_camera_record()
{
    realtime_camera_config_t rt_config[CAMERA_POSITION_MAX] = {0};

    for (int i = 1; i < CAMERA_POSITION_MAX; i++) {
        if (cams_enabled[i]) {
            Config_parser bag_conf(BAGHEERACONFIG_INI);
            Config_parser nd_conf_analytics(ND_CONFIG_ANALYTICS);

            if (i == CAMERA_POSITION_BACK) {
                fill_inward_rt_config(&bag_conf, &nd_conf_analytics, &rt_config[i]);

                if ((rt_config[i].enable_streaming == true) && (false == cam_rec_zmq_init((camera_pos)i, &rt_config[i]))) {
                    LOG_C(TAG, " cam_rec_zmq_init returned false; check for streaming settings ");
                }
            } else if (i == CAMERA_POSITION_DMS) {
#ifdef DMS_CAMERA_SUPPORTED
                fill_dms_rt_config(&bag_conf, &nd_conf_analytics, &rt_config[i]);

                if (rt_config[i].enable_streaming == true) {
                    /* Set up a Unix Domain socket to accept connection from bagheera service*/
                    LOG_I(TAG, "Setting a unix domain socket for accepting connection from bagheera service");
                    pthread_t socket_setup_th;
                    if (pthread_create(&socket_setup_th, NULL, socket_setup, NULL) == 0) {
                        LOG_I(TAG, "Thread created : socket_setup");
                        // Detach the thread to release resources automatically after it finishes
                        pthread_detach(socket_setup_th);
                    } else {
                        LOG_E(TAG, "Failed to create thread : socket_setup");
                    }
                }
#endif
            }

            bool rc = init_camera_record_platform(i, &rt_config[i]);
            if (rc == false) {
                LOG_E(TAG, "init_camera_record_platform failed for cam %d", i);
                return false;
            }

            rc = init_record_session_platform(i);
            if (rc == false) {
                LOG_E(TAG, "init_record_session_platform failed for cam %d", i);
                return false;
            }
        }
    }

    return true;
}

void clone_message_received( req_livestreaming_data_t *req, req_livestreaming_data_t *new_msg)
{
    new_msg->msg_type = req->msg_type;
    new_msg->length = req->length;
    strncpy(new_msg->client_id,req->client_id, sizeof(new_msg->client_id));
    new_msg->msg_idx = req->msg_idx;
    new_msg->res_reqd = req->res_reqd;

    new_msg->duration = req->duration;
    new_msg->bitrate = req->bitrate;
    new_msg->camera = req->camera;
    new_msg->fps = req->fps;
    new_msg->req_id = req->req_id;
    new_msg->msg_idx = req->msg_idx;
    new_msg->id = req->id;
    new_msg->dual_streaming = req->dual_streaming;
    new_msg->dual_streaming_active = req->dual_streaming_active;
    strncpy(new_msg->endpoint, req->endpoint, sizeof(new_msg->endpoint));
    strncpy(new_msg->resolution, req->resolution, sizeof(new_msg->resolution));
}

void *inward_cam_livestreaming_timer( void *arg)
{
    req_livestreaming_data_t *kinesis_req_msg = (req_livestreaming_data_t *)arg;

    req_livestreaming_data_t new_msg;
    clone_message_received( kinesis_req_msg, &new_msg);

    LOG_I(TAG, "In live streaming timer of inward camera");

    // int livestreaming_limit = live_stream_duration / 60;
    int duration = new_msg.duration;
    unsigned int connection_check_cnt = 4;
    while (duration > 0) {
        sleep(1);
        if ((duration % connection_check_cnt) == 0) {
            // check for connection
            if (!check_internet_exist()) {
                LOG_I(TAG, "No internet, unable to continue live streaming");
                duration = duration + 10;
                connection_check_cnt = 10;	// reduce overhead
            }
        }
        if ((live_stream_force_stop_inward == true) && (connection_check_cnt == 10)) {
            LOG_I(TAG, "break live stream timer");
            new_msg.force_stop = true;
            live_stream_force_stop_inward = false;
            live_stream_duration = 0;
            connection_check_cnt = 4;
            break;
        }
        LOG_I(TAG, "livestreaming time left %d", duration);
        duration--;
    }

    LOG_I(TAG, "live streaming time completed for inward camera");
    if (!send_msg((generic_msg_t *)&new_msg, (msg_type_t)REQ_CAMREC_STOP_LIVE_STREAMING,
				sizeof(new_msg), Q_NAME, Q_NAME, 0)) {
        LOG_E(TAG, "Cannot send message to cam_rec to stop kinesis streaming");
    }
    return NULL;
}

void livestream_framedrop_cb(int cam_num)
{
    LOG_I(TAG, "LIVE_STREAM stopping due to continous frame dropping");
    live_stream_force_stop_inward = true;
}

void send_livestreaming_completion_msg( req_livestreaming_data_t *msg_req, livestreaming_status_t livestreaming_status, live_stream_error_t error_cam_one = LS_ERR_DEFAULT, live_stream_error_t error_cam_two = LS_ERR_DEFAULT)
{
	res_livestreaming_data_t msg_res;
	msg_res.idx = msg_req->id;
	bool msg_status = false;
    msg_res.dual_streaming = msg_req->dual_streaming;
	msg_res.status = livestreaming_status;
    msg_res.error_cam_one = LS_ERR_DEFAULT;
    msg_res.error_cam_two = LS_ERR_DEFAULT;

    if(livestreaming_status != LIVE_STREAMING_DONE){
        if(msg_req->dual_streaming){
            msg_res.error_cam_one = error_cam_one;
            msg_res.error_cam_two = error_cam_two;
        }
        else {
            msg_res.error_cam_one = error_cam_one;
        }
    }
    // if(!(msg_req->dual_streaming && msg_req->dual_streaming_active == 1)){
    LOG_I(TAG,"Live streaming error codes: %d %d", msg_res.error_cam_one, msg_res.error_cam_two);
    msg_status = send_msg((generic_msg_t*)&msg_res, RES_LIVE_STREAM, sizeof(msg_res), Q_NAME, "AWSIOT", 0);
    if(msg_status == false){
        LOG_E(TAG,"LIVE_STREAMING_DONE for inward camera not sent to client, client_id: AWSIOT");
    } else {
        LOG_I(TAG,"LIVE_STREAMING_DONE for inward camera sent to client, client_id: AWSIOT");
    }
    // }
}

static void* session_filename_monitor_thread(void* arg) {
    static char prev_session_filename[GSTREAMER_NAME_LENGTH_MAX] = {0};

    while (1) {
        sleep(SESSION_FILENAME_MONITOR_INTERVAL_SEC);

        if (strcmp(session_filename, prev_session_filename) == 0) {
            LOG_I(TAG, "session_filename unchanged for %d seconds, exiting cam_rec", SESSION_FILENAME_MONITOR_INTERVAL_SEC);
            for(int i = 1; i < CAMERA_POSITION_MAX; i++) {
                if (cams_enabled[i]) {
                    cam_rec_ctx[i].stop_recording_received = true;
                    stop_record_session_platform(i);
                }
            }
            _exit(0);
        } else {
            nd_strncpy(prev_session_filename, session_filename, sizeof(prev_session_filename));
        }
    }
    return NULL;
}

void msg_loop()
{
	nd_msgq_t::nd_msg_t *msg;
	bool streaming_status = false;

	LOG_I(TAG, "Entering msg_loop() of cam_rec service");

	while (1) {
		if ((msg = msg_q->receive()) == NULL) {
			LOG_C(TAG, "Receive message failed for camera recorder service");
			continue;
		}

		msg_type_t type = get_msg_type(msg->get_buffer());

		generic_msg_t *g_msg = (generic_msg_t *)msg->get_buffer();
		if( NULL == g_msg ) {
			LOG_E(TAG, "msg->get_buffer() returned NULL");
			continue;
		}

		switch (type) {
		case REQ_CAMREC_START_NEXT_SESSION:
		    {
			    req_start_next_session_msg_t *start_next_session_msg = (req_start_next_session_msg_t *)g_msg;

			    if (start_next_session_msg->length != sizeof(req_start_next_session_msg_t)) {
				    LOG_E(TAG, "start_next_session_msg->length != sizeof(req_start_next_session_t)");
				    break;
			    }

	            if (should_move_partial_files == true) {
	                /* When cam_rec service is simply restarted without device reboot,
	                 * it is expected to send messages to bagheera service to move other
	                 * cameras partial files to sdcard.
	                 * When this was done in a separate thread, there was a possibility of
	                 * simultaneous access of camrec-ndcentral message queue by partial
	                 * thread and recording thread.
	                 */
	                move_partial_files();
	                should_move_partial_files = false;
	            }

			    strcpy(session_filename, start_next_session_msg->next_session_filename);

			    LOG_I(TAG, "START_NEXT_SESSION message received with Filename: %s", session_filename);

#ifdef DMS_CAMERA_SUPPORTED
                //Check DMS IRLED status at the end of each session.
                if(cam_rec_ctx[CAMERA_POSITION_DMS].session_frame_count > 0 && !is_dms_disconnected_and_reconnected) { // Ignore 1st session after boot.
                    LOG_I(log_tag[CAMERA_POSITION_DMS], "New session is about to start. Check for status of DMS IRLED in current session.");
                    dms_irled_event_info_t dms_irled_event_info = {};
                    dms_irled_event_info.event = DMS_IRLED_EVENT_CHECK_STATUS;
                    nd_strncpy(dms_irled_event_info.session_filename, cam_rec_ctx[CAMERA_POSITION_DMS].cur_file_name, sizeof(dms_irled_event_info.session_filename));
                    dms_irled_event_info.session_filename[0] = '0';
                
                    // Wrap the struct in an nd_msg_t
                    nd_msgq_t::nd_msg_t msg_check_status((char *)&dms_irled_event_info, sizeof(dms_irled_event_info), false);
                    if (msgq_dms_irled) {
                        if (!msgq_dms_irled->send(msg_check_status, nd_msgq_t::ND_MSG_MED)) {
                            LOG_E(log_tag[CAMERA_POSITION_DMS], "Error in sending DMS_IRLED_EVENT_CHECK_STATUS msg to msgq");
                        }
                    }
                } else {
                    LOG_I(log_tag[CAMERA_POSITION_DMS], "Ignore checking DMS IRLED status for the 1st session after boot or when DMS is disconnected and reconnected.");
                }
#endif

                session_change_inward = true;
                session_change_left = true;
                session_change_right = true;
                session_change_dms = true;

			    for (int i=1; i < CAMERA_POSITION_MAX; i++) {
				    if (cams_enabled[i]) {
					    if (cam_rec_ctx[i].state == STATE_INIT)
						    start_record_session_platform(i);
				    } else {
					    if ((cam_rec_ctx[i].state != STATE_UNINITIALIZED) && (cam_rec_ctx[i].state != STATE_EXIT))
					        stop_record_session_platform(i);
				    }
			    }
			    break;
		    }

        case REQ_CAMREC_START_LIVE_STREAMING:
            {
                req_livestreaming_data_t *kinesis_req_msg = (req_livestreaming_data_t *)g_msg;

    			LOG_I(TAG, "START_LIVE_STREAMING message received");
                if (streaming_status == false) {
                    if (!start_kinesis( kinesis_req_msg)) {
                        if(!kinesis_req_msg->dual_streaming){
                            send_livestreaming_completion_msg( kinesis_req_msg, LIVE_STREAMING_ERR, kinesis_req_msg->error);
                        }
                        else{
                            send_livestreaming_completion_msg( kinesis_req_msg, LIVE_STREAMING_ERR, LS_ERR_DEFAULT, kinesis_req_msg->error);
                        }
                        LOG_I (TAG, "start kinesis failed for inward camera");
                        break;
                    }
                    LOG_I(TAG, "live streaming duration: %d", kinesis_req_msg->duration);
                    live_stream_duration = kinesis_req_msg->duration;
                    pthread_t live_streaming;
                    if (!pthread_create (&live_streaming, NULL, &inward_cam_livestreaming_timer, g_msg)) {
                        LOG_I (TAG, "Live streaming timer thread started for inward camera");
                    }
                    streaming_status = true;
                } else {
                    LOG_E(TAG,"streaming is already requested for inward camera, cannot honor the current request");
                }

                break;
            }

        case REQ_CAMREC_STOP_LIVE_STREAMING:
            {
                req_livestreaming_data_t *kinesis_msg = (req_livestreaming_data_t *)g_msg;
                livestreaming_status_t status;
                status = LIVE_STREAMING_DONE;
                LOG_I(TAG, "STOP_LIVE_STREAMING message received for inward camera");
                send_livestreaming_completion_msg( kinesis_msg, status, kinesis_msg->error);
                if (!stop_kinesis(kinesis_msg)) {
                    LOG_E (TAG, "stop kinesis failed for inward camera");
                    status = LIVE_STREAMING_DONE;
                }
                streaming_status = false;
                break;
            }

        case PRIVACY_UPDATED:
            {
                privacy_update_msg_t *privacy_update_msg = (privacy_update_msg_t *)g_msg;
                LOG_I(TAG, "inward privacy update message received from nd_central");
                inward_privacy_status = privacy_update_msg->privacy_status;
                LOG_I(TAG, "inward privacy value changed to %d", inward_privacy_status);
                break;
            }
        case BAGHEERA_RESTART_DONE_MSG:
            {
                LOG_I(TAG, "BAGHEERA_RESTART_DONE message received, send message to bagheera that ZMQ publisher for inward camera is already created");
                send_zmqpub_create_msg_to_bagheera(Q_NAME, CAMERA_POSITION_BACK);
#ifdef DMS_CAMERA_SUPPORTED
                send_dms_connection_status_to_bagheera(Q_NAME, is_dms_connected);
#endif
                break;
            }

        case ANALYTICS_SERVICE_RESTARTED_MSG:
            {
                LOG_I(TAG, "ANALYTICS_SERVICE_RESTARTED message received");

                cam_record_ctxt *context_inward = (cam_record_ctxt *)&cam_rec_ctx[CAMERA_POSITION_BACK];
                cam_record_ctxt *context_dms = (cam_record_ctxt *)&cam_rec_ctx[CAMERA_POSITION_DMS];

                if (cams_enabled[CAMERA_POSITION_BACK]) {
                    if (context_inward->rt_config.enable_streaming) {
                        recreate_incam_rt_shared_memory();
                    }
                }
                if (cams_enabled[CAMERA_POSITION_DMS]) {
                    LOG_I(TAG, "Start dropping the DMS RT frames and wait for DMS shared memory to be recreated.");
                    drop_dms_rt_frames = true;
                }
                break;
            }
#ifdef DMS_CAMERA_SUPPORTED
        case DMSCAM_SHM_RECREATED_MSG:
            {
                LOG_I(TAG, "DMSCAM_SHM_RECREATED message received, stop dropping the DMS RT frames");
                drop_dms_rt_frames = false;
                break;
            }

        case SET_DMS_LED_MSG:
            {
                if (isDMSconnected()) {
                    set_dms_led_msg_t *dms_led = (set_dms_led_msg_t *)g_msg;
                    LOG_I(TAG, "Received SET_DMS_LED_MSG message to set %d", dms_led->led_colour);

                    dms_irled_event_info_t dms_irled_event_info = {};
                    dms_irled_event_info.event = DMS_IRLED_EVENT_SET_LED_COLOR;
                    dms_irled_event_info.led_colour = dms_led->led_colour;
                    nd_msgq_t::nd_msg_t msg_led_color((char *)&dms_irled_event_info, sizeof(dms_irled_event_info), false);
                    if (msgq_dms_irled) {
                        if (!msgq_dms_irled->send(msg_led_color, nd_msgq_t::ND_MSG_MED)){
                            LOG_E(log_tag[CAMERA_POSITION_DMS], "Unable to send DMS_IRLED_EVENT_SET_LED_COLOR message to msgq");
                        }
                    }
                }
                else {
                    LOG_E(TAG, "DMS camera is not connected!!!");
                }
                break;
            }
#endif
        case REQ_CAMREC_START_QR_SCAN:
            {
                LOG_I(TAG, "REQ_CAMREC_START_QR_SCAN message received");

                req_qr_login_scan_msg_t *qr_scan_msg = (req_qr_login_scan_msg_t *)msg->get_buffer();

                bool ret = start_QR_scan(qr_scan_msg->tags);

                if (!file_is_present(qr_scan_started_file))
                    send_qrscan_resp_to_bagheera_service(Q_NAME, RESP_CAMREC_QR_SCAN_STATUS, ret);
            }
            break;

        case REQ_CAMREC_STOP_QR_SCAN:
            {
                LOG_I(TAG, "REQ_CAMREC_STOP_QR_SCAN message received for inward camera");
                stop_QR_scan();
                break;
            }
        }
    }
}

bool deinit_camera()
{
    bool success = true;
    for (int i = 1; i < CAMERA_POSITION_MAX; i++) {
        if (false ==  stop_record_session_platform(i)) {
            success = false;
            LOG_E(TAG, "Camera %d deinit failed", i);
            continue;
        }
        LOG_I(TAG, "Camera %d deinited", i);
    }

    return success;
}

int main(int argc, char *argv[])
{
	cam_record_service_obj = NDService::get_service_obj(TAG);

    main_thread_pid = getpid();

    atexit (cam_record_service_exit);

    camrec_service_exiting = false;

    printf("Initializing logger for cam_rec service");
    bool status_log = nd_log_init( log_dir.c_str() );
    if (status_log == false) {
        printf("Unable to initialize logger for cam_rec service:: Exiting from main");
        cam_record_service_obj->send_err_msg(SM_E_NDC_LOG_INIT_FAIL, NDService::UNUSED_ERR_AUX_CODE, "unable to init logger :: Exiting from main");
    }

#ifdef ROUTE_LOGS
    route_logs( log_dir.c_str() );
#endif

    LOG_C(TAG,"#### Starting: Camera Recorder Service ####");
    nd_device_obj_init();
    //Initialize Message Queue
    bool flush = true;
    msg_q = nd_msgq_t::get_msgq( Q_NAME, nd_msgq_t::ND_MSGQ_SERVER, flush);
    if (msg_q == NULL) {
        LOG_E(TAG,"Could not initialize message queue of camera recorder service, Exiting");
        return -1;
    }


    int64_t fspace = file_getfreespace("/");
    LOG_I(TAG, "Free Space : %lld", fspace);

    if( fspace < MIN_FREE_SPACE ) {
        LOG_E(TAG, "Free Space %lld is less than %ld. Exiting From main", fspace, MIN_FREE_SPACE);
        return 1;
    }

    string db_path_file = DB_PATH + "/" + DBFILE_NAME;
    if (nd_open_db(db_path_file, &db_handle) == false) {
        db_handle = NULL;
        LOG_E(TAG, "Failure to open DB");
        cam_record_service_obj->send_err_msg(SM_E_NDC_OPEN_DB_FAIL, 0, "open_DB failed" );
    }

    string db_path_file_camera_crash = DB_PATH + "/" + DBFILE_NAME_CAMERA_CRASH;
    if (nd_open_db(db_path_file_camera_crash, &db_handle_camera_crash) == false) {
        db_handle_camera_crash = NULL;
        LOG_E(TAG, "Failure to open DB for camera crash");
        cam_record_service_obj->send_err_msg(SM_E_NDC_OPEN_DB_FAIL, 0, "open_DB for camera crash failed" );
    }

    if (file_is_present("/dev/shm/nd_files_c/cam_rec_service_started") == false) {
        LOG_I(TAG,"Create a file in the shared memory space to indicate to Bagheera service about camera record service startup.");
        file_touch("/dev/shm/nd_files_c/cam_rec_service_started");

        first_after_boot = true;
    }

    bool init_cam_status = init_camera();
    if (init_cam_status == false) {
        LOG_E(TAG, "Init camera failed, Exiting");
    	return -1;
    }

    string files_folder = "/home/iriscli/files/";
    DIR *dir = opendir(files_folder.c_str());
    //Check if recording path exists, if No Exit
    if( NULL ==  dir ) {
        LOG_I(TAG, "folder %s is not present.", files_folder.c_str());
        sleep(DELAY_NO_FILES_FOLDER);
        dir = opendir(files_folder.c_str());
        if( NULL ==  dir ) {
            LOG_I(TAG, "folder %s is not present. Exiting..", files_folder.c_str());
            return -1;
        }
    }
    {
        LOG_I(TAG, "folder %s is present.", files_folder.c_str());
        closedir(dir);
    }

    if (first_after_boot == false) {
        LOG_I(TAG,"Camera record service started again after abrupt shutdown, send the message to Bagheera service to end its current session and start a new one.");
        send_split_now_msg_to_bagheera(Q_NAME, cams_enabled);
        int waiting_time = 0;
        while (file_is_present("/dev/shm/nd_files_c/nvargus_daemon_restarted") == false) {
            LOG_I(TAG, "Waiting for nvargus daemon to restart");
            usleep(50000);

            /* The session end message sent by cam_rec service is not acknowledged
             * when bagheera service is offline.
             * The same message is now sent multiple times at an interval of 10 secs
             * so that bagheera service acknowledges the message after it comes online.
             */
            waiting_time += 50;
            if (waiting_time > (10 * 1000)) { /* Waiting for 10 secs before sending one more message */
                LOG_I(TAG, "sending again the message to Bagheera service to end its current session and start a new one.");
                send_split_now_msg_to_bagheera(Q_NAME, cams_enabled);
                waiting_time = 0;
            }
        }
        LOG_I(TAG, "nvargus daemon restarted, delete the shm file");
        file_delete("/dev/shm/nd_files_c/nvargus_daemon_restarted");

        should_move_partial_files = true;
    }

    /* This condition handles the scenario in which bagheera service
     * is started after cam_rec service. */
    while ((file_is_present("/dev/shm/bagheera_reboot_token_file.bin") == false)) {
        LOG_I(TAG, "Bagheera service is not yet started, let's wait........");
        usleep(50000);
    }

    // Start session_filename monitor thread
    pthread_t session_filename_monitor_th;
    if (!pthread_create(&session_filename_monitor_th, NULL, session_filename_monitor_thread, NULL)) {
        LOG_I(TAG, "Thread created for session_filename_monitor_thread");
    }

#ifdef DMS_CAMERA_SUPPORTED
    send_dms_connection_status_to_bagheera(Q_NAME, is_dms_connected);

    // Create a msg queue and a Thread to handle DMS IRLED related operations
    msgq_dms_irled = nd_msgq_t::get_msgq(DMS_IRLED_MSGQ_NAME, nd_msgq_t::ND_MSGQ_SERVER, true);
    if (!msgq_dms_irled) {
        LOG_E(TAG, "Failed to initialize message queue\n");
    } else {
        LOG_I(TAG, "DMS_IRLED_MSGQ created successfully");
        int ret_thread = pthread_create(&dms_irled_thread, NULL, dms_irled_thread_func, NULL);
        if (ret_thread != 0) {
            LOG_E(TAG, "Failed to create dms_irled_thread: %s\n", strerror(ret_thread));
            // Cleanup
            nd_msgq_t::delete_msgq(msgq_dms_irled);
        } else {
            LOG_I(TAG, "dms_irled_thread created successfully");
            // Detach thread so resources are freed automatically, when thread exits.
            // So, NO need to call thread join later.
            pthread_detach(dms_irled_thread);
        }
    }

    //Create a thread to monitor if DMS is connected at runtime.
    if (is_dms_enabled_in_config && !is_dms_connected) {
        pthread_t dms_connection_monitor_thread;
        int ret_thread = pthread_create(&dms_connection_monitor_thread, NULL, dms_connection_monitor_thread_func, NULL);
        if (ret_thread != 0) {
            LOG_E(TAG, "Failed to create dms_connection_monitor_thread: %s\n", strerror(ret_thread));
        } else {
            LOG_I(TAG, "dms_connection_monitor_thread created successfully");
            pthread_detach(dms_connection_monitor_thread);
        }
    }
#endif

    bool init_cam_record_status = init_camera_record();
    if (init_cam_record_status == false) {
        LOG_E(TAG, "Init camera record failed, Exiting");
        return -1;
    }

    // create audio playback for inward live streaming socket
    init_live_streaming_audio_files();
    create_audio_socket();

    //Get into a message loop
    msg_loop();     //Get blocked here

    if (deinit_camera() == false) {
        LOG_E(TAG,"Deinit camera failed, Exiting");
        return -1;
    }

    nd_close_db(db_handle);
    db_handle = NULL;
    nd_close_db(db_handle_camera_crash);
    db_handle_camera_crash = NULL;
    LOG_I(TAG,"Exiting: gracefully");

#ifdef DMS_CAMERA_SUPPORTED
    if (msgq_dms_irled) {
        nd_msgq_t::delete_msgq(msgq_dms_irled);
    }
#endif
    cam_record_service_obj->release_service_obj();

    return 0;
}
