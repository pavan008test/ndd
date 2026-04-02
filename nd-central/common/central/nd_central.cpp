/* Copyright (C) 2016 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Arun V <arun.vj@netradyne.com>, October 2016
 */

#include <cstring>
#include <iostream>
#include <sstream>
#include <fstream>
#include <list>
#include <iomanip>
#include <algorithm>

#include <unistd.h>
#include <atomic>

#include "nd_cb_utils.h"
#include <signal.h>
#include <sys/time.h>
#include <sys/reboot.h>
#include "nd_central.h"
#include <log.h>
#include <nd_task.h>
#include <svc.h>
#include <system_utils.h>
#include <sys/prctl.h>
#include <sys/sysinfo.h>
#include "device_mode.h"
#include "nd_file_utils.h"
#include "nd_cam_utils.h"
#include "gst_recorder.h"
#include "irled_api.h"
#include "service_utils.h"
#include "ndmb/nd_msg_interface.h"
#include "ndmb/nd_mbclient.h"
#include "ndmb/nd_mbserver.h"
#include <jansson/jansson.h>
#include <future>
#include <frameinfo.h>
#include <nd_ext_cam_utils.h>
#include <nd_time.h>
#include <led_utils.h>
#include <openssl/evp.h>
#include <nd_auth_openssl.h>
#include <nd_db_utils.h>
#include <nd_prop_utils.h>
#ifdef KRAIT
#include <nd_msp_utils.h>
#endif
#include <nd_net_utils.h>
#include <boost/interprocess/streams/vectorstream.hpp>
#include <healthstats_utils.h>
#include <unordered_map>
#include <audio.pb.h>
#include <zmq.h>
#include "nd_messenger.h"
#include "nd_mount_path.h"
#include <photodiode_dev.h>
#include "wake_up_reason.h"
#include <nd_auth_utils.h>
#include <cmath>
#include <mutex>
#include <nd_config_read_utils.h>
#include "nd_tinyalsa.h"
#include <nd_utils.h>
#include <service_utils.h>
#include <nd_server.h>
#include <deque>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

#define NO_SDCARD

#ifdef __cplusplus
extern "C" {
#endif

#ifdef BAGHEERA2
#include "adc_api.h"
#include "power_state_api.h"
#endif

#include "gpio_api.h"
#ifdef BAGHEERA2
#include "nd_gpio.h"
#elif KRAIT
#include "gpio_icdc.h"
#endif
#include <zipper.h>

#ifdef __cplusplus
}
#endif

using namespace zipper;

#define ND_SOCKET_INI "/home/ubuntu/.nddevice/latest/nd_core_common.ini"

#define ROUTE_LOGS

#undef PLAY_BEEP

#ifdef PLAY_BEEP
static string boot_status_file="/home/ubuntu/.nddevice/boot_status";
static string beep_file="/home/ubuntu/.nddevice/latest/service/bagheera/beep.wav";
#endif

static const string ld_extn=".ld.mp4";
static volatile bool fnamecb_invoked = false;
/* Made it space by default, instead of empty string, as string find function will return success always for empty string */
string partial_session_name = " ";

static const string dp_extn=".dp.mp4";

static const string ea_extn=".0_ea.jpeg";

static const int ASSUMED_FULL_SESSION_DURATION = 60;
static const int UNKNOWN_VIDEO_DURATION = -1;

static const string bagheera_reboot_token_file = "/dev/shm/bagheera_reboot_token_file.bin";
static const string lpw_no_record_persistent_file = "/home/ubuntu/.nddevice/lpw_no_record.bin";
static const string offduty_mode_activated_file = "/home/ubuntu/.nddevice/offduty_mode_activated_file.bin";

static const float invalid_speed_threshold = 2.0;
static const float invalid_accuracy_threshold = 10.0;
/* The below file is used to maintain the privacy state. When device is not in offduty mode then the overall device privacy will be stored here
   and when device goes into offduty mode the updated privacy state due to speed/ignition changes will be stored here (considering the situation
   as there is no Off-duty mode)
   PURPOSE: This file will help to restore the correct privacy state after offduty mode is deactivated
*/
static const string privacy_state_file_global = "/home/ubuntu/.nddevice/privacy_state_global.bin";
#ifdef KRAIT
static const string installer_scan_active_file = "/dev/shm/installer_scan_ongoing";
#else
static const string installer_scan_active_file = "/dev/shm/nd_files_c/installer_scan_ongoing";
#endif
static const char installer_queue_name[] = "installer_queue";
static const uint16_t installer_scan_max_blink_timeout_sec = 240;

#ifdef BAGHEERA2
static const string default_privacy_activated_audio = "/home/ubuntu/autocam/audio/nd_debug2/privacy_mode_is_activated_en_f.wav";
static const string default_privacy_deactivated_audio = "/home/ubuntu/autocam/audio/nd_debug2/privacy_mode_is_deactivated_en_f.wav";
static const string default_offduty_activated_audio = "/home/ubuntu/autocam/audio/nd_debug2/off_duty_driving_mode_is_activated_en_f.wav";
static const string default_offduty_deactivated_audio = "/home/ubuntu/autocam/audio/nd_debug2/off_duty_driving_mode_is_deactivated_en_f.wav";
static const string default_enhanced_privacy_activated_audio = "/home/ubuntu/autocam/audio/nd_debug2/enhanced_privacy_mode_with_inward_camera_in_local_mode_is_activated_en_f.wav";
#elif KRAIT
static const string default_privacy_activated_audio = "/data/nd_files/autocam/audio/nd_debug2/privacy_mode_is_activated_en_f.wav";
static const string default_privacy_deactivated_audio = "/data/nd_files/autocam/audio/nd_debug2/privacy_mode_is_deactivated_en_f.wav";
static const string default_offduty_activated_audio = "/data/nd_files/autocam/audio/nd_debug2/off_duty_driving_mode_is_activated_en_f.wav";
static const string default_offduty_deactivated_audio = "/data/nd_files/autocam/audio/nd_debug2/off_duty_driving_mode_is_deactivated_en_f.wav";
static const string default_enhanced_privacy_activated_audio = "/data/nd_files/autocam/audio/nd_debug2/enhanced_privacy_mode_with_inward_camera_in_local_mode_is_activated_en_f.wav";
#endif

static unordered_map<string, int64_t> sessionMapCount;
static std::mutex sessionMapCountMutex;

NDMBServer server(SERVICE_APM);
#define LINE_LENGTH 256
#define OTHER_FPS 15

#ifdef BAGHEERA2
#define BLINKING_LED ND_GPIO_PWR_LED_G
#define BLINKING_LED_OFF_DUTY ND_GPIO_PWR_LED_R
#define INST_BLINKING_LED ND_GPIO_PWR_LED_B
#elif KRAIT
#define BLINKING_LED SYS_LEFT_GREEN
#define BLINKING_LED_OFF_DUTY SYS_LEFT_RED
#define INST_BLINKING_LED SYS_LEFT_BLUE
#endif

pthread_mutex_t crash_logs_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t audio_partial_file_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t meta_partial_file_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t live_stream_flag = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t file_start_time_vec_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t inst_scan_led_mutex = PTHREAD_MUTEX_INITIALIZER;

static obd_protocol_t obd_protocol = INIT_PROTOCOL;
can_status_t can_status_updated = CAN_UNKNOWN_ERROR, can_status = CAN_UNKNOWN_ERROR;
can_connectivity_status_t can_connectivity_status = CAN_STATUS_DISCONNECTED;
can_src_t can_src = CAN_STATUS_DISABLED;
char obd_vin[MAX_SIZE_OF_VIN]={0};
char configured_obd_vin[MAX_SIZE_OF_VIN]={0};
char can_firmware_ver[CAN_FW_VER_LEN] = {0};
char can_sn[CAN_SN_LEN] = {0};
char can_model[CAN_MODEL_LEN] = {0};
int engine_status = ENGINE_STATE_UNKNOWN ;

int fuel_report = 0;
int idling_report = 0;
int main_thread_pid = 0;
int NUM_CAMERA_BOOT_STATUS_RETRY = 5; //Try resetting Camera 5 times
int NUM_SIDE_CAMERA_BOOT_STATUS_RETRY = 3; //Try resetting side Camera 3 times
int DELAY_CAMERA_BOOT_STATUS_CHECK = 2; // Delay after camera reset before checking boot status
int DELAY_SIDE_CAMERA_BOOT_STATUS_CHECK = 1; // Delay after side camera reset before checking boot status
static int64_t service_start_time;
static const int64_t ONE_MICRO_IN_NANO = 1000;
static const int64_t ONE_MILLI_IN_MICRO = 1000;
int64_t offduty_mode_activation_time = 0;

static uint64_t g_session_frame_count[CAMERA_POSITION_MAXIMUM] = {0,0,0,0,0,0,0,0,0};

NDService *nd_service_obj; //nd service object, to detect crashes
ND_DeviceFactory *nd_device_obj = NULL; // nd device object based on deviceType
static string CIRCULAR_BUFFER_PATH = "";
static string CIRCULAR_BUFFER_PATH_EA = "";

// future has to be global always
static std::future<void> nd_out_sess_dir_fut;
static std::future<void> user_alert_result;
static std::future<void> inst_scan_indicate_result;
static const int64_t MIN_FREE_SPACE = 256 * 1024 * 1024; //256 MB of minimum free space for bagheera to run
static const int DELAY_MAIN_EXIT = 2 * 60; //If minimum free space is not available exit after this delay in secs.
static const int DELAY_NO_FILES_FOLDER = 30; //when file_mkdir for recording path fails exit after this delay in secs.
static string ND_INPUT_PATH = "/home/iriscli/ND_INPUT/";
static const string ND_OUTPUT_PATH = "/home/iriscli/ND_OUTPUT/";
static const string UPL_BASE_PATH = "/home/iriscli/saveMP4/";
static const string CAMERA_CRASH_DB_TABLE = "CAMERA_CRASH_DB";
static const string SIDE_CAM_CRASH_INFO_DB_TABLE = "SIDE_CAM_CRASH_INFO";

static const int TOLERANCE_VIDEO_DURATION_MS = 65000;

static const string OUTCAM_MIRROR_ENABLED = "4";
static const string outcam_mirror_status_cmd = "/home/ubuntu/.nddevice/latest/service/bagheera/outcam_mirror_status | cut -d ' ' -f4";

#ifdef BAGHEERA
static const string native_cam_service = "nvcamera-daemon.service";
static const string dump_camera_daemon_logs_cmd = "journalctl -u nvcamera-daemon.service";
#elif BAGHEERA2
static const string native_cam_service = "nvargus-daemon.service";
static const string dump_camera_daemon_logs_cmd = "journalctl -u nvargus-daemon.service";
#elif KRAIT
static const string dump_camera_daemon_logs_cmd = "journalctl -u qmmf-server";
#endif

static const string dump_meminfo_cmd = "cat /proc/meminfo";

static const string QNAME_CMF = "q_nd_central_cmf"; // queue for copy_or_move_files

#ifdef BAGHEERA2
static const string isp_input_mode_cmd = GPIO_TEST_APP_INPUT_MODE(OUT_CAM_GPIO); // "/bin/vendor/gpio_test -n 461 -i";
static const string isp_reset_cmd = GPIO_TEST_APP_SIG(OUT_CAM_GPIO, 0); // "/bin/vendor/gpio_test -n 461 -s 0";
#endif

static const int CAMERA_RESET_DURATION = 100 * 1000; // Giving enough time(100 ms) to reset camera sensors and ISP

// Declaration
static void reset_isp();

#ifdef BAGHEERA
static const string inward_cam_reset_cmd = GPIO_TEST_APP_SIG(IN_CAM_GPIO, 0); // "/bin/vendor/gpio_test -n 426 -s 0";
static const string inward_cam_oo_reset_cmd = GPIO_TEST_APP_SIG(IN_CAM_GPIO, 1); // "/bin/vendor/gpio_test -n 426 -s 1";

static const string side_cams_reset_cmd = GPIO_TEST_APP_SIG(SIDE_CAM_GPIO, 0); // "/bin/vendor/gpio_test -n 457 -s 0";
static const string side_cams_oo_reset_cmd = GPIO_TEST_APP_SIG(SIDE_CAM_GPIO, 1); //  "/bin/vendor/gpio_test -n 457 -s 1";
#elif BAGHEERA2
static const string inward_cam_reset_cmd = GPIO_TEST_APP_SIG(IN_CAM_GPIO, 0); // "/bin/vendor/gpio_test -n 277 -s 0";
static const string inward_cam_oo_reset_cmd = GPIO_TEST_APP_SIG(IN_CAM_GPIO, 1); // "/bin/vendor/gpio_test -n 277 -s 1";

static const string side_cams_reset_cmd = GPIO_TEST_APP_SIG(SIDE_CAM_GPIO, 0); // "/bin/vendor/gpio_test -n 457 -s 0";
static const string side_cams_oo_reset_cmd = GPIO_TEST_APP_SIG(SIDE_CAM_GPIO, 1); //  "/bin/vendor/gpio_test -n 457 -s 1";
static const string right_cam_retimer_cmd = GPIO_TEST_APP_SIG(RIGHT_CAM_RETIMER_GPIO, 1); // "/bin/vendor/gpio_test -n 267 -s 0";
static const string right_cam_oo_retimer_cmd = GPIO_TEST_APP_SIG(RIGHT_CAM_RETIMER_GPIO, 0); //  "/bin/vendor/gpio_test -n 267 -s 1";
static const string left_cam_retimer_cmd = GPIO_TEST_APP_SIG(LEFT_CAM_RETIMER_GPIO, 1); // "/bin/vendor/gpio_test -n 443 -s 0";
static const string left_cam_oo_retimer_cmd = GPIO_TEST_APP_SIG(LEFT_CAM_RETIMER_GPIO, 0); //  "/bin/vendor/gpio_test -n 443 -s 1";
#elif KRAIT
static const string inward_cam_reset_cmd = "gpio_test -n 152 -s 0";
static const string inward_cam_oo_reset_cmd = "gpio_test -n 152 -s 1";

static const string side_cams_reset_cmd = "gpio_test -n 149 -s 0";
static const string side_cams_oo_reset_cmd = "gpio_test -n 149 -s 1";
#endif

#ifdef BAGHEERA
static const string isp_boot_status_cmd = "/home/ubuntu/.nddevice/latest/service/bagheera/ov491_reg_access read 0x31a4 | cut -d':' -f 4";
static const string side_cams_boot_status_cmd = GPIO_TEST_APP_GET(SIDE_CAM_GPIO); // "/bin/vendor/gpio_test -n 457 -g | grep ':' | cut -d':' -f 3";
#elif BAGHEERA2
static const string isp_boot_status_cmd = "/home/ubuntu/.nddevice/latest/service/bagheera/i2c_transfer 0 16 2 24 31a4 | cut -d' ' -f 7";
static const string reset_isp_boot_status_cmd0 = "/home/ubuntu/.nddevice/latest/service/bagheera/i2c_transfer 1 16 2 24 3516 0";
static const string reset_isp_boot_status_cmd1 = "/home/ubuntu/.nddevice/latest/service/bagheera/i2c_transfer 1 16 2 24 3058 2";
static const string reset_isp_boot_status_cmd2 = "/home/ubuntu/.nddevice/latest/service/bagheera/i2c_transfer 1 16 2 24 3059 0";
static const string inward_boot_status_set_cmd = "i2cset -f -y 2 0x3c 0xfd 0x00" ;
static const string inward_boot_status_cmd = "i2cget -f -y 2 0x3c 0x02" ;
static const string inward_boot_status2_cmd = "i2cget -f -y 2 0x3c 0x03" ;
static const string left_cam_boot_status_cmd = "/home/ubuntu/.nddevice/latest/service/bagheera/i2c_transfer 0 16 2 36 300a | cut -d' ' -f 7";
static const string left_cam_boot_status2_cmd = "/home/ubuntu/.nddevice/latest/service/bagheera/i2c_transfer 0 16 2 36 300b | cut -d' ' -f 7";
static const string right_cam_boot_status_cmd = "/home/ubuntu/.nddevice/latest/service/bagheera/i2c_transfer 0 16 7 36 300a | cut -d' ' -f 7";
static const string right_cam_boot_status2_cmd = "/home/ubuntu/.nddevice/latest/service/bagheera/i2c_transfer 0 16 7 36 300b | cut -d' ' -f 7";
static const string right_mipi_status_cmd = "i2cdump -f -y 6 0x6c | grep \"44 50 48 59 31 30 30 20\"";
static const string left_mipi_status_cmd = "i2cdump -f -y 8 0x6c | grep \"44 50 48 59 31 30 30 20\"";

static const string get_isp_left_status_cmd = "/home/ubuntu/.nddevice/latest/service/bagheera/i2c_transfer 0 16 2 36 0100 | cut -d' ' -f 7";
static const string get_isp_right_status_cmd = "/home/ubuntu/.nddevice/latest/service/bagheera/i2c_transfer 0 16 7 36 0100 | cut -d' ' -f 7";
#endif

static const int CAM_BOOT_STATUS_CHECK_TIMEOUT = 5;
static const int ND_REOPERATE_MAX_TIMEOUT = 10;

static const string METADATA_OBS_PATH = "/home/ubuntu/.nddevice/inertial_obs";
static const string METADATA_OBS_EXT = ".json";
static const string METADATA_ZIP_EXT = ".zip";

static const int OUTWARD_CAM_STATUS_MASK = 0x1;
static const int INWARD_CAM_STATUS_MASK = 0x2;
static const int SIDE_CAM_STATUS_MASK = 0x4;
static const int LEFT_CAM_STATUS_MASK = 0x4;
static const int RIGHT_CAM_STATUS_MASK = 0x8;

static const string BAGHEERA3_OUTWARD_CAM_FLIP_REG_READ_COMMAND = "i2c_transfer 0 16 2 0x24 0x13ca | awk '{print $NF}' | sed 's/\\.$//'";
static const string BAGHEERA3_INWARD_CAM_FLIP_REG_READ_COMMAND = "i2c_transfer 0 8 2 0x3d 0x3f | awk '{print $NF}' | sed 's/\\.$//'";
static const string BAGHEERA3_LEFT_CAM_FLIP_REG_READ_COMMAND = "i2c_transfer 0 8 7 0x3c 0x3f | awk '{print $NF}' | sed 's/\\.$//'";
static const string BAGHEERA3_RIGHT_CAM_FLIP_REG_READ_COMMAND = "i2c_transfer 0 8 7 0x3d 0x3f | awk '{print $NF}' | sed 's/\\.$//'";

static bool dp_enabled = false;
int pow_on_off_reason = 0;

static int ea_enabled = 0;
static int ea_outward_enabled = 0;

static bool audio_enable = false;
static bool audio_encryption = true;
static bool audio_running = false;

static const string AUDIO_PCM_FILE_PATH = "/dev/shm/nd_files_c/audio_raw.pcm";
static const string AUDIO_ENCODE_FILE_PATH = "/dev/shm/nd_files_c/";

static const string AUDIO_PARTIAL_PCM_FILE_BASE_PATH = "/home/iriscli/files" ;
static const string AUDIO_PARTIAL_FILE_SUFFIX = "_partial.pcm" ;
static const string META_PARTIAL_FILE_SUFFIX = "_partial.csv" ;

static bool video_encryption = true;
static const string video_encryption_default = "true";
static const int OPERATE_DELTA_SIZE = 80; // File size increase amount when operated

static const string vm_default_enable = "true";
static const string vm_default_dirty_writeback_centisecs = "100";
static const string vm_default_dirty_expire_centisecs = "100";
static const int vm_min_dirty_writeback_centisecs = 10;
static const int vm_min_dirty_expire_centisecs = 10;
static const string vm_path_dirty_writeback_centisecs = "/proc/sys/vm/dirty_writeback_centisecs";
static const string vm_path_dirty_expire_centisecs = "/proc/sys/vm/dirty_expire_centisecs";

static const int AUDIO_ENCODE_TIME_MAX = 20;
static const int GET_FLIP_MIRROR_REGISTER_MAX_TIME = 2;
static const int SET_LED_TIME_MAX = 2;
static const int AUDIO_PAUSE_TIME = 2;
cam_crash_status_t cam_crash_status = {false, false, false, false, false, false, false, false, false};

static bool imu_init_done = false;
static bool is_adc_updated = false;
static bool is_nd_config_corrupted = false;

static const int default_obd_data_retry_count = 300;
static const int default_obd_data_retry_time  = 10;
static int obd_data_retry_count = default_obd_data_retry_count;
static int obd_data_retry_time = default_obd_data_retry_time;
static const int default_gps_data_retry_count = 300;
static const int default_gps_data_retry_time  = 10;

//default value for copy_status_for_cams , edit_status_for_cams and remove_status_for_cams
const int default_status_value = 0x10F;
int copy_status_for_cams = default_status_value;
int edit_status_for_cams = 0x0;
int remove_status_for_cams = default_status_value;
bool copy_or_move_files_flag = false;
bool session_contains_usr_alert = false;
bool imu_outage = true;
bool gps_outage = true;

static bool inward_ld = true;
static bool outward_ld = true;
static bool dms_ld = false;

//array for healthstat_reason
string healthstat_reason[CAMERA_POSITION_MAXIMUM] =  {"", "", "", "", "", "", "", "", ""};
// DMS variables
static const string DMS_NODE = "/dev/dms_h264";
constexpr size_t HEX_BUFFER_SIZE = 8;

#ifdef KRAIT
/**** ADC Required Declaration and Definition ****/
typedef struct obd_adc_thread_info{
    pthread_t obd_thread_id;
    char      *argv;
}obd_adc_thread_info_t;
void* obd_adc_subs_funcptr(void* );
bool read_adc_status_and_channel_two_data(float *, int *);
static const int32_t THREAD_TIMEOUT = 1;
static const int32_t COND_VAR_TIMEOUT = 3;

ndmbmsg_obd_adc_data_t *obd_data_ptr=NULL;
pthread_cond_t obd_data_cv;
pthread_mutex_t obd_data_lock;

/********************************************/
#endif

std::atomic<bool> end_of_session_atomic;
std::atomic<bool> bagheera_service_exiting;
int valid_GPS_entries = 0;
static int gps_auto_start_counter = 0;
static bool gps_failure = true;
static std::future<void> gps_auto_gps_config;

static int const udidSize  = 4;
static int const sessionCountSize = 6;

//kinesis
static bool live_streaming_enabled = false;
static int live_stream_duration = -1;
static volatile bool live_stream_force_stop_outward = false;
static volatile bool live_stream_force_stop_inward = false;

extern bool button_long_press_required_for_privacy ;

static bool ublox_enabled = false;
static device_mode_t device_mode_global_partial;
static int partial_video_len_sec;

static bool first_after_boot = true;
const int64_t month_in_seconds = 30*24*60*60;
privacy_reason_t privacy_reason = REASON_NO_PRIVACY;

// extern from nd_tinyalsa.cpp
extern vector<AudioRequest> audio_requests;
void activate_enhanced_privacy_mode(string cur_session_fname);

#ifdef BAGHEERA2
static const string crash_log_cmds[]=
{
    //Print task status
    "top -b -n 1",
    //Print GPU load
    "echo GPU load: && cat /sys/devices/gpu.0/load && echo GPU Frequency && cat /sys/devices/gpu.0/devfreq/17000000.gp10b/cur_freq",
    //Read cam reset status
    GPIO_TEST_APP_GET(SIDE_CAM_GPIO),
    GPIO_TEST_APP_GET(IN_CAM_GPIO),
    GPIO_TEST_APP_GET(OUT_CAM_GPIO),
    //Read ignition and battery voltage
    "/home/ubuntu/.nddevice/latest/service/bagheera/thermal_adc",

    dump_meminfo_cmd,
    dump_camera_daemon_logs_cmd,
    //Camera I2C reads
    "/home/ubuntu/.nddevice/latest/service/bagheera/camera_check | grep cam",
    "/home/ubuntu/.nddevice/latest/service/bagheera/incam_reg_dump",
    "/home/ubuntu/.nddevice/latest/service/bagheera/leftcam_reg_dump",
    "/home/ubuntu/.nddevice/latest/service/bagheera/rightcam_reg_dump",
};
#elif KRAIT
static const string crash_log_cmds[]=
{
    //Print task status
    "top -b -n 1",
    //Print GPU load
    "echo GPU load: && cat /sys/devices/platform/host1x/gpu.0/load && echo GPU Frequency && cat /sys/devices/platform/host1x/gpu.0/devfreq/gpu.0/cur_freq",
    //Read cam reset status
    "gpio_test -n 149 -g",
    "gpio_test -n 152 -g",
    //Read ignition and battery voltage
    "/home/ubuntu/.nddevice/latest/service/bagheera/thermal_adc",

    //Camera I2C reads
    "/home/ubuntu/.nddevice/latest/service/bagheera/camera_check | grep cam",
    dump_meminfo_cmd,
    dump_camera_daemon_logs_cmd,
    "/home/ubuntu/.nddevice/latest/service/bagheera/incam_reg_dump",
    "/home/ubuntu/.nddevice/latest/service/bagheera/leftcam_reg_dump",
    "/home/ubuntu/.nddevice/latest/service/bagheera/rightcam_reg_dump",
};
#endif

struct VideoFlipRegisterInput {
    int cam_num;
    string hex_val;  // register output
};

static const string mipi_status_logs[] =
{
    //MIPI status
//  "md_test 0x54081248 4",
//  "md_test 0x54081a48 4",
//  "md_test 0x54080a48 4"
};

static const string boot_logs_cmds[]=
{
    //Read Cam 0 ISP boot status: 0xAA for success
    "/home/ubuntu/.nddevice/latest/service/bagheera/ov491_reg_access read 0x31a4",

    //List of open cameras
    "lsof -t /dev/video0",
    "lsof -t /dev/video1",
    "lsof -t /dev/video2",
    "lsof -t /dev/video3",

    //Print task status
    "top -b -n 1",
    //Print GPU load
#ifdef BAGHEERA2
    "echo GPU load: && cat /sys/devices/gpu.0/load && echo GPU Frequency && cat /sys/devices/gpu.0/devfreq/17000000.gp10b/cur_freq",
#elif KRAIT
    "echo GPU load: && cat /sys/devices/platform/host1x/gpu.0/load && echo GPU Frequency && cat /sys/devices/platform/host1x/gpu.0/devfreq/gpu.0/cur_freq",
#endif
};

static const string trace_logs_rename[]=
{
    "for f in /home/ubuntu/.nddevice/log/ndcentral/*.tar.gz; do mv -- $f $f.log; done",
};

static const string trace_logs_delete[]=
{
    "for f in /home/ubuntu/.nddevice/log/ndcentral/*.tar.gz;  do rm $f; done",
    "for f in /home/ubuntu/.nddevice/log/ndcentral/*.tar.gz.log;  do rm $f; done",
};

static const string cam_audio_clk_disable_cmd = "mw_test 0x70003158 0x10 && mw_test 0x70003180 0x10";
static const string cam_audio_clk_enable_cmd = "mw_test 0x70003158 0x00 && mw_test 0x70003180 0x00";

using namespace std;

static const char *TAG="NDC";

nd_central_ctx ctx;

/* for ignition based privacy handling, based on duration set */
static bool ignition_on_received = false;
static int64_t post_ignition_on_target_time = 0;
static bool ignition_off_received = false;
static int64_t post_ignition_off_target_time = 0;

// This is used to set the speed privacy after privacy enable/disable time seconds from ignition ON/OFF
static int64_t speed_privacy_activate_target_time = 0;
static bool privacy_activate_update_received = false;
static int64_t speed_privacy_deactivate_target_time = 0;
static bool privacy_deactivate_update_received = false;

Gps::gps_data_t def_gps = {
    .valid = false,
    .latitude = invalid_lat,
    .longitude = invalid_long,
    .altitude = 0,
    .speed = 0,
    .bearing = 0,
    .accuracy = 0,
    .timestamp = get_system_time(),
    .system_timestamp = get_system_time(),
    .flags = 0,
    .privacy_enabled = false,
    .reserved = {}
};

Ublox::ublox_gps_data_t def_ublox_gps = {
    .valid = false,
    .latitude = invalid_lat,
    .longitude = invalid_long,
    .altitude = 0,
    .speed = 0,
    .bearing = 0,
    .accuracy = 0,
    .timestamp = get_system_time(),
    .system_timestamp = get_system_time(),
    .flags = 0
};

namespace DriveSimulation
{
    bool drive_simulation_enabled = false;
#ifdef AUTOMATION
    static bool dummy_imu_callback(Imu::val_t val_a, Imu::val_t val_g, Imu::val_t val_m);
    int receiveData_IMU();
    void handle_client(int clientSocket);
    void start_dts_imu_msg_loop();
    std::deque<std::tuple<Imu::val_t, Imu::val_t, Imu::val_t>> imu_data_queue;
    bool isImuQueueEmpty = true;
#endif
}

static vector <pair <string, int64_t>> file_start_time_vec_pair;

#ifdef BAGHEERA2
static int NUM_CAMERAS = 4;
#elif KRAIT
static int NUM_CAMERAS = 2;
#endif

static int NUM_EXT_CAMERAS = 4;

int startmeta_count = 0;
#define SIG_INFO_STRING_MAX_LEN 300

static const string Q_NAME_BTFV = "BTFV";
static const string Q_NAME_SPEED = "SPEED";
static const string Q_NAME_time_sync = "TIME_SYNC";
static const string Q_NAME_power_monitor = "q_power_monitor";
static const string Q_NAME_EXT_CAM = "EXT_CAM";
static const string Q_NAME_AWS_PUB = "AWSIOT_PUB";
static const string Q_NAME_SCH_MGR = "SCH";
static const string Q_NAME_CAMREC = "q_cam_rec";
static const string Q_NAME_OBD_PUB = "OBD_PUB";
static const string Q_NAME_UPLOADER = "UniUpload";
static const string Q_AUDIOPLAYBACK = "q_audioplayback";
static int msg_idx = 0;
static int handles = 0;
//DIS related variables

static volatile int gps_switch = 0;
static volatile bool prev_gps_valid = false;
static volatile int ublox_gps_switch = 0;
static volatile bool prev_ublox_gps_valid = false;

pthread_mutex_t dis_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t rt_session_id_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t rt_gps_mutex = PTHREAD_MUTEX_INITIALIZER;

static const int IRLED_ON_HR_DEFAULT = 17;  // 5 pm
static const int IRLED_OFF_HR_DEFAULT = 7;  // 7 am

static int IRLED_ON_HR = IRLED_ON_HR_DEFAULT;
static int IRLED_OFF_HR = IRLED_OFF_HR_DEFAULT;

pthread_mutex_t irled_mutex = PTHREAD_MUTEX_INITIALIZER;

enum  irled_reason_t {
    REASON_PRIVACY = 0,
    REASON_AMBIENT_LIGHT,
    REASON_TIME,
    REASON_QR_SCAN,
    REASON_INVALID
};

static const int default_irled_level = 2;

static const string fname_update_clients []=
{
    //Q_NAME_BTFV
};

// Installer Scan Indicating LED Thread
static std::atomic<bool> inst_scan_led_blinking{false};
static std::atomic<bool> inst_scan_cancel{false};
static std::atomic<int64_t> inst_scan_blink_deadline_sec{0};
static pthread_t inst_scan_thread_id = 0;


static bool start_meta(string fname, uint64_t epoch_time_micro, uint64_t raw_time_micro);
static bool stop_meta(char *fname, int cam_num, int flipflop, uint64_t epoch_time, uint64_t raw_time, uint64_t pts_time, bool is_ld);
static string get_fname(string header, uint64_t session_first_frame_epoch_time_ms);
static void recover_installer_scan_led_blinking_on_startup();
static void* inst_scan_led_worker(void* arg);
void mediarecorder_callback(RecordState state, uint64_t frame_time, uint64_t frame_time_extra,
                            uint64_t pts_time, void *app, uint64_t session_frame_count, bool is_ld, void* filename);
void mediarecorder_firstframe_callback (void *app);
void frame_write_cb (uint64_t frame_number, uint64_t raw_time_ns, uint64_t epoch_time_ns,  void *arg);
//bool set_irled_brightness (int brightness);
static bool enable_genmeta(string name, uint64_t epoch_time_micro, uint64_t raw_time_micro);
static bool start_photodiode();
static bool stop_camera(int cam_num);
static string record_fnamecb (void *app, uint64_t session_first_frame_epoch_time_ms);
int get_num_digital_cams_enabled();
int get_num_analog_cams_enabled();
static bool enable_all_sensors();
static bool disable_all_sensors();
bool csv_to_json(string source_path, string destination_path,
        device_mode_t &device_mode_global_partial, int &partial_video_len_sec);

static const int CRASH_LOG_TIMEOUT = 5;
static const int TRACE_LOG_TIMEOUT = 10;
static const int AUDIO_PLAYBACK_THREAD_EXIT_TIMEOUT = 5;

static bool copy_or_move_files(char *fname, int cam_num, int flipflop, uint64_t epoch_time, uint64_t raw_time, uint64_t pts_time, bool is_ld);
static int check_outward_camera_boot_status();

static void read_ext_camera_config();

bool deinit_gps();

// RT functions
bool send_msg_analytics_imu(Imu::val_t val_a, Imu::val_t val_g, Imu::val_t val_m);
bool send_msg_analytics_gps(Gps::gps_data_t val);
bool send_msg_analytics_gps_geo_fence(Gps::gps_data_t val);
int fill_rt_config(cam_pos_t cam_pos, Config_parser* bag_conf,
                    Config_parser* bag_conf_mod, realtime_camera_config_t* rt_config);
bool fill_rt_config_imu();
bool fill_rt_config_gps(string socket_name = gps_streaming_socket); // Setup GPS RT config by default
int set_rt_status(cam_pos_t cam_pos, realtime_camera_config_t* rt_config);
void *check_inward_cam_RT_thread(void *);
#ifdef DMS_CAMERA_SUPPORTED
void *socket_setup(void *arg);
void *rcv_dmabuf_fd_thread(void *);
void create_dmscam_rt_shared_memory(realtime_camera_config_t* rt_config);
void recreate_dmscam_rt_shared_memory(realtime_camera_config_t* rt_config);
#endif
void appsink_camera_callback(int64_t cb_pts, int cb_cam_pos, uint64_t raw_time_ns, uint64_t epoch_time_ns, uint64_t raw_frame_sent_time_ms, int64_t data_size, int smb_id, int64_t uid);
void livestream_framedrop_cb(int cam_num);
bool qr_scan_callback(unordered_map<string, vector<string>> qr_scan_out, int num_qr_codes_detected, uint64_t qr_frame_ts, qr_scan_status qrscan_status);
float read_adc_channel_two_data(void);

void move_other_cam_partial_files(const char *filename, int cam_num);

string fname_const_part = "";

//bool cams_enabled[DEVICE_CAMERA_POSITION_MAX] = {false, false, false, false};
bool cams_enabled[CAMERA_POSITION_MAXIMUM] = {true,false,false,false,false,false,false,false,false};

//Used to handle disabling of engine idle in the middle of a session
bool video_copied[CAMERA_POSITION_MAXIMUM] = {false,false,false,false,false,false,false,false,false};
bool video_removed[CAMERA_POSITION_MAXIMUM] = {false,false,false,false,false,false,false,false,false};
string cur_session_fnames [CAMERA_POSITION_MAXIMUM] = {"","","","","","","","",""};
int cam_crash_count[CAMERA_POSITION_MAXIMUM] = {0,0,0,0,0,0,0,0,0};
int max_cam_crash_count = 10;

//If user generated an alert duing engine idle, we have to copy all enabled
//camera files. This list is populated and used then.
bool user_alert_copy[CAMERA_POSITION_MAXIMUM] = {false,false,false,false,false,false,false,false,false};
bool highg_alert_copy[CAMERA_POSITION_MAXIMUM] = {false,false,false,false,false,false,false,false,false};

string currvid_fname = "";
string nextvid_fname = "";
string nextvid_fname_temp = "";

pthread_mutex_t nextVideoName_mutex = PTHREAD_MUTEX_INITIALIZER;

static string network_info="";
static bool stop_gps_updates_to_time_sync = false;
static bool send_gps_updates_to_awsiot = false;
static int pub_rt_session_id = 0;
static bool is_iosix_enabled = false;

// Trace logs enable / disable flag
static bool trace_logs_enable = false;

string imu_acc_data;
//std::mutex mutex_imu_data;
static const string HS_JSON_IMU = "imu";
static const string HS_JSON_GPS = "gps";
static const string HS_JSON_OBD = "obd";
static const string HS_JSON_ALERT = "alert";
// DB handler
db_handle_t* db_handle;
db_handle_t* db_handle_camera_crash = NULL;

bool vdm_enabled = false;
// Store file extended attributes if config is true
static bool use_extended_attributes = true;

#define MAX_PUBLISHER_Q_SZ 16

enum live_stream_event_t {
    INVALID = -1,
    START = 0,
    END = 1,
    MAX = 2,
    DUAL_START = 3,
    DUAL_END = 4
};

#ifdef BAGHEERA2
static const string default_live_stream_outward_start_file = "/home/ubuntu/autocam/audio/nd_debug2/live_streaming_outward_start.wav";
static const string default_live_stream_outward_end_file = "/home/ubuntu/autocam/audio/nd_debug2/live_streaming_outward_end.wav";
static const string default_live_stream_inward_start_file = "/home/ubuntu/autocam/audio/nd_debug2/live_streaming_inward_start.wav";
static const string default_live_stream_inward_end_file = "/home/ubuntu/autocam/audio/nd_debug2/live_streaming_inward_end.wav";
static const string default_dual_live_stream_start_file = "/home/ubuntu/autocam/audio/nd_debug2/dual_live_streaming_start.wav";
static const string default_dual_live_stream_end_file = "/home/ubuntu/autocam/audio/nd_debug2/dual_live_streaming_end.wav";
#elif KRAIT
static const string default_live_stream_outward_start_file = "/data/nd_files/autocam/audio/nd_debug2/live_streaming_outward_start.wav";
static const string default_live_stream_outward_end_file = "/data/nd_files/autocam/audio/nd_debug2/live_streaming_outward_end.wav";
static const string default_live_stream_inward_start_file = "/data/nd_files/autocam/audio/nd_debug2/live_streaming_inward_start.wav";
static const string default_live_stream_inward_end_file = "/data/nd_files/autocam/audio/nd_debug2/live_streaming_inward_end.wav";
static const string default_dual_live_stream_start_file = "/data/nd_files/autocam/audio/nd_debug2/dual_live_streaming_start.wav";
static const string default_dual_live_stream_end_file = "/data/nd_files/autocam/audio/nd_debug2/dual_live_streaming_end.wav";
#endif

static const string TMP_KINESIS_STREAMING_OUTWARD = "/tmp/kinesis_streaming_out";
static const string TMP_KINESIS_STREAMING_INWARD = "/tmp/kinesis_streaming_in";
extern std::mutex dualStreamingFileMutex;
std::atomic<bool> dual_streaming_audio_start_played(false);
std::atomic<bool> dual_streaming_audio_end_played(false);
const std::map<live_stream_event_t, live_stream_event_t> audioPlayedMap = {
    {START, END},
    {DUAL_START, DUAL_END}
};

static bool qr_scan_started = false;
static bool qr_scan_irled_toggle_state = false;
static pthread_t qr_scan_irled_thread;
static bool qr_scan_irled_thread_running = false;
string qr_messenger_socket = "";
string qr_messenger_topic = "";
string qr_fetcher_socket = "";
string qr_fetcher_topic = "";
NDMessenger::ClientBuilder qr_scan_data_fetcher;
NDMessenger::ServerBuilder qr_scan_data_publisher;
pthread_t qr_scan_data_fetcher_thread;
bool qr_scan_data_fetcher_thread_running = false;
void* qr_scan_data_fetcher_thread_func(void* arg);
pthread_mutex_t qr_scan_start_mutex = PTHREAD_MUTEX_INITIALIZER;
static const string qr_scan_started_file = "/dev/shm/qr_scan_started_file.bin";
static char saved_qr_scan_tags[kQRScanMaxTagPatterns][kQRScanMaxTagPatternDataLen];
int64_t qr_scan_stop_timestamp = 0;

typedef struct {
    irled_reason_t reason;
    bool value;
} irled_data;

irled_data saved_irled_data;

bool read_privacy_value_and_reason_from_file(bool &privacy, privacy_reason_t &reason, string filename)
{
    ifstream file(filename.c_str());
    if (!file.is_open()) {
        return false;
    }

    if (!(file >> privacy)) {
        file.close();
        return false;
    }

    int reason_int;
    if (!(file >> reason_int)) {
        reason = REASON_NO_PRIVACY;
    } else {
        reason = static_cast<privacy_reason_t>(reason_int);
    }

    file.close();
    return true;
}

bool write_privacy_to_file_with_reason(int value, privacy_reason_t reason, string filename)
{
    ofstream state_file_fp;
    state_file_fp.open(filename.c_str());
    if (state_file_fp.is_open()) {
        state_file_fp << value << endl;
        state_file_fp << reason << endl;
    }
    state_file_fp.close();
    return true;
}

static const string default_user_initiated_audio_alert = "/home/ubuntu/autocam/audio/nd_debug2/user_alert_triggered.wav";

static bool isDMSsupported(int deviceType)
{
    if ( (deviceType == eBagheera_3) && (file_is_present(DMS_NODE)) )
        return true;
    else
        return false;
}

bool start_qr_scan_data_fetcher_thread()
{
    const char* TAG = "qr_scan_data_fetcher";

    if (qr_scan_data_fetcher_thread_running) {
        LOG_W(TAG, "QR scan data fetcher thread is already running");
        return true;
    }

    if (qr_fetcher_socket.empty() || qr_fetcher_topic.empty()) {
        LOG_E(TAG, "QR fetcher socket or topic not configured");
        return false;
    }

    int thread_result = pthread_create(&qr_scan_data_fetcher_thread, NULL, qr_scan_data_fetcher_thread_func, NULL);
    if (thread_result != 0) {
        LOG_E(TAG, "Failed to create QR scan data fetcher thread: %d", thread_result);
        return false;
    }

    LOG_I(TAG, "QR scan data fetcher thread started successfully");
    return true;
}

static void init_qr_scan()
{
    Config_parser c(BAGHEERACONFIG_INI);
    bool is_val_overridden = false;
    bool is_driverlogin_qr_enabled = false;

    if (c.getParseStatus()) {
        if ("true" == c.getConfig("driverlogin_v2", "qr_enabled", "false", true, is_val_overridden))
            ctx.is_driverlogin_qr_enabled = true;

        if (ctx.is_driverlogin_qr_enabled == true) {
            LOG_I(TAG, "Driver login through QR code scanning is enabled");
            Config_parser sock_addr_config(ND_SOCKET_INI);
            qr_messenger_socket = sock_addr_config.getConfig("messenger_sockets", "qr_decodes", "", true, is_val_overridden);
            qr_messenger_topic = sock_addr_config.getConfig("messenger_topics", "qr_decodes", "", true, is_val_overridden);
            qr_scan_data_publisher.setServer(qr_messenger_socket);
            qr_scan_data_publisher.setTopic(qr_messenger_topic);

            LOG_I(TAG, "%s, %s", qr_messenger_socket.c_str(), qr_messenger_topic.c_str() );

            qr_fetcher_socket = sock_addr_config.getConfig("messenger_sockets", "qr_ndcentral", "", true, is_val_overridden);
            qr_fetcher_topic = sock_addr_config.getConfig("messenger_topics", "qr_ndcentral", "", true, is_val_overridden);
            qr_scan_data_fetcher.setServer(qr_fetcher_socket);
            qr_scan_data_fetcher.setTopic(qr_fetcher_topic);

            if (first_after_boot == false) {
                LOG_I(TAG, "Bagheera service is restarted after it got crashed, send message to nd_bt service so that it can re-initiate QR code scanning");
                bagheera_restart_done_msg_t msg;
                send_msg ((generic_msg_t*)&msg, BAGHEERA_RESTART_DONE_MSG, sizeof (msg), Q_NAME, Q_NAME_BTFV, 0);
            }
#ifdef BAGHEERA2
            // For Bagheera2, start the QR scan data fetcher thread here
            if (!start_qr_scan_data_fetcher_thread()) {
                LOG_E(TAG, "Failed to start QR scan data fetcher thread");
            }
#endif
        } else {
            LOG_I(TAG, "Driver login through QR code scanning is disabled");
        }
    }
}

static void init_live_streaming_audio_files()
{
    Config_parser c(BAGHEERACONFIG_INI);
    bool is_val_overridden = false;

    if (c.getParseStatus()) {
        if ((ctx.live_stream_outward_start_file = c.getConfig("live_streaming",
                        "outward_stream_start_file", "", true, is_val_overridden)) == "") {

            ctx.live_stream_outward_start_file.assign(default_live_stream_outward_start_file);
        }
        if ((ctx.live_stream_outward_end_file = c.getConfig("live_streaming",
                        "outward_stream_end_file", "", true, is_val_overridden)) == "") {

            ctx.live_stream_outward_end_file.assign(default_live_stream_outward_end_file);
        }
        if ((ctx.live_stream_inward_start_file = c.getConfig("live_streaming",
                        "inward_stream_start_file", "", true, is_val_overridden)) == "") {

            ctx.live_stream_inward_start_file.assign(default_live_stream_inward_start_file);
        }
        if ((ctx.live_stream_inward_end_file = c.getConfig("live_streaming",
                        "inward_stream_end_file", "", true, is_val_overridden)) == "" ) {

            ctx.live_stream_inward_end_file.assign(default_live_stream_inward_end_file);
        }
        if ((ctx.dual_stream_start_file = c.getConfig("live_streaming",
                        "dual_stream_start_file", "", true, is_val_overridden)) == "" ) {

            ctx.dual_stream_start_file.assign(default_dual_live_stream_start_file);
        }
        if ((ctx.dual_stream_end_file = c.getConfig("live_streaming",
                        "dual_stream_end_file", "", true, is_val_overridden)) == "" ) {

            ctx.dual_stream_end_file.assign(default_dual_live_stream_end_file);
        }
        if ("true" == c.getConfig("live_streaming",
                        "audio_notification", "false", true, is_val_overridden)){
            ctx.live_stream_audio_notification = true;
        }
    }
    LOG_I(TAG, "Live streaming outward start: %s", ctx.live_stream_outward_start_file.c_str());
    LOG_I(TAG, "Live streaming outward end: %s", ctx.live_stream_outward_end_file.c_str());
    LOG_I(TAG, "Live streaming inward start: %s", ctx.live_stream_inward_start_file.c_str());
    LOG_I(TAG, "Live streaming inward end: %s", ctx.live_stream_inward_end_file.c_str());
    LOG_I(TAG, "Live streaming audio notification: %d", ctx.live_stream_audio_notification);
}

static void initialize_settings_for_user_triggered_alerts() {
    // Generic logic for both krait and bagheera
    const bool get_override_val = true;
    bool is_val_overridden = false;
    if ("1" == ctx.nd_config_analytics->getConfig("inCabFeedback", "enable", "0", get_override_val, is_val_overridden)) {
        LOG_I(TAG, "global inCabFeedback alerts is enabled");

        ctx.user_alert_cfg.enable_status_.at(0) = ("1" == ctx.nd_config_analytics->getConfig("inCabFeedback",
                                                                                             usralert_map[0],
                                                                                             "0",
                                                                                             get_override_val,
                                                                                             is_val_overridden));
        ctx.user_alert_cfg.enable_status_.at(1) = ("1" == ctx.nd_config_analytics->getConfig("inCabFeedback",
                                                                                             usralert_map[1],
                                                                                             "0",
                                                                                             get_override_val,
                                                                                             is_val_overridden));
        ctx.user_alert_cfg.enable_status_.at(2) = ("1" == ctx.nd_config_analytics->getConfig("inCabFeedback",
                                                                                             usralert_map[2],
                                                                                             "0",
                                                                                             get_override_val,
                                                                                             is_val_overridden));
        ctx.user_alert_cfg.enable_status_.at(3) = ("1" == ctx.nd_config_analytics->getConfig("inCabFeedback",
                                                                                             usralert_map[3],
                                                                                             "0",
                                                                                             get_override_val,
                                                                                             is_val_overridden));
        ctx.user_alert_cfg.enable_status_.at(4) = ("1" == ctx.nd_config_analytics->getConfig("inCabFeedback",
                                                                                             usralert_map[4],
                                                                                             "0",
                                                                                             get_override_val,
                                                                                             is_val_overridden));

        if (ctx.user_alert_cfg.enable_status_.at(0)) {
            ctx.user_alert_cfg.audio_file_.at(0) = ctx.nd_config_analytics->getConfig("audioFile", usralert_map[0],
                                                                                      default_user_initiated_audio_alert,
                                                                                      get_override_val, is_val_overridden);
            LOG_I(TAG, "audio alert is enabled for button 0 with file: %s", ctx.user_alert_cfg.audio_file_.at(0).c_str());
        }

        if (ctx.user_alert_cfg.enable_status_.at(1)) {
            ctx.user_alert_cfg.audio_file_.at(1) = ctx.nd_config_analytics->getConfig("audioFile", usralert_map[1],
                                                                                      default_user_initiated_audio_alert,
                                                                                      get_override_val, is_val_overridden);
            LOG_I(TAG, "audio alert is enabled for button 1 with file: %s", ctx.user_alert_cfg.audio_file_.at(1).c_str());
        }

        if (ctx.user_alert_cfg.enable_status_.at(2)) {
            ctx.user_alert_cfg.audio_file_.at(2) = ctx.nd_config_analytics->getConfig("audioFile", usralert_map[2],
                                                                                      default_user_initiated_audio_alert,
                                                                                      get_override_val, is_val_overridden);
            LOG_I(TAG, "audio alert is enabled for button 2 (BLE) with file: %s", ctx.user_alert_cfg.audio_file_.at(2).c_str());
        }

        if (ctx.user_alert_cfg.enable_status_.at(3)) {
            ctx.user_alert_cfg.audio_file_.at(3) = ctx.nd_config_analytics->getConfig("audioFile", usralert_map[3],
                                                                                      default_user_initiated_audio_alert,
                                                                                      get_override_val, is_val_overridden);
            LOG_I(TAG, "audio alert is enabled for button 3 (BLE) with file: %s", ctx.user_alert_cfg.audio_file_.at(3).c_str());
        }

        if (ctx.user_alert_cfg.enable_status_.at(4)) {
            ctx.user_alert_cfg.audio_file_.at(4) = ctx.nd_config_analytics->getConfig("audioFile", usralert_map[4],
                                                                                      default_user_initiated_audio_alert,
                                                                                      get_override_val, is_val_overridden);
            LOG_I(TAG, "audio alert is enabled for button 4 (BLE) with file: %s", ctx.user_alert_cfg.audio_file_.at(4).c_str());
        }
    } else {
        LOG_I(TAG, "global inCabFeedback alerts is disabled");
    }
}

void set_bit(int& num, int bit_pos) {
    num |= (1 << bit_pos); // Set the specified bit
}

void clear_bit(int& num, int bit_pos) {
    num &= ~(1 << bit_pos); // Clear the specified bit
}

bool is_bit_set(int num, int bit_pos) {
    return (num & (1 << bit_pos)) != 0;
}

bool create_audio_socket() {

    LOG_I(TAG, "Create audio ZMQ socket");
    if ((ctx.zmq_publisher_audio == NULL) && (ctx.zmq_context_audio == NULL))
    {
        ctx.zmq_context_audio = zmq_ctx_new ();
        if( ctx.zmq_context_audio == NULL ){
            LOG_E(TAG, "ctx.zmq_context_audio == NULL; errno: %d, zmq_error:   %s", errno, zmq_strerror(zmq_errno()));
            return false;
        }
        ctx.zmq_publisher_audio = zmq_socket (ctx.zmq_context_audio, ZMQ_PUB);
        if( ctx.zmq_publisher_audio == NULL ){
            LOG_E(TAG, "ctx.zmq_publisher_audio == NULL; errno: %d, zmq_error:   %s", errno, zmq_strerror(zmq_errno()));
            return false;
        }

        int max_q_sz = MAX_PUBLISHER_Q_SZ;
        if( zmq_setsockopt(ctx.zmq_publisher_audio, ZMQ_SNDHWM, &max_q_sz, sizeof(int)) ) {
            LOG_E(TAG, "zmq_setsockopt failed; errno: %d, zmq_error:   %s", errno, zmq_strerror(zmq_errno()));
            return false;
        }

        int trail_count = 0, rc = -1;
        do {
            trail_count++;
            usleep(100*1000); // sleep for 100MS before binding : can this avoid FAILED TO CREATE with err 98 ?
            rc = zmq_connect (ctx.zmq_publisher_audio, nd_device_obj->get_audio_socket());
            if(rc != 0) {
                LOG_E(TAG, "FAILED TO CREATE zmq_socket rc: %d , errno: %d,   zmq_error no:   %s",
                        rc, errno, zmq_strerror(zmq_errno()));
                ctx.enable_rt_inertial = false;
            }
            else {
                LOG_I(TAG, "SUCCESS IN CREATE zmq_socket");
                ctx.enable_rt_inertial = true;
                break;
            }
        } while( trail_count < 5 );

        if( rc != 0 ) {
            LOG_C(TAG, "after %d re trails FAILED TO CREATE zmq_socket rc: %d ", trail_count, rc);

            zmq_close (ctx.zmq_publisher_audio);
            zmq_ctx_destroy (ctx.zmq_context_audio);
            ctx.zmq_publisher_audio = NULL;
            ctx.zmq_context_audio = NULL;

            return false;
        }
    }
    LOG_I(TAG, "Created audio ZMQ socket !!!!");
    return true;
}

static bool send_audio_play(nd_audio::AudioData &audio_data) {

    bool status = false;

    nd_audio::AudioMessage audio_message{};

    audio_message.set_allocated_audio_data(&audio_data);

    do {

        string audio_msg_str;
        if (!audio_message.SerializeToString(&audio_msg_str)) {
            LOG_E(TAG, "Failed to serialize audio message");
            break;
        }

        if (audio_msg_str.empty()) {
            LOG_E(TAG, "Serialized audio message is empty");
            break;
        }

        if (bagheera_service_exiting == false) {
            const int zmq_result = zmq_send(ctx.zmq_publisher_audio, const_cast<char *>(audio_msg_str.c_str()), audio_msg_str.size(), 0);
            if(zmq_result == -1) {
                LOG_E(TAG, "FAILED to send message of alert audio message");
                break;
            }
        } else {
            LOG_I(TAG, "bagheera_service_exiting, Don't send audio data to zmq");
        }

        status = true;

    } while (false);

    audio_message.release_audio_data();

    return status;
}

static bool send_live_streaming_status_audio_play(int cam_num, live_stream_event_t event) {

    bool status = false;

    string session_name = "Live_streaming";
    string alert_type = "LiveSt";
    string event_code = "eventCode";
    string uuid = get_new_uuid();
    uint64_t curr_time = (uint64_t)get_system_time();

    do {

        string file_name = "";
        if ((cam_num == 0) && (event == START)) {
            file_name = ctx.live_stream_outward_start_file;
        } else if ((cam_num == 0) && (event == END)) {
            file_name = ctx.live_stream_outward_end_file;
        } else if ((cam_num == 1) && (event == START)) {
            file_name = ctx.live_stream_inward_start_file;
        } else if ((cam_num == 1) && (event == END)) {
            file_name = ctx.live_stream_inward_end_file;
        } else if(event == DUAL_START) {
            file_name = ctx.dual_stream_start_file;
        } else if(event == DUAL_END) {
            file_name = ctx.dual_stream_end_file;
        } else {
            LOG_E(TAG, "Incorrect event number: %d, FAILED to send audio message for camera %d", event, cam_num);
            break;
        }

        if (!file_name.size()) {
            LOG_E(TAG, "Audio file name string for Live streaming is NULL");
            break;
        }

        if (!file_is_present(file_name)) {
            LOG_E(TAG, "Audio file %s is not present", file_name.c_str());
            break;
        }

        if (uuid.empty()) {
            LOG_E(TAG, "%s uuid is empty, using dummy", __func__);
            uuid = "uuid";
        }

        std::cout << "session_name: " << session_name << " alert_type: " << alert_type << " event_code: " << event_code << " uuid: " << uuid << " file_name: " << file_name << " curr_time: " << curr_time << std::endl;

        LOG_I(TAG, "send audio play for camera %d with uuid: %s", cam_num, uuid.c_str());

        nd_audio::AudioData audio_data{};

        audio_data.set_session_name(session_name);
        audio_data.set_alert_type(alert_type);
        audio_data.set_event_code(event_code);
        audio_data.set_uuid(uuid);
        audio_data.set_frame_gen_time(curr_time);
        audio_data.set_issue_time(curr_time);
        audio_data.set_acceptable_latency(5000);
        audio_data.set_volume(100);
        audio_data.set_play_audio(true);
        audio_data.set_audio_file_name(file_name);

        // Add the audio request
        add_audio_request(file_name, AudioEventType::LiveSt);

        if (!send_audio_play(audio_data)) {
            LOG_E(TAG, "send_audio_play failed");
            break;
        }

        status = true;

    } while (false);

    return status;
}

static bool send_alert_audio_play(int button)
{
    bool status = false;

    string session_name = cur_session_fnames[DEVICE_CAMERA_POSITION_FRONT];
    string alert_type = "UserAl";
    string event_code = usralert_map[button];
    string uuid = get_new_uuid();
    uint64_t curr_time = (uint64_t)get_system_time();
    string file_name = ctx.user_alert_cfg.audio_file_.at(button);

    do {

        if (file_name.empty()) {
            LOG_E(TAG, "%s file name is empty", __func__);
            break;
        }

        if (!file_is_present(file_name)) {
            LOG_E(TAG, "Audio file %s is not present", file_name.c_str());
            break;
        }

        if (session_name.empty()) {
            LOG_E(TAG, "%s current session name is empty, hence assigning dummy for audio play", __func__);
            session_name = "User_Alert";
        }

        if (alert_type.empty()) {
            LOG_E(TAG, "%s alert type is empty", __func__);
            break;
        }

        if (event_code.empty()) {
            LOG_E(TAG, "%s event code is empty", __func__);
            break;
        }

        if (uuid.empty()) {
            LOG_E(TAG, "%s uuid is empty, using dummy", __func__);
            uuid = "uuid";
        }

        std::cout << "session_name: " << session_name << " alert_type: " << alert_type << " event_code: " << event_code << " uuid: " << uuid << " file_name: " << file_name << " curr_time: " << curr_time << std::endl;

        nd_audio::AudioData audio_data{};
        audio_data.set_session_name(session_name);
        audio_data.set_alert_type(alert_type);
        audio_data.set_event_code(event_code);
        audio_data.set_uuid(uuid);
        audio_data.set_frame_gen_time(curr_time);
        audio_data.set_issue_time(curr_time);
        audio_data.set_acceptable_latency(5000);
        audio_data.set_volume(100);
        audio_data.set_play_audio(true);
        audio_data.set_audio_file_name(file_name);

        LOG_I(TAG, "send alert audio play with uuid: %s", uuid.c_str());

        // Add the audio request
        add_audio_request(file_name, AudioEventType::UserAl);

        if (!send_audio_play(audio_data)) {
            LOG_E(TAG, "send_audio_play failed");
            break;
        }

        status = true;

    } while (false);

    return status;
}

bool send_power_mon_alert_audio_play(string file_name, string audio_type) {

    LOG_I(TAG, "%s", __func__);

    if ( !file_name.size() ) {
        LOG_E(TAG, "Audio file name string is NULL");
        return false;
    }

    if ( !file_is_present(file_name) ) {
        LOG_E(TAG, "File %s is not present", file_name.c_str());
        return false;
    }

	nd_audio::AudioData audio_data {};
	string session_name = "Ignition Alert";
	string alert_type = audio_type;
	string event_code = "eventCode";
	string uuid = "uuid";
	uint64_t curr_time = (uint64_t)get_system_time();

    // Initialize the audio data
	audio_data.set_session_name(session_name);
	audio_data.set_alert_type(alert_type);
	audio_data.set_event_code(event_code);
	audio_data.set_uuid(uuid);
	audio_data.set_frame_gen_time(curr_time);
	audio_data.set_issue_time(curr_time);
	audio_data.set_acceptable_latency(5000);
	audio_data.set_volume(100);
	audio_data.set_play_audio(true);
	audio_data.set_audio_file_name(file_name);

    if (!send_audio_play(audio_data)) {
        LOG_E(TAG, "send_audio_play failed for %s", file_name.c_str());
        return false;
    }
    LOG_I(TAG, "Audio play sent for %s successfully", file_name.c_str());
	return true;
}

bool set_default_network_info()
{
    stringstream ss;
    ss.str("");
    ss << " \"recordedTime\": \"" << get_system_time() << "\"" <<
          ", \"regState\": \"NA\"" <<
          ", \"rat\": \"NA\"" <<
          ", \"band\": \"NA\"" <<
          ", \"homeNetwork\": \"NA\"" <<
          ", \"cellId\": \"0\"" <<
          ", \"roaming\": \"NA\"" <<
          ", \"rssi\": \"0\""<<
          ", \"rsrq\": \"0\""<<
          ", \"rsrp\": \"0\""<<
          ", \"sinr\": \"0\"";

    network_info = ss.str();
}

bool set_network_info (conn_mgr_sig_info_msg_t *sig_info)
{
    // validate home network
    string home_network = sig_info->home_network;
    LOG_D(TAG, "home network: %s", home_network.c_str());
    if(find_if_not(home_network.begin(), home_network.end(),
                  [](unsigned char c) {return isalnum(c);}) != home_network.end()) {
        LOG_I(TAG, "detected non-alphanumeric characters in home network info, so setting NA");
        home_network = "NA";
    }

    stringstream ss;
    ss.str("");
    ss<<
        " \"recordedTime\": "<<"\""<<sig_info->time<<"\"";
    ss<<
        ", \"regState\": "<<"\""<<sig_info->reg_state_str<<"\"";
    ss<<
        ", \"rat\": "<<"\""<<sig_info->radio_interface_str<<"\""<<
        ", \"band\": "<<"\""<<sig_info->band_class_str<<"\""<<
        ", \"homeNetwork\": "<<"\""<<home_network<<"\""<<
        ", \"cellId\": "<<"\""<<sig_info->cell_id<<"\""<<
        ", \"roaming\": "<<"\""<<sig_info->roaming_status_str <<"\""<<
        ", \"rssi\": "<<"\""<<(int)sig_info->rssi<<"\""<<
        ", \"rsrq\": "<<"\""<<(int)sig_info->rsrq<<"\""<<
        ", \"rsrp\": "<<"\""<<sig_info->rsrp<<"\""<<
        ", \"sinr\": "<<"\""<<sig_info->sinr<<"\"";

    network_info = ss.str();

    return true;
}

static string get_network_info ()
{
    return network_info;
}

static string UNKNOWN_DRVID = "";
static string driver_id = "";
static string driver_id_v2 = "[]";

static bool set_driver_id (char drvIds[][MAX_DRV_ID_LEN], char loginTime[][MAX_APP_LOGIN_TIME_LEN], int num_ids) {

    int i = 0;
    string driver_id_list("");
    string driver_id_v2_list("[");

    for (i=0; i<num_ids; i++)
    {
        if (i > 0)
        {
            driver_id_v2_list += ",";
        }

        driver_id_list = driver_id_list + (i > 0 ? ", " : "") + "\"" + string(drvIds[i]) + "\"";
        driver_id_v2_list += "{\"driver_id\":\"" + string(drvIds[i]) + "\",\"login_time\":" + string(loginTime[i]) + "}";
    }

    driver_id_v2_list += "]";

    LOG_I (TAG,"Setting driver id as %s",driver_id_list.c_str());
    LOG_I (TAG, "Setting driver id v2 as %s",driver_id_v2_list.c_str());
    driver_id = driver_id_list;
    driver_id_v2 = driver_id_v2_list;

    return true;
}

static bool set_driver_id (string drvId) {

    LOG_I (TAG,"set driver id  %s",drvId.c_str());
    driver_id = drvId;
    driver_id_v2 = "[{\"driver_id\":\"" + (drvId.empty() ? "app_driver: unassigned_driver" : drvId) +
                     "\",\"login_time\":" + "-1" + "}]";
    return true;
}

static string get_driver_id () {
    return driver_id;
}

static string get_driver_id_v2 () {
    return driver_id_v2;
}

static string get_msgq_name() {
        return Q_NAME;
}

static string get_ext_cam_msgq_name() {
    return Q_NAME_EXT_CAM;
}

static string get_cam_rec_msgq_name() {
    return Q_NAME_CAMREC;
}

static bool send_trigger_scheduler_manager()
{
    generic_msg_t trigger_sch_mgr;
    if (send_msg (&trigger_sch_mgr, TRIGGER_SCHEDULER_LEGACY, sizeof (trigger_sch_mgr), get_msgq_name(), Q_NAME_SCH_MGR, msg_idx++))
    {
        LOG_I (TAG,"Trigger scheduler manager sent");
        return true;
    }
    else
    {
        LOG_E (TAG,"send_msg failed");
        return false;
    }
}

static bool send_drv_login_query()
{
    generic_msg_t drv_login_query;
    if (send_msg (&drv_login_query, DRV_LOGIN_QUERY, sizeof (drv_login_query), get_msgq_name(), Q_NAME_BTFV, msg_idx++))
    {
        LOG_I (TAG,"Driver Login Query sent");
        return true;
    }
    else
    {
        LOG_E (TAG,"send_msg failed");
        return false;
    }
}



static bool set_led_functionality()
{
    ctx.privacy_mode_led = true;

#ifdef KRAIT
    if (!file_is_present(installer_scan_active_file)) {
        nd_device_obj->nd_set_led_on(GREEN, POWER_LED, true );
    }
    nd_device_obj->nd_set_led_on(GREEN, PRIVACY_LED, true );
    nd_device_obj->nd_set_led_on(RED, PRIVACY_LED, true );
    nd_device_obj->nd_clear_led( SYS_RIGHT_RED );
    nd_device_obj->nd_clear_led( SYS_RIGHT_GREEN );
#endif

    recover_installer_scan_led_blinking_on_startup();

    Config_parser c(BAGHEERACONFIG_INI);

    bool get_override_val = true;
    bool is_val_overridden = false;
    if (c.getParseStatus())
    {
        if (c.getConfig ("privacy_mode", "privacymode_led", "false", get_override_val, is_val_overridden) == "false")
        {
            LOG_I (TAG,"LED will indicate GPS validity");
            ctx.privacy_mode_led = false;
            return true;
        }
    }

    LOG_I (TAG,"LED will indicate privacy mode");
    return true;
}

static bool set_privacy_mode_led_task(void *args)
{
    if(args == NULL) {
        return true;
    }
    bool privacy_status = *(bool *)args;
    if (ctx.privacy_mode_led == false) {
        return true;
    }
    else {
        if (!ctx.ublox_led_indication) {
            /* Set both LED to RED for personal privacy/off duty mode/geofence mode */
            if (ctx.off_duty_privacy || ctx.geofence_privacy) {
                nd_device_obj->nd_set_led_on(GREEN, PRIVACY_LED, false);
                nd_device_obj->nd_set_led_on(BLUE, PRIVACY_LED, false);
                nd_device_obj->nd_set_led_on(RED, PRIVACY_LED, true);

                if (!inst_scan_led_blinking) {
                    nd_device_obj->nd_set_led_on(GREEN, POWER_LED, false);
                    nd_device_obj->nd_set_led_on(BLUE, POWER_LED, false);
                    nd_device_obj->nd_set_led_on(RED, POWER_LED, true);
                }
#ifdef DMS_CAMERA_SUPPORTED
                set_dms_led_msg_t dms_led_msg;
                dms_led_msg.led_colour = dmsLedRed;
                send_msg((generic_msg_t *)&dms_led_msg, SET_DMS_LED_MSG,
                    sizeof( dms_led_msg ), get_msgq_name(), get_cam_rec_msgq_name(), 0);
#endif
               return true;
            }
            /* Set power LED to green when not in Off duty or installer pairing mode */
            if (!inst_scan_led_blinking) {
                nd_device_obj->nd_set_led_on(RED, POWER_LED, false);
                nd_device_obj->nd_set_led_on(BLUE, POWER_LED, false);
                nd_device_obj->nd_set_led_on(GREEN, POWER_LED, true);
            }

            if (!cams_enabled[DEVICE_CAMERA_POSITION_BACK]) {
                nd_device_obj->nd_set_led_on(GREEN, PRIVACY_LED, false);
                nd_device_obj->nd_set_led_on(BLUE, PRIVACY_LED, false);
                nd_device_obj->nd_set_led_on(RED, PRIVACY_LED, true);
                return true;
            }
            /* Set inward LED color to custom configuration in case of custom and enhanced privacy */
            if (ctx.privacy_params.enhanced_privacy || privacy_status) {
                if (ctx.privacy_params.inward_led_color == "purple") {
                    nd_device_obj->nd_set_led_on(GREEN, PRIVACY_LED, false);
                    nd_device_obj->nd_set_led_on(BLUE, PRIVACY_LED, true);
                    nd_device_obj->nd_set_led_on(RED, PRIVACY_LED, true);
#ifdef DMS_CAMERA_SUPPORTED
                    set_dms_led_msg_t dms_led_msg;
                    dms_led_msg.led_colour = dmsLedRed;
                    send_msg((generic_msg_t *)&dms_led_msg, SET_DMS_LED_MSG,
                        sizeof( dms_led_msg ), get_msgq_name(), get_cam_rec_msgq_name(), 0);
#endif
                } else if (ctx.privacy_params.inward_led_color == "red") {
                    nd_device_obj->nd_set_led_on(GREEN, PRIVACY_LED, false);
                    nd_device_obj->nd_set_led_on(BLUE, PRIVACY_LED, false);
                    nd_device_obj->nd_set_led_on(RED, PRIVACY_LED, true);
#ifdef DMS_CAMERA_SUPPORTED
                    set_dms_led_msg_t dms_led_msg;
                    dms_led_msg.led_colour = dmsLedRed;
                    send_msg((generic_msg_t *)&dms_led_msg, SET_DMS_LED_MSG,
                        sizeof( dms_led_msg ), get_msgq_name(), get_cam_rec_msgq_name(), 0);
#endif
                } else if (ctx.privacy_params.inward_led_color == "green") {
                    nd_device_obj->nd_set_led_on(BLUE, PRIVACY_LED, false);
                    nd_device_obj->nd_set_led_on(RED, PRIVACY_LED, false);
                    nd_device_obj->nd_set_led_on(GREEN, PRIVACY_LED, true);
#ifdef DMS_CAMERA_SUPPORTED
                    set_dms_led_msg_t dms_led_msg;
                    dms_led_msg.led_colour = dmsLedGreen;
                    send_msg((generic_msg_t *)&dms_led_msg, SET_DMS_LED_MSG,
                        sizeof( dms_led_msg ), get_msgq_name(), get_cam_rec_msgq_name(), 0);
#endif
                } else {
                    LOG_E (TAG, "inward_led_color parameter coming wrong: %s. making default LED as purple",
                                  ctx.privacy_params.inward_led_color.c_str());
                    nd_device_obj->nd_set_led_on(GREEN, PRIVACY_LED, false);
                    nd_device_obj->nd_set_led_on(BLUE, PRIVACY_LED, true);
                    nd_device_obj->nd_set_led_on(RED, PRIVACY_LED, true);
#ifdef DMS_CAMERA_SUPPORTED
                    set_dms_led_msg_t dms_led_msg;
                    dms_led_msg.led_colour = dmsLedRed;
                    send_msg((generic_msg_t *)&dms_led_msg, SET_DMS_LED_MSG,
                        sizeof( dms_led_msg ), get_msgq_name(), get_cam_rec_msgq_name(), 0);
#endif
                }
                return true;
            }
            if (privacy_status == false) {
                nd_device_obj->nd_set_led_on(RED, PRIVACY_LED, false);
                nd_device_obj->nd_set_led_on(BLUE, PRIVACY_LED, false);
                nd_device_obj->nd_set_led_on(GREEN, PRIVACY_LED, true);
#ifdef DMS_CAMERA_SUPPORTED
                set_dms_led_msg_t dms_led_msg;
                dms_led_msg.led_colour = dmsLedGreen;
                send_msg((generic_msg_t *)&dms_led_msg, SET_DMS_LED_MSG,
                    sizeof( dms_led_msg ), get_msgq_name(), get_cam_rec_msgq_name(), 0);
#endif
                return true;
            }
        }
    }
    return true;
}

static bool set_privacy_mode_led(bool privacy_status)
{
    task_result_t task_result = nd_timed_task(set_privacy_mode_led_task,
                        SET_LED_TIME_MAX, (void *)&privacy_status, "set_led");
    if (task_result != TASK_SUCCESS) {
        LOG_E (TAG, "nd_timed_task for set_privacy_mode_led timedout");
        nd_service_obj->send_err_msg(SM_E_NDC_SET_LED_FAIL, 0, "set_privacy_mode_led timedout");
    }
    LOG_I(TAG, "set_privacy_mode_led return status %d for privacy_status %d", task_result, privacy_status);
    return true;
}

// returns true if crank is low; suppose to discard videos from next session after true
bool check_ignition_privacy(power_crank_levels_t crank_level) {
    return (crank_level != CRANK_HIGH);
}

//Call this function with value , only if you have a preference.
//Ignore filling this parameter when in doubt

// value = true, caller asking to set IRLED ON
// value = false, caller asking to set IRLED OFF

// Do not add any return statements in the middle of the body
// except for invalid reason
static bool check_set_irled(irled_reason_t reason, bool value=false)
{
    if (cams_enabled[DEVICE_CAMERA_POSITION_BACK] == false) {
        LOG_I(TAG, "Inward camera is disabled, hence not taking any action on IRLED");
        return true;
    }

    if (reason == REASON_INVALID) {
        LOG_E(TAG, "IRLED reason is invalid. Ignoring request");
        return false;
    }

    LOG_I(TAG, "%s: called with reason = %d, value = %d", __func__, reason, value);

    bool final_val = value;
    if (reason != REASON_QR_SCAN) {
        if ((ctx.is_driverlogin_qr_enabled == true) && (qr_scan_started == true)) {
            LOG_I(TAG, "%s: QR scan is in progress, ignoring IRLED change request for reason %d", __func__, reason);
            // Save the latest request for processing after QR scan is complete
            saved_irled_data.reason = reason;
            saved_irled_data.value = value;

            return true;
        } else {
            // photodiode_override has the latest photodiode status
            // = true, if photodiode says NIGHT_MODE
            // = false, if photodiode says DAY_MODE
            // This variable does not track the latest status of irled
            // if photodiode says NIGHT MODE, irled can still be OFF because of privacy
            static bool photodiode_override = false;
            static bool irled_on_hrs = false;

            if (reason == REASON_AMBIENT_LIGHT) {
                LOG_I(TAG, "%s: Photodiode says value = %d", __func__, value);
                photodiode_override = value;
            } else if (reason == REASON_TIME) {
                irled_on_hrs = value;
            }

            /* If it is IRLED_ON_HRS, IRLED will be switched on irrespective of dark/light condition,
             * but if it is IRLED_OFF_HRS, depending on the light condition IRLED will be switched ON
             * or switched OFF */
            if (irled_on_hrs) {
                final_val = true;
            } else if (photodiode_override == true) {
                LOG_I(TAG, "%s: Photodiode override is true", __func__);
                final_val = true;
            }

            // If Vehicle is in regular privacy or off-duty or geofence and save_user_alert_video is disabled, IRLED will be switched off
            if ((ctx.geofence_privacy) || (((ctx.fused_privacy && ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_BACK]) || ctx.off_duty_privacy) &&
                    !ctx.privacy_params.save_user_alert_video)) {
                LOG_I(TAG, "%s: Turning off IRLED because of privacy being true", __func__);
                final_val = false;
            }
        }
    }

    // Only update IRLED if the state actually changes
    static int last_irled_state = -1; // -1 means not set yet

    pthread_mutex_lock(&irled_mutex);
    if (final_val != last_irled_state) {
        //set IRLED based on the final_val
        if (final_val == true) {
            //Removed level 4 hardcoded value for Krait, added in bagheera_config.ini
            ctx.photodiode->set_irled_brightness(ctx.irled_level);
            ctx.ir_led_status = true;
        } else { // final_val == false
            ctx.photodiode->set_irled_clear();
            ctx.ir_led_status = false;
        }
        last_irled_state = final_val;
        LOG_I(TAG, "IRLED is set with %d for reason %d", final_val, reason);

        set_irled_mode_for_fname(currvid_fname, final_val);
    } else {
        LOG_I(TAG, "IRLED state unchanged (%d), skipping update for reason %d", final_val, reason);
    }
    pthread_mutex_unlock(&irled_mutex);

    return true;
}

static bool extract_hr_minute (double time_in_min, int &hr, int &min)
{
    stringstream ss;
    string dec, fract;
    double fraction = 0.0;

    double min_to_hr = time_in_min / 60;

    ss.str("");
    ss.clear();

    ss << min_to_hr;

    getline (ss, dec, '.');
    getline (ss, fract, '.');

    ss.clear ();
    ss.str("");

    ss << dec;
    ss >> hr;

    ss.clear();
    ss.str ("");

    fract = "." + fract;
    ss << fract;
    ss >> fraction;
    min = (int )(fraction * 60);

    ss.clear ();

}

static bool get_local_hr_minute (double utc_time_in_min, double tz_offset_in_min, int &hr, int &minute, bool add)
{
    int utc_hr, utc_min;
    int tz_offset_hr, tz_offset_min;
    int final_hr, final_minute;

    string dec, fract;

    extract_hr_minute (utc_time_in_min, utc_hr, utc_min);

    LOG_D (TAG,"UTC: %d:%d",utc_hr,utc_min);

    extract_hr_minute (tz_offset_in_min, tz_offset_hr, tz_offset_min);

    LOG_D (TAG,"TZ offset: %d:%d",tz_offset_hr, tz_offset_min);

    if (add)
    {
        final_hr = (utc_hr + tz_offset_hr) % 24;
        final_minute = utc_min + tz_offset_min;

        if (final_minute >= 60)
        {
            final_hr = (final_hr + 1) % 24;
            final_minute = final_minute % 60;
        }
    }
    else
    {
        final_hr = (utc_hr - tz_offset_hr + 24) % 24;
        final_minute = utc_min - tz_offset_min;

        if (final_minute < 0)
        {
            final_minute = 60 + final_minute;
            final_hr = (final_hr - 1 + 24 ) % 24;
        }
    }

    hr = final_hr;
    minute = final_minute;
    return true;
}

static bool turn_irled_on_or_off (int64_t time, double longitude)
{
    bool turn_on_ir_time = false, turn_off_ir_time = true;

    if (ctx.saved_gps.valid) {

        bool addition = true;
        if (longitude < 0) {
            longitude = -1 * longitude;
            addition = false;
        }

        time_t t = time;
        struct tm ts = *gmtime (&t);

        double tz_offset_in_min = longitude * 4;
        LOG_D (TAG,"tz_offset_in_min:%lf", tz_offset_in_min);
        double utc_time_in_min = ts.tm_hour * 60 + ts.tm_min;

        int local_hr, local_minute;
        LOG_I(TAG, "inside turn_irled_on_or_off");

        get_local_hr_minute(utc_time_in_min, tz_offset_in_min, local_hr, local_minute, addition);
        LOG_I(TAG, "Local Time: %d:%d",local_hr, local_minute);
        LOG_I(TAG, "local_hr %d IRLED_ON_HR %d IRLED_OFF_HR %d", local_hr, IRLED_ON_HR, IRLED_OFF_HR);
        // IR LED should be on from IRLED_ON_HR till IRLED_OFF_HR
        // Assumption: IRLED_OFF_HR will be between 00:00 am and IRLED_ON_HR
        turn_on_ir_time =  (local_hr >= IRLED_ON_HR) || (local_hr <  IRLED_OFF_HR);
        turn_off_ir_time = (local_hr <  IRLED_ON_HR) && (local_hr >= IRLED_OFF_HR);
    } else {
        LOG_I(TAG, "GPS is not available. Hence, not taking action on IRLED");
        return true;
    }

    LOG_I(TAG, "turn_on_ir_time %d turn_off_ir_time %d ctx.inward_privacy_state %d",
                turn_on_ir_time, turn_off_ir_time, ctx.inward_privacy_state);

    // call decision making module here
    if (turn_on_ir_time == true) {
        check_set_irled(REASON_TIME, true);
    } else if(turn_off_ir_time == true) {
        check_set_irled(REASON_TIME, false);
    }

    return true;
}

void blink_led_once (led_num_t led_num, led_color_t color, int ms_interval)
{

    if (led_num == PRIVACY_LED || led_num == POWER_LED)
    {
        nd_device_obj->nd_set_led_on (color, led_num, true);
        usleep (ms_interval * 1000);
        nd_device_obj->nd_set_led_on (color, led_num, false);
        usleep (ms_interval * 1000);
    }
    else //both
    {
        nd_device_obj->nd_set_led_on (color, PRIVACY_LED, true);
        nd_device_obj->nd_set_led_on (color, POWER_LED, true);
        usleep (ms_interval * 1000);
        nd_device_obj->nd_set_led_on (color, PRIVACY_LED, false);
        nd_device_obj->nd_set_led_on (color, POWER_LED, false);
        usleep (ms_interval * 1000);
    }
}

void *ublox_led_flash_thread (void *)
{
    if (ctx.ublox_led_indication == false) {
        LOG_E (TAG, "Ublox LED indication is not enabled. exiting thread");
        pthread_exit (NULL);
    }
    while (1)
    {
        if (ctx.flash_ublox_led)
        {
            nd_device_obj->nd_set_led_on (RED, PRIVACY_LED, false);
            blink_led_once (PRIVACY_LED, GREEN, 200);
            ctx.flash_ublox_led = false;
        }
        usleep (1000);
    }
}

//callback section

static bool ublox_callback (Ublox::ublox_data_t &data) {

    if (ctx.ublox_led_indication) {
        ctx.flash_ublox_led = true;
    }
    //both pps_timestamp and wall clock time comes in nano seconds.
    LOG_I(TAG, "%s: time = %lld", __func__, data.system_time);
    data.system_time =  data.system_time/ONE_MICRO_IN_NANO;
    ctx.meta_buff[ctx.session_flipflop].push(data);
}

static bool ublox_gps_callback ( Ublox::ublox_gps_data_t val, uint64_t ublox_gps_index, uint64_t raw_time_ns, double altitudeMSL) {
    if( ctx.pipeline_enabled == false ) {
        return false;
    }

    std::shared_ptr<ndmbmsg_apm_gps_data_t>  all_gps_data(new ndmbmsg_apm_gps_data_t);
    all_gps_data->session_no = 1;
    all_gps_data->gps_data_ptr.valid = val.valid;
    all_gps_data->gps_data_ptr.speed = val.speed;
    all_gps_data->gps_data_ptr.accuracy = val.accuracy;
    all_gps_data->gps_data_ptr.latitude = val.latitude;
    all_gps_data->gps_data_ptr.longitude = val.longitude;
    all_gps_data->gps_data_ptr.timestamp = val.timestamp;
    all_gps_data->gps_data_ptr.system_timestamp = get_system_monotonic_time();

    server.publish(TOPIC_APM_GPS_DATA, all_gps_data);
    //ublox_failure = false;
    LOG_D(TAG, "Time: %lld Lat: %lf Long: %lf AltMSL: %lf AltEllipsoid: Bearing: %f Accuracy: %f Valid: %d",
            val.timestamp, val.latitude, val.longitude, altitudeMSL, val.bearing, val.accuracy, val.valid);

    if( val.valid == true ) {
        ctx.saved_ublox_gps = val;
#ifdef GPS_LED_STATUS
        if (ctx.privacy_mode_led == false)
        {
        nd_device_obj->nd_set_led_on(RED, PRIVACY_LED, false);
        nd_device_obj->nd_set_led_on(GREEN, PRIVACY_LED, true);
        }
#endif
    } else {
        //If not valid, push saved GPS value
        val = ctx.saved_ublox_gps;
        val.valid = false;
#ifdef GPS_LED_STATUS
        if (ctx.privacy_mode_led == false)
        {
            if (!ctx.ublox_led_indication) {
                nd_device_obj->nd_set_led_on(GREEN, PRIVACY_LED, false);
                nd_device_obj->nd_set_led_on(RED, PRIVACY_LED, true);
            }
        }
#endif
    }

    ublox_gps_switch = ( prev_ublox_gps_valid != val.valid );
    prev_ublox_gps_valid = val.valid;

    pthread_mutex_lock(&dis_mutex);
    if(ublox_gps_switch) {
        if(val.valid) {
            ctx.dis_status = 1;
            ctx.dis_update_ts = get_system_time();
        }
        else {
            ctx.dis_status = 0;
        }
    }
    pthread_mutex_unlock(&dis_mutex);

    Gps::gps_data_t val_gps;
    memset(&val_gps, 0, sizeof(val_gps));
    val_gps.valid = val.valid;
    val_gps.latitude = val.latitude;
    val_gps.longitude = val.longitude;
    val_gps.altitude = val.altitude;
    val_gps.speed = val.speed;
    val_gps.bearing = val.bearing;
    val_gps.accuracy = val.accuracy;
    val_gps.timestamp = val.timestamp;
    val_gps.system_timestamp = val.system_timestamp;


	Gps::gps_metadata_t gps_sensor_data;
	gps_sensor_data.gps_data = val_gps;
	gps_sensor_data.gps_index = ublox_gps_index;
	gps_sensor_data.altitudeMSL = altitudeMSL;
	gps_sensor_data.raw_time_micro = raw_time_ns / ONE_MICRO_IN_NANO;

	ctx.meta_buff[ctx.session_flipflop].push(gps_sensor_data);

  //  send_gps_updates(val_gps);
    //TODO:Code to be removed after migration to new GPS
    ctx.saved_gps = val_gps;

    return true;
}

static bool ublox_pps_callback (Ublox::ublox_pps_data_t &val) {

    Gps::gps_pps_data_t val_pps;
    val_pps.pps_index = val.pps_index;
    val_pps.pps_raw_time = val.pps_raw_time;
    val_pps.pps_clock_time = val.pps_clock_time;

    ctx.meta_buff[ctx.session_flipflop].push(val_pps);
}

static bool gps_pps_callback (Gps::gps_pps_data_t &val) {

    ctx.meta_buff[ctx.session_flipflop].push(val);
}

static bool ublox_constellation_callback (Ublox::ublox_constellation_data_t &ublox_constellation_data)
{
    // push to metadata
    ctx.meta_buff[ctx.session_flipflop].push(ublox_constellation_data);
}

void set_imu_acc_data(string data)
{
//    lock_guard<std::mutex> lock(mutex_imu_data);
    imu_acc_data = data;
}

void get_imu_acc_data(string& data)
{
//    lock_guard<std::mutex> lock(mutex_imu_data);
    data = imu_acc_data;
}

static bool gps_callback (Gps::gps_sensor_data_t gps_sensor_data) {
    gps_outage = false;
    static bool first_gps = true;
    if(first_gps) {
        first_gps = false;
        LOG_I(TAG, "First GPS data received with lat: %lf, long: %lf",
                gps_sensor_data.gps_data.latitude, gps_sensor_data.gps_data.longitude);
        def_gps.latitude = gps_sensor_data.gps_data.latitude;
        def_gps.longitude = gps_sensor_data.gps_data.longitude;
        pthread_mutex_lock(&rt_gps_mutex);
        ctx.saved_gps = def_gps;
        pthread_mutex_unlock(&rt_gps_mutex);
        
    }
    if (ctx.pipeline_enabled == false) {
        return false;
    }

    // Maintain a queue of speed samples for button-based privacy in sequential manner
    if (ctx.privacy_params.privacy_activate_params.button_based_privacy) {
        static size_t max_samples = ctx.privacy_params.privacy_activate_params.long_press_duration_ms / 1000;
        // Use a queue to store speed samples in sequential order
        if (gps_sensor_data.gps_data.valid && (ctx.ignition_status == IGNITION_STATUS_ON)) {
            ctx.speed_samples.emplace(gps_sensor_data.gps_data.speed, gps_sensor_data.gps_data.timestamp, gps_sensor_data.gps_data.accuracy);
        } else {
            ctx.speed_samples.emplace(0.0, gps_sensor_data.gps_data.timestamp, gps_sensor_data.gps_data.accuracy);
        }
        // Keep only the last max_samples elements
        while (ctx.speed_samples.size() > max_samples) {
            ctx.speed_samples.pop();
        }
    }
//#ifdef KRAIT
    std::shared_ptr<ndmbmsg_apm_gps_data_t> all_gps_data(new ndmbmsg_apm_gps_data_t);
    all_gps_data->session_no = 1;
    all_gps_data->gps_data_ptr.valid = gps_sensor_data.gps_data.valid;
    all_gps_data->gps_data_ptr.speed = gps_sensor_data.gps_data.speed;
    all_gps_data->gps_data_ptr.accuracy = gps_sensor_data.gps_data.accuracy;
    all_gps_data->gps_data_ptr.latitude = gps_sensor_data.gps_data.latitude;
    all_gps_data->gps_data_ptr.longitude = gps_sensor_data.gps_data.longitude;
    all_gps_data->gps_data_ptr.timestamp = gps_sensor_data.gps_data.timestamp;
    all_gps_data->gps_data_ptr.system_timestamp = get_system_monotonic_time();

    if (bagheera_service_exiting == true) {
        LOG_I(TAG, "bagheera_service_exiting, not publishing GPS data");
        return true;  // Not a fail so returning true;
    }
    server.publish(TOPIC_APM_GPS_DATA, all_gps_data);
//#endif
    LOG_D(TAG, "Accuracy of GPS data: %f", gps_sensor_data.gps_data.accuracy);
    if(gps_sensor_data.gps_data.accuracy == 1000) {
       LOG_I(TAG, "GPS accuracy is 1000, setting gps data to the last known location");
        pthread_mutex_lock(&rt_gps_mutex);
        ctx.saved_gps = gps_sensor_data.gps_data;
        pthread_mutex_unlock(&rt_gps_mutex);
    }
    if (gps_sensor_data.gps_data.valid == true) {
        pthread_mutex_lock(&rt_gps_mutex);
        ctx.saved_gps = gps_sensor_data.gps_data;
        pthread_mutex_unlock(&rt_gps_mutex);
#ifdef GPS_LED_STATUS
        if (ctx.privacy_mode_led == false) {
            nd_device_obj->nd_set_led_on(RED, PRIVACY_LED, false);
            nd_device_obj->nd_set_led_on(GREEN, PRIVACY_LED, true);
        }
#endif
    } else {
        //If not valid, push saved GPS value
        gps_sensor_data.gps_data = ctx.saved_gps;
        gps_sensor_data.gps_data.valid = false;
#ifdef GPS_LED_STATUS
        if (ctx.privacy_mode_led == false) {
            if (!ctx.ublox_led_indication) {
                nd_device_obj->nd_set_led_on(GREEN, PRIVACY_LED, false);
                nd_device_obj->nd_set_led_on(RED, PRIVACY_LED, true);
            }
        }
#endif
    }

    if (ctx.enable_rt_gps == true) {
        bool msg_send_status = send_msg_analytics_gps(gps_sensor_data.gps_data);
        LOG_D(TAG, "msg_send_status: %d", msg_send_status);
    }

    if(ctx.enable_rt_gps_geo_fence == true) {
	bool geo_fence_msg_send_status = send_msg_analytics_gps_geo_fence(gps_sensor_data.gps_data);
	LOG_D(TAG, "geo_fence_msg_send_status: %d", geo_fence_msg_send_status);
    }

    gps_failure = false;
    LOG_D(TAG, "Time: %lld Lat: %lf Long: %lf Alt: %lf Bearing: %f Accuracy: %f Valid: %d",
            gps_sensor_data.gps_data.timestamp, gps_sensor_data.gps_data.latitude, gps_sensor_data.gps_data.longitude, gps_sensor_data.gps_data.altitude, gps_sensor_data.gps_data.bearing, gps_sensor_data.gps_data.accuracy, gps_sensor_data.gps_data.valid);

    gps_switch = (prev_gps_valid != gps_sensor_data.gps_data.valid);
    prev_gps_valid = gps_sensor_data.gps_data.valid;

    pthread_mutex_lock(&dis_mutex);
    if (gps_switch) {
        if (gps_sensor_data.gps_data.valid) {
            ctx.dis_status = 1;
            ctx.dis_update_ts = get_system_time();
        } else {
            ctx.dis_status = 0;
        }
    }
    pthread_mutex_unlock(&dis_mutex);

    while (1) {
        if (ctx.off_duty_privacy == true || ctx.geofence_privacy == true) {
            LOG_D(TAG, "In OFF Duty/Geofence privacy mode, not saving GPS updates");
            break;
        }

        if ((ctx.fused_privacy == true) && (ctx.privacy_params.gps_privacy == true)) {
            LOG_D(TAG, "In GPS privacy mode, not saving GPS updates");
            break;
        }
        Gps::gps_metadata_t gps_observation;
        gps_observation.gps_data = gps_sensor_data.gps_data;
        gps_observation.altitudeMSL = gps_sensor_data.altitudeMSL;
        gps_observation.gps_index = gps_sensor_data.gps_index;
        gps_observation.raw_time_micro = gps_sensor_data.raw_time_micro;
        ctx.meta_buff[ctx.session_flipflop].push(gps_observation);
        //ctx.meta_buff[ctx.session_flipflop].push(gps_sensor_data);
        break;
    }

    return true;
}

static bool imu_temperature_callback ( Imu::val_t val_temp) {

    val_temp.raw_time = val_temp.raw_time/ONE_MICRO_IN_NANO;
    val_temp.clock_time = val_temp.clock_time/ONE_MICRO_IN_NANO;
    ctx.meta_buff[ctx.session_flipflop].push(Imu::IMU_TEMP,val_temp, ctx.hdmaps_mode_enabled);
    return true;
}

static bool imu_callback ( Imu::val_t val_a, Imu::val_t val_g, Imu::val_t val_m )
{
    imu_outage = false;
    if (ctx.pipeline_enabled == false)
        return false;

//#ifdef KRAIT
    std::shared_ptr<ndmbmsg_apm_imu_data_t>  all_imu_data(new ndmbmsg_apm_imu_data_t);
    all_imu_data->session_no = 1;
    all_imu_data->imu_data_ptr.accel_x = val_a.x;
    all_imu_data->imu_data_ptr.accel_y = val_a.y;
    all_imu_data->imu_data_ptr.accel_z = val_a.z;
    all_imu_data->imu_data_ptr.accel_time = val_a.clock_time / ONE_MICRO_IN_NANO;

    all_imu_data->imu_data_ptr.gyro_x = val_g.x;
    all_imu_data->imu_data_ptr.gyro_y = val_g.y;
    all_imu_data->imu_data_ptr.gyro_z = val_g.z;
    all_imu_data->imu_data_ptr.gyro_time = val_g.clock_time / ONE_MICRO_IN_NANO;
    LOG_D(TAG, "all_imu_data->imu_data_ptr.gyro_x: %f, Line: %d", all_imu_data->imu_data_ptr.gyro_x, __LINE__);

    if (bagheera_service_exiting == true) {
        LOG_I(TAG, "bagheera_service_exiting, not publishing IMU data");
        return true;  // Not a fail so returning true;
    }
    server.publish(TOPIC_APM_IMU_DATA, all_imu_data);
//#endif

    //IMU time comes from kernel as nano seconds.
    val_a.raw_time = val_a.raw_time / ONE_MICRO_IN_NANO;
    val_g.raw_time = val_g.raw_time / ONE_MICRO_IN_NANO;
    val_m.raw_time = val_m.raw_time / ONE_MICRO_IN_NANO;

    val_a.clock_time = val_a.clock_time / ONE_MICRO_IN_NANO;
    val_g.clock_time = val_g.clock_time / ONE_MICRO_IN_NANO;
    val_m.clock_time = val_m.clock_time / ONE_MICRO_IN_NANO;

    //disable IMU_MAGNETO
    ctx.meta_buff[ctx.session_flipflop].push(Imu::IMU_ACCEL, val_a, ctx.hdmaps_mode_enabled);
    ctx.meta_buff[ctx.session_flipflop].push(Imu::IMU_GYRO, val_g, ctx.hdmaps_mode_enabled);
    //ctx.meta_buff[ctx.session_flipflop].push(IMU_MAGNETO, val_m, ctx.hdmaps_mode_enabled);

    LOG_D(TAG, "a: %f %f %f, %lld", val_a.x, val_a.y, val_a.z, val_a.raw_time);
    LOG_D(TAG, "g: %f %f %f, %lld", val_g.x, val_g.y, val_g.z, val_g.raw_time);
    //LOG_D(TAG, "m: %f %f %f, %lld", val_m.x, val_m.y, val_m.z, val_m.raw_time);

  //  string imu_acc = "x: " + round_off_to_string(val_a.x,1) + ",y: " + round_off_to_string(val_a.y,1) + ",z: " + round_off_to_string(val_a.z,1);
  //  set_imu_acc_data(imu_acc);
    if (ctx.enable_rt_inertial) {
        bool msg_send_status = send_msg_analytics_imu(val_a, val_g, val_m);
        if (!msg_send_status)
            LOG_E(TAG, "Failed to send message to analytics");
    }
    return true;
}

#ifdef AUTOMATION

bool DriveSimulation::dummy_imu_callback(Imu::val_t val_a, Imu::val_t val_g, Imu::val_t val_m)
{
    if (DriveSimulation::isImuQueueEmpty == true)
    {
        return true;
    }
    try
    {
        if (!DriveSimulation::imu_data_queue.empty())
        {
            auto data = DriveSimulation::imu_data_queue.front();
            DriveSimulation::imu_data_queue.pop_front();

            Imu::val_t val_a = std::get<0>(data);
            Imu::val_t val_g = std::get<1>(data);
            Imu::val_t val_m = std::get<2>(data);

            int64_t clock_time = get_system_time_ns();
            int64_t raw_time = get_system_monotonic_time_ns();
            val_a.clock_time = clock_time;
            val_a.raw_time = raw_time;
            val_g.clock_time = clock_time;
            val_g.raw_time = raw_time;
            // Call the imu_callback function with the parsed data
            imu_callback(val_a, val_g, val_m);
        }
        else
        {
            LOG_I(TAG, "imu_data_queue is empty");
            DriveSimulation::isImuQueueEmpty = true;
        }
    }
    catch (const std::exception &e)
    {
        LOG_E(TAG, "Exception in dummy_imu_callback: %s", e.what());
    }
    return true;
}
#endif

static bool audio_callback (uint64_t cb_pts, int data_size, char* data ) {
    if( ctx.pipeline_enabled == false ) {
        return false;
    }
    if(data == NULL) {
        LOG_E(TAG,"%s: data is NULL", __func__);
        return false;
    }
    if(data_size > AUDIO_PACKET_SIZE_MAX) {
        LOG_E(TAG,"%s: data size = %d", __func__, data_size);
        return false;
    }

    LOG_D(TAG,"inside nd_central audio_cb");
    Audio::audio_t audio_sample;
    ctx.audio->set_data(&audio_sample, cb_pts, data_size, data);

    ctx.meta_buff[ctx.session_flipflop].push(audio_sample);
    return true;
}

static bool audio_err_callback ( ) {
    if( ctx.pipeline_enabled == false ) {
        return false;
    }

    LOG_E(TAG, "Mic is not working as expected. Reporting to service mon");
    nd_service_obj->send_err_msg(SM_E_NDC_AUDIO_CRASH, 0, "Mic is crashed");

    if((audio_enable == true) && (audio_running == true)) {
        LOG_I(TAG, "stop_audio");
        ctx.audio->stop_audio();
                audio_running = false;
                sleep(AUDIO_PAUSE_TIME);
                if(ctx.audio->start_audio()) {
                    audio_running = true;
                }
    }

    return true;
}

static bool photodiode_callback ( int pt_status)
{
    ndc_photodiode_cb_msg_t photodiode_cb_msg;
    photodiode_cb_msg.type = PHOTODIODE_CALL_BACK;
    photodiode_cb_msg.pt_status = pt_status;
    photodiode_cb_msg.len = sizeof (ndc_photodiode_cb_msg_t);

    nd_msgq_t::nd_msg_t msg((char *)&photodiode_cb_msg, sizeof(photodiode_cb_msg), false);
    if (ctx.msg_q)
    {
        ctx.msg_q->send(msg, nd_msgq_t::ND_MSG_MED);
    }
    else
    {
        LOG_I (TAG,"MSGQ not yet created");
    }
    return true;
}
//End of callback section


//Init Section

static bool get_cams_enabled()
{
    // Marks which all cameras need to be enabled
    // after reading config file.
    // First check over ride file

    // always enable outward camera irrespective of cloud request.
    cams_enabled[DEVICE_CAMERA_POSITION_FRONT] = true;

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

    if (c->isPresent("camera", "back")) {
        if (c->getConfig("camera", "back", "") == "enable")
            cams_enabled[DEVICE_CAMERA_POSITION_BACK] = true;
        else {
            cams_enabled[DEVICE_CAMERA_POSITION_BACK] = false;
            inward_ld = false; // disable ld recording also, if inward camera is disabled
        }
    } else {
        LOG_E(TAG, "No configuration for back camera");
        cams_enabled[DEVICE_CAMERA_POSITION_BACK] = false;
    }

    if (c->isPresent("camera", "left")) {
        if (c->getConfig("camera", "left", "") == "enable")
            cams_enabled[DEVICE_CAMERA_POSITION_LEFT] = true;
        else
            cams_enabled[DEVICE_CAMERA_POSITION_LEFT] = false;
    } else {
        LOG_E(TAG, "No configuration for left camera");
        cams_enabled[DEVICE_CAMERA_POSITION_LEFT] = false;
    }

    if (c->isPresent("camera", "right")) {
        if (c->getConfig("camera", "right", "") == "enable")
            cams_enabled[DEVICE_CAMERA_POSITION_RIGHT] = true;
        else
            cams_enabled[DEVICE_CAMERA_POSITION_RIGHT] = false;
    } else {
        LOG_E(TAG, "No configuration for right camera");
        cams_enabled[DEVICE_CAMERA_POSITION_RIGHT] = false;
    }

    Config_parser temp (BAGHEERACONFIG_INI);
    Config_parser temp_nd (ND_CONFIG_ANALYTICS);
    bool get_override_val = true;
    bool is_val_overridden = false;

    if (isDMSsupported(nd_device_obj->getDeviceType())) {
        /* if dms_drowsy is enabled in nd_config.ini, we will enable DMS recording by default
         * If it is disabled, then only we will go ahead and check for dms_camera config for
         * dms recording only without drowsy */
        int dms_drowsy = 0;
        if (temp_nd.getParseStatus()) {
            string_to_integer(temp_nd.getConfig("dms_drowsy", "enabled", "0", get_override_val, is_val_overridden), dms_drowsy);
            if (dms_drowsy) {
                LOG_I(TAG, "dms_drowsy is enabled in nd_config.ini, enable dms recording also");
                cams_enabled[DEVICE_CAMERA_POSITION_DMS] = true;
            }
        }
        if (!dms_drowsy && temp.getParseStatus()) {
            if (temp.getConfig("dms_camera", "enabled", "", get_override_val, is_val_overridden) == "true") {
                LOG_I(TAG, "dms_drowsy is disabled in nd_config.ini but dms_camera is enabled in override, enable dms recording");
                cams_enabled[DEVICE_CAMERA_POSITION_DMS] = true;
            } else {
                LOG_I(TAG, "DMS camera is disabled");
                cams_enabled[DEVICE_CAMERA_POSITION_DMS] = false;
            }
        }
        if (cams_enabled[DEVICE_CAMERA_POSITION_DMS] && is_dms_connection_status_file_present()) {
            if (ctx.is_dms_connected == false) {
                LOG_I(TAG, "DMS camera is not recording, disabling DMS camera");
                cams_enabled[DEVICE_CAMERA_POSITION_DMS] = false;
            }
        }
        get_override_val = true;
        is_val_overridden = false;
    } else {
        cams_enabled[DEVICE_CAMERA_POSITION_DMS] = false;
    }

    if (!cams_enabled[DEVICE_CAMERA_POSITION_DMS])
        dms_ld = false; // disable ld recording also, if dms camera is disabled

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

#ifdef BAGHEERA2
        int num_cams = 1;
#elif KRAIT
        int num_cams = DEVICE_CAMERA_POSITION_MAX;
#endif
        for (int i = 0; i < num_cams; i++) {
            stringstream property_str;
            property_str << "CAM" << i << "_CRASH_COUNT";
            prop_data_t entry;
            if (db_handle_camera_crash != NULL) {
                if (get_property_DB(property_str.str(), &entry, db_handle_camera_crash, CAMERA_CRASH_DB_TABLE)) {
                    string_to_integer(entry.value, cam_crash_count[i]);
                    LOG_I (TAG,"crash count for cam %d is at %d", i, cam_crash_count[i]);
                }
            }
        }
    } else {
        LOG_E (TAG, "Can't parse bagheera_config.ini");
    }

    delete c;
    return true;
}

static bool get_ld_enabled()
{
    Config_parser c(BAGHEERACONFIG_INI);

    bool get_override_val = true;
    bool is_val_overridden = false;
    if (c.getParseStatus()) {
        if (c.getConfig ("camera", "outward_ld_enabled", "false", get_override_val, is_val_overridden) == "false") {
            LOG_I (TAG, "Data Product low_fps implementation is enabled");
            outward_ld = false;
        }
        if (c.getConfig ("camera", "inward_ld_enabled", "false", get_override_val, is_val_overridden) == "false") {
            LOG_I (TAG, "Data Product low_fps implementation is enabled");
            inward_ld = false;
        }
        if (c.getConfig ("camera", "dms_ld_enabled", "false", get_override_val, is_val_overridden) == "true") {
            LOG_I (TAG, "Data Product low_fps implementation is enabled");
            dms_ld = true;
        }
    }

    return true;
}

static bool get_dp_enabled()
{
    dp_enabled = false;

    Config_parser c(BAGHEERACONFIG_INI);

    bool get_override_val = true;
    bool is_val_overridden = false;
    if (c.getParseStatus()) {
        if (c.getConfig ("data_products", "enabled", "false", get_override_val, is_val_overridden) == "true") {
            LOG_I (TAG, "Data Product low_fps implementation is enabled");
            dp_enabled = true;
        }
    }

    return true;
}

static bool get_ea_enabled()
{
    ea_enabled = 0;

    Config_parser *bagheera_config = new Config_parser(BAGHEERACONFIG_INI);

    bool get_override_val = true;
    bool is_val_overridden = false;
    string temp = bagheera_config->getConfig("ea_config", "enabled", "0", get_override_val, is_val_overridden);
    string_to_integer(temp.c_str(), ea_enabled);
    if (ea_enabled == 0) {
        LOG_I(TAG, "Event Access Preview Feature is disabled");
    } else if (ea_enabled == 1) {
        LOG_I(TAG, "Event Access Preview Feature is enabled");
        temp = bagheera_config->getConfig("ea_config", "outward", "0", get_override_val, is_val_overridden);
        string_to_integer(temp.c_str(), ea_outward_enabled);
    } else {
        LOG_I(TAG, "Invalid value %d provided for enabled field under ea_config section, forcing ea_enabled to default value (0) thereby disabling Event Access Preview Feature", ea_enabled);
    }

    delete bagheera_config;
    return true;
}

static bool get_audio_enable()
{
    audio_enable = false;

    Config_parser c(BAGHEERACONFIG_INI);

    bool get_override_val = true;
    bool is_val_overridden = false;
    if (c.getParseStatus())
    {
        if (c.getConfig ("camera", "audio_enable", "false", get_override_val, is_val_overridden) == "true")
        {
            LOG_I (TAG,"audio recording is enabled");
            audio_enable = true;
        }
    }

    if (c.getParseStatus())
    {
        if (c.getConfig ("camera", "audio_encryption", "false", get_override_val, is_val_overridden) == "false")
        {
            LOG_I (TAG,"audio encryption is disabled");
            audio_encryption = false;
        }
    }

    return true;
}

int get_num_digital_cams_enabled()
{
    int i = 0;
    int num_cams_enabled = 0;

    for (i = 0; i < NUM_CAMERAS; i++) {
        if (cams_enabled[i]) {
            num_cams_enabled++;
        }
    }

    if (cams_enabled[DEVICE_CAMERA_POSITION_DMS])
        num_cams_enabled++;

    return num_cams_enabled;
}

int get_num_analog_cams_enabled()
{
    int i = 0;
    int num_cams_enabled = 0;

    for (i = 0; i < NUM_EXT_CAMERAS; i++) {
        if (ctx.ext_cam_enabled[i]) {
            num_cams_enabled++;
        }
    }

    return num_cams_enabled;
}

int get_num_digital_cam_files_recording()
{
    int i = 0;
    int num_cams_enabled = get_num_digital_cams_enabled();
    //total no of files recording in /home/iriscli/files including HD+LD for digital and native cams
    int num_files_recording_enabled = outward_ld + inward_ld + dms_ld + num_cams_enabled;

    LOG_I(TAG,"get_num_digital_cam_files_recording: num_cams_enabled=%d, outward_ld=%d, inward_ld=%d, dms_ld=%d, total files recording=%d",
          num_cams_enabled, outward_ld, inward_ld, dms_ld, num_files_recording_enabled);
    return num_files_recording_enabled;
}

//moved to factory class
#if 0
void execute_cmd (string cmd, string tag)
{
    char buffer [LINE_LENGTH] = {'\0'};
    FILE *fp = NULL;

    LOG_I (TAG,"cmd: %s",cmd.c_str());
    fp = popen (cmd.c_str(),"r");
    if (fp == NULL)
    {
        LOG_E (tag.c_str(),"Failed to execute %d",cmd.c_str());
        return;
    }
    while (fgets (buffer, sizeof (buffer), fp))
    {
        LOG_I (tag.c_str(),"%s", buffer);
    }

    pclose (fp);
}
#endif

static bool crash_log_task (void *arg)
{
    LOG_I(TAG,"*************CRASH LOGS*****************");
    for (int i = 0; i < (sizeof (crash_log_cmds)/sizeof (string)); i++)
    {
        execute_cmd (crash_log_cmds[i], "CRASH_LOGS");
    }

    //Print MIPI status 10 times at the interval of 100ms
    int repeat = 0;
    static const int delay = 100;
    while( repeat -- ) {
        for (int i=0; i< (sizeof (mipi_status_logs)/sizeof(string)); i++)
        {
            execute_cmd(mipi_status_logs[i], "MIPI_STATUS");
        }

        usleep(delay*1000);
    }
    LOG_I(TAG,"*************CRASH LOGS END*****************");
    return true;
}

static bool setup_rtcpu_logs_task()
{
    // Define RTCPU sysfs paths with enable/disable values and descriptions
    std::map<string, std::tuple<string, string, string>> rtcpu_settings = {
        // {path, {enable_value, disable_value, description}}
        {"/sys/kernel/debug/tracing/tracing_on", {"1", "0", "tracing_on"}},
        {"/sys/kernel/debug/tracing/buffer_size_kb", {"30720", "", "buffer_size_kb"}}, // only needed for enable
        {"/sys/kernel/debug/tracing/events/tegra_rtcpu/enable", {"1", "0", "tegra_rtcpu"}},
        {"/sys/kernel/debug/tracing/events/freertos/enable", {"1", "0", "freertos"}},
        {"/sys/kernel/debug/camrtc/log-level", {"2", "0", "camrtc_log_level"}},
        {"/sys/kernel/debug/tracing/events/camera_common/enable", {"1", "0", "camera_common"}}
    };

    bool enable_logs = trace_logs_enable;
    string operation = enable_logs ? "enable" : "disable";

    LOG_I(TAG, enable_logs ?
          "*************SETTING UP RTCPU LOGS*****************" :
          "Trace logs disabled, therefore disabling RTCPU /sys/fs flags if enabled");

    bool success = true;
    for (const auto& setting : rtcpu_settings) {
        const string& path = setting.first;
        const string& enable_val = std::get<0>(setting.second);
        const string& disable_val = std::get<1>(setting.second);
        const string& description = std::get<2>(setting.second);

        string value = enable_logs ? enable_val : disable_val;

        // Skip if disable value is empty (like buffer_size_kb)
        if (!enable_logs && disable_val.empty()) {
            continue;
        }

        int int_value = std::stoi(value);
        if (!write_into_sysfs_entry(path, int_value)) {
            LOG_E(TAG, "Failed to write %s to %s for %s %s", value.c_str(), path.c_str(), description.c_str(), operation.c_str());
            success = false;
        }
    }

    LOG_I(TAG, success ?
          ("*************RTCPU LOGS " + operation + " COMPLETE*****************").c_str() :
          ("*************RTCPU LOGS " + operation + " FAILED*****************").c_str());
    
    return success;
}

static bool trace_log_task (void *arg)
{
    LOG_I(TAG,"*************TRACE LOGS*****************");

    stringstream ss, file_name;
#ifdef NO_SDCARD
    const string trace_log = "/media/data/nd_sdcard/trace.log";
#else
    const string trace_log = "/media/SdCard/trace.log";

#endif

    // save the contents
    ss.str("");
    ss << "cat /sys/kernel/debug/tracing/trace > " << trace_log;
    execute_cmd (ss.str(), "TRACE_LOGS");

    // compress the contents
    ss.str("");
    file_name << "/home/ubuntu/.nddevice/log/ndcentral/trace_" << get_system_time() << ".tar.gz";
    ss << "tar -cvzf " << file_name.str() << " " << trace_log;
    execute_cmd (ss.str(), "TRACE_LOGS");

    // rename to .log file
    ss.str("");
    ss << "mv " << file_name.str() << " " << file_name.str() << ".log";
    execute_cmd (ss.str(), "TRACE_LOGS");

    // delete original
#ifdef NO_SDCARD
    execute_cmd ("rm /media/data/nd_sdcard/trace.log", "TRACE_LOGS");
#else
    execute_cmd ("rm /media/SdCard/trace.log", "TRACE_LOGS");
#endif

    LOG_I(TAG,"*************TRACE LOGS END*****************");
    return true;
}

void startup_logs() {
    LOG_I(TAG,"*************STARTUP LOGS*****************");
    for (int i=0; i< (sizeof (boot_logs_cmds)/sizeof(string)); i++)
    {
        execute_cmd(boot_logs_cmds[i], "STARTUP_LOGS");
    }
    LOG_I(TAG,"*************STARTUP LOGS END*****************");
}

void send_qr_scan_info_healthstats(string filename)
{
    LOG_I(TAG, "Sending QR scan info to healthstats: File = %s", filename.c_str());

    int qr_codes_detected_count = ctx.qr_codes_detected[!(ctx.session_flipflop)];
    int qr_codes_decoded_count = ctx.qr_codes_decoded[!(ctx.session_flipflop)];
    int qr_codes_mismatched_count = ctx.qr_codes_mismatched[!(ctx.session_flipflop)];

    LOG_I(TAG, "QR codes detected = %d, QR codes decoded = %d, QR codes mismatched = %d", qr_codes_detected_count, qr_codes_decoded_count, qr_codes_mismatched_count);

    json_t *root = json_object();
    json_t *qr_info = json_object();
    char* req_params = NULL;

    // Build the JSON structure
    json_object_set_new(root, "session", json_string(filename.c_str()));
    json_object_set_new(qr_info, "qr_codes_detected_count", json_integer(qr_codes_detected_count));
    json_object_set_new(qr_info, "qr_codes_decoded_count", json_integer(qr_codes_decoded_count));
    json_object_set_new(qr_info, "qr_codes_mismatched_count", json_integer(qr_codes_mismatched_count));
    json_object_set_new(root, "qr_scan_info", qr_info);

    // Serialize the JSON
    req_params = json_dumps(root, JSON_INDENT(4)); // Pretty print with indentation
    if (req_params == NULL) {
        LOG_E(TAG, "JSON creation failed for HS message of QR scan info");
        json_decref(root);
        return;
    }

    printf("sending QR scan info msg to HS: %s\n", req_params);
    int length = strlen(req_params);
    nd_service_obj->send_msg_healthstats(req_params, length);

    // Cleanup
    json_decref(root);
    free(req_params);
}

void send_session_irled_info_healthstats(string filename, int irled_status, string irled_states)
{
    LOG_I(TAG, "File = %s", filename.c_str());
    LOG_I(TAG, "IR LED status = %d, IR LED states = %s", irled_status, irled_states.c_str());

    json_t *root = json_object();
    json_t *irled_info = json_object();
    json_t *irled_states_json = NULL;
    char* req_params = NULL;

    // Parse irled_states string into a JSON array
    json_error_t error;
    irled_states_json = json_loads(irled_states.c_str(), 0, &error);
    if (!irled_states_json) {
        LOG_E(TAG, "Failed to parse irled_states JSON: %s at line %d", error.text, error.line);
        json_decref(root);
        return;
    }

    // Build the JSON structure
    json_object_set_new(root, "session", json_string(filename.c_str()));
    json_object_set_new(irled_info, "irled_status", json_integer(irled_status));
    json_object_set_new(irled_info, "irled_states", irled_states_json);
    json_object_set_new(root, "irled_info", irled_info);

    // Serialize the JSON
    req_params = json_dumps(root, JSON_INDENT(4)); // Pretty print with indentation
    if (req_params == NULL) {
        LOG_E(TAG, "JSON creation failed for HS message of irled info");
        json_decref(root);
        return;
    }

//    LOG_I(TAG, "sending irled info msg to HS: %s", req_params);
    printf("sending irled info msg to HS: %s\n", req_params);
    int length = strlen(req_params);
    nd_service_obj->send_msg_healthstats(req_params, length);

    // Cleanup
    json_decref(root);
    free(req_params);
}

void send_alert_info_recording_info_healthstats(string filename, int64_t starttime, int64_t endtime)
{
    LOG_I(TAG, "File = %s" , filename.c_str());
    LOG_I(TAG, "Recording start time = %llu end time = %llu" , starttime, endtime);

    json_t *root = json_object();
    char* req_params = NULL;
    json_t *alert_info = json_object();
    json_object_set_new( root, "session", json_string(filename.c_str()) );

    json_object_set_new( alert_info, "recordingstart", json_integer(starttime) );
    json_object_set_new( alert_info, "recordingend", json_integer(endtime));

    json_object_set_new( root, "alert_info", alert_info );

    req_params = json_dumps(root, 0);

    if(req_params == NULL){
        LOG_E(TAG,"JSON creation failed for HS message");
        json_decref(root);
        return;
    }
    LOG_I(TAG, "sending msg to hs : %s", req_params);
    int length = strlen(req_params);
    nd_service_obj->send_msg_healthstats(req_params, length);
    json_decref(root);
    free(req_params);
}

static bool audio_playback_thread_exit_tt(void *args)
{
    audio_monitor_util_deinit(); // Join audio monitor thread
    return true;
}

static bool audio_playback_thread_exit()
{
    task_result_t task_result = nd_timed_task(audio_playback_thread_exit_tt, AUDIO_PLAYBACK_THREAD_EXIT_TIMEOUT, (void *)NULL, "audio_playback_thread_exit");
    if (task_result != TASK_SUCCESS) {
        LOG_E (TAG, "nd_timed_task for audio_playback_thread_exit_tt, timedout");
        return false;
    }
    LOG_I(TAG, "audio_playback_thread_exit_tt return status %d", task_result);
    return true;
}

void record_component_errorcb(component_error_t error, void *crash_status)
{
    static bool err_cb = false;
    string cmd;
    pthread_t th_crash_log;
    bool cam_boot_status = false;
    int cam_num = -1;
    task_result_t task_result;

    if (crash_status == NULL) {
        LOG_E(TAG, "received crash_status as NULL");
        return;
    }
    LOG_E(TAG, "Camera error callback");

    pthread_mutex_lock (&crash_logs_mutex);

    cam_crash_status_t* temp_status = (cam_crash_status_t *)crash_status;
    LOG_I (TAG, "camera_crash_status %d, %d, %d, %d, %d", temp_status->status[0], temp_status->status[1],
                       temp_status->status[2], temp_status->status[3], temp_status->status[8]);

#ifdef BAGHEERA2
#if 0
    task_result_t task_result = nd_timed_task (crash_log_task, CRASH_LOG_TIMEOUT, NULL, "crash log task", false);
    if (task_result != TASK_SUCCESS) {
        LOG_E(TAG, "crash log task create failed");
        err_cb = true;
    }
#endif

    if (trace_logs_enable == true) {
        task_result = nd_timed_task (trace_log_task, TRACE_LOG_TIMEOUT, NULL, "trace log task", false);
        if (task_result != TASK_SUCCESS) {
            LOG_E(TAG, "trace log task create failed");
            err_cb = true;
        }
    }
#endif

    for (int i = 0; i < CAMERA_POSITION_MAXIMUM; i++) {
        if ((cams_enabled[i] == true) && (temp_status->status[i] == true)) {
            cam_num = i;
            break;
        }
    }

    if (FATAL_SHM_BLOCK == error) {
        LOG_E(TAG, "Shared memory blocked for cam_num %d", cam_num);
        bagheera_service_exiting = true;
        nd_service_obj->send_err_msg(SM_E_NDC_CAM_SHM_FAIL, cam_num, "Camera SHM fail");
        pthread_mutex_unlock (&crash_logs_mutex);
        _exit(1);
    }

    /* 1 added to support DMS camera */
    for (int i = 0; i < (DEVICE_CAMERA_POSITION_MAX+1); i++) {
        if (i == DEVICE_CAMERA_POSITION_MAX)
            i = DEVICE_CAMERA_POSITION_DMS;
        if ((cams_enabled[i] == true) && (temp_status->status[i] == true)) {
            LOG_E(TAG, "cam_num %d is crashed", i);

#ifdef BAGHEERA2
            if (i == DEVICE_CAMERA_POSITION_LEFT) {

                if ( TASK_SUCCESS == nd_device_obj->get_retry_left_isp_status_tt() ) {
                    LOG_E(TAG, "Camera LPM crash for cam_num %d", i);
                    nd_service_obj->send_err_msg(SM_E_NDC_CAM_LPM_CRASH, i, "Camera LPM crash");
                }
                else {
                    nd_service_obj->send_err_msg(SM_E_NDC_CAM_CRASH, i, "Camera crash");
                }

            } else if (i == DEVICE_CAMERA_POSITION_RIGHT) {

                if ( TASK_SUCCESS == nd_device_obj->get_retry_right_isp_status_tt()) {
                    LOG_E(TAG, "Camera LPM crash for cam_num %d", i);
                    nd_service_obj->send_err_msg(SM_E_NDC_CAM_LPM_CRASH, i, "Camera LPM crash");
                }
                else {
                    nd_service_obj->send_err_msg(SM_E_NDC_CAM_CRASH, i, "Camera crash");
                }

            } else {
            	nd_service_obj->send_err_msg(SM_E_NDC_CAM_CRASH, i, "Camera crash");
            }

            cam_boot_status = nd_device_obj->check_cam_boot_status(i);

            if (cam_boot_status)
                LOG_I(TAG, "Camera %d boot status is true", i);
            else
                LOG_E(TAG, "Camera %d boot status is false", i);

            stringstream property_str;
            property_str << "CAM" << i << "_CRASH_COUNT";

            cam_crash_count[i]++;
            LOG_I (TAG,"set_property %s with %d", property_str.str().c_str(), cam_crash_count[i]);

            if (db_handle_camera_crash != NULL) {
                if ((set_property_DB(property_str.str(), to_string(cam_crash_count[i]), db_handle_camera_crash, CAMERA_CRASH_DB_TABLE)) == false) {
                    nd_service_obj->send_err_msg(SM_E_NDC_SET_PROP_DB_FAIL, i, "set_property_DB failed for camera crash count" );
                    LOG_E(TAG, "set_property_DB failed for cam %d", i);
                }
            }

            if ((i == DEVICE_CAMERA_POSITION_LEFT) || (i == DEVICE_CAMERA_POSITION_RIGHT)) {
                if ((insert_property_DB(property_str.str(), to_string(cam_crash_count[i]), db_handle_camera_crash, SIDE_CAM_CRASH_INFO_DB_TABLE)) == false) {
                    LOG_E(TAG, "insert_property_DB failed for side cam %d", i);
                }
            }
#elif KRAIT
            nd_service_obj->send_err_msg(SM_E_NDC_CAM_CRASH, i, "Camera crash");
            stringstream property_str;
            property_str << "CAM" << i << "_CRASH_COUNT";

            cam_crash_count[i]++;
            LOG_I (TAG,"set_property %s with %d", property_str.str().c_str(), cam_crash_count[i]);

            if (db_handle_camera_crash != NULL) {
                if ((set_property_DB(property_str.str(), to_string(cam_crash_count[i]), db_handle_camera_crash, CAMERA_CRASH_DB_TABLE)) == false) {
                    nd_service_obj->send_err_msg(SM_E_NDC_SET_PROP_DB_FAIL, i, "set_property_DB failed for camera crash count" );
                    LOG_E(TAG, "set_property_DB failed for cam %d", i);
                }
            }
#endif
        }
    }

    //For debugging camera crash:
    //We log system uptime and list contents of ND_FILES_PATH
    cmd = "ls -l " + ctx.base_path_cam0 + " 2>&1";
    execute_cmd (cmd.c_str(), "CRASH_LOGS");

#ifdef BAGHEERA2
    int64_t uptime = get_system_monotonic_time();
    LOG_I (TAG, "System Uptime : %lld seconds", uptime / 1000);

    if (cam_num == DEVICE_CAMERA_POSITION_FRONT ) {
        LOG_C(TAG, "###TIME TO RESTART MACHINE###");

        //// log it in a separate file
        LOG_E(TAG, "common_log_dir.c_str() %s", common_log_dir.c_str());

        ofstream reboot_log;
        string common_log = common_log_dir + "/ndc_common.log";
        stringstream content;
        int64_t time = get_system_time();
        content << "cam_num "  << cam_num << " failed @ time " << time << " , rebooting" << endl ;

        reboot_log.open (common_log.c_str(), std::ofstream::out | std::ofstream::app);
        reboot_log << content.str();
        reboot_log.close();

        LOG_E(TAG, "content.str() %s", content.str().c_str());
        LOG_E(TAG, "common_log.c_str() %s", common_log.c_str());

        if (send_powermon_to_reboot(Q_NAME,REQ_POWERMON_CAM_CRASH_TO_REBOOT) == false) {
            bool reboot_status = system_reboot();
            if (reboot_status == true) {
                LOG_I(TAG, "reboot_status == true; wait for a while to get killed");
                sleep(5);
            }
            LOG_C (TAG, "System reboot did not occur, not expected to be here");

            string str_msg = "System reboot did not occur, not expected to be here reboot_status " + std::to_string(reboot_status);
            LOG_C(TAG, str_msg.c_str());

            nd_service_obj->send_err_msg(SM_E_PM_SYS_RBT_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );

            static const int rtc_timeout_seconds = 20;
            bool rtc_status = false;
            rtc_status = nd_device_obj->configure_rtc_time(rtc_timeout_seconds);
            if (rtc_status == false) {
                LOG_C(TAG, "Cannot set RTC timer status: %d", rtc_status);
            }
            usleep(500 * 1000);
            LOG_I(TAG, "Set gpio for por");
            nd_device_obj->gpio_por_assert();
            pthread_mutex_unlock (&crash_logs_mutex);
            nd_service_obj->send_err_msg(SM_E_PM_POR_GPIO_FAIL, NDService::UNUSED_ERR_AUX_CODE,
                                "gpio_por_assert failed in cam crash NDC; handle this" );

            while (1) {
                LOG_E(TAG, "waiting for svc timeout and watchdog");
                sleep(10);
            }
        }
    }
#elif KRAIT
    bagheera_service_exiting = true;

    struct sysinfo info;
    if (!sysinfo (&info))
        LOG_I (TAG, "System Uptime : %ld seconds", info.uptime);
    else
        LOG_E (TAG, "Failed to get sysinfo with errno %d", errno);

    if (cam_num == DEVICE_CAMERA_POSITION_FRONT ){
        //// log it in a separate file
        LOG_E(TAG, "common_log_dir.c_str() %s", common_log_dir.c_str());

        ofstream reboot_log;
        string common_log = common_log_dir + "/ndc_common.log";
        stringstream content;
        int64_t time = get_system_time();
        content << "cam_num "  << cam_num << " failed @ time " << time << " , not rebooting" << endl ;

        reboot_log.open (common_log.c_str(), std::ofstream::out | std::ofstream::app);
        reboot_log << content.str();
        reboot_log.close();

        LOG_E(TAG, "content.str() %s", content.str().c_str());
        LOG_E(TAG, "common_log.c_str() %s", common_log.c_str());

#if OUTWARD_CRASH_REBOOT
        LOG_C(TAG, "###TIME TO RESTART MACHINE###");
        bool reboot_status = system_reboot();
        if (reboot_status == true) {
            LOG_I(TAG, "reboot_status == true; wait for a while to get killed");
            sleep(5);
        }
        LOG_C (TAG, "System reboot did not occur, not expected to be here");

        string str_msg = "System reboot did not occur, not expected to be here reboot_status " + std::to_string(reboot_status);
        LOG_C(TAG, str_msg.c_str());

        nd_service_obj->send_err_msg(SM_E_PM_SYS_RBT_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg);

        static const int rtc_timeout_seconds = 20;
        bool rtc_status = false;
        rtc_status = set_RTC_time(rtc_timeout_seconds);
        if (rtc_status == false) {
            LOG_C(TAG, "Cannot set RTC timer status: %d", rtc_status);
        }
        usleep(500 * 1000);
        LOG_I(TAG, "Set gpio for por");
        set_por_gpio();
        nd_service_obj->send_err_msg(SM_E_PM_POR_GPIO_FAIL, NDService::UNUSED_ERR_AUX_CODE,
                            "set_por_gpio failed in cam crash NDC; handle this" );
        while (1) {
            LOG_E(TAG, "waiting for svc timeout and watchdog");
            sleep(10);
        }
#endif
        pthread_mutex_unlock (&crash_logs_mutex);
        // Need to join audio playback thread to exit bagheera gracefully
        audio_playback_thread_exit();
        exit(1);
    }

    // TEMP HACK TO ADRESS DRIVER CAM CRASH
    // EXITING FROM BAGHEERA
    // SERVICE WILL BE RESTARTED AFTER 10 SECS AUTOMATICALLY
    if (cam_num == DEVICE_CAMERA_POSITION_BACK) {
        LOG_C(TAG, "###TIME TO RESTART BAGHEERA SERVICE###");
        LOG_E(TAG, "inward camera crash");
        pthread_mutex_unlock (&crash_logs_mutex);
        LOG_E(TAG, "exiting from bagheera with exit(1) command");
        // Need to join audio playback thread to exit bagheera gracefully
        audio_playback_thread_exit();
        exit(1);
    }
#endif

    pthread_mutex_unlock (&crash_logs_mutex);
    return;
}

string record_fnamecb (void *app, uint64_t session_first_frame_epoch_time_ms)
{
    int cam_num = *((int*)(&app));
    prop_data_t entry;
    int64_t sessionCount = 0;
    stringstream ss;
    string fname = "";

    if (cam_num == DEVICE_CAMERA_POSITION_FRONT) {
        pthread_mutex_lock(&nextVideoName_mutex);
        if (!fnamecb_invoked)
            fnamecb_invoked = true;

        if ((nextvid_fname == "") || (currvid_fname == nextvid_fname)) {
            if (get_property_DB("udid", &entry, db_handle)) {
                ctx.udid_string = entry.value;
            }
            if (!increment_property_DB("sessionCount")) {
                LOG_E(TAG, "failed to increment sessionCount to gen props DB");
            }
            ctx.sessionCount_string = "";
            if (get_property_DB("sessionCount", &entry, db_handle)) {
                ctx.sessionCount_string = entry.value;
            }
            string_to_int64(ctx.sessionCount_string, sessionCount);
            string sessionCountHex_string, udidHex_string ;
            decimalStr_to_hexStr(ctx.sessionCount_string, sessionCountHex_string, sessionCountSize);
            decimalStr_to_hexStr(ctx.udid_string, udidHex_string, udidSize);

            string fileNameBase = "trip" + udidHex_string + "_part" + sessionCountHex_string ;
            LOG_D(TAG, "file: %s, udid: %s, sessionCount: %s, hexStr: %s", fileNameBase.c_str(), ctx.udid_string.c_str(), ctx.sessionCount_string.c_str(), sessionCountHex_string.c_str());

            nextvid_fname = get_fname(fileNameBase, session_first_frame_epoch_time_ms);
            LOG_I(TAG, "Filling nextvid_fname with %s during record_fnamecb", nextvid_fname.c_str());
        } else {
            ctx.sessionCount_string = "";
            if ( get_property_DB("sessionCount", &entry, db_handle) ) {
                ctx.sessionCount_string = entry.value;
            }
            string_to_int64(ctx.sessionCount_string, sessionCount) ;
        }
        ss.str("");
        ss << cam_num << nextvid_fname;
        fname = ss.str();

        if (audio_enable == true) {
            if (ctx.audio->audio_pcm_partial_file_base_path == "") {
                ctx.audio->audio_pcm_partial_file_base_path = ctx.base_path_cam0 + "/" + fname + AUDIO_PARTIAL_FILE_SUFFIX ;
                ctx.audio->next_audio_pcm_partial_file_base_path = ctx.base_path_cam0 + "/" + fname + AUDIO_PARTIAL_FILE_SUFFIX ;
                LOG_I(TAG, "partial pcm file was "", now: %s", ctx.audio->audio_pcm_partial_file_base_path.c_str());
            } else {
                ctx.audio->next_audio_pcm_partial_file_base_path = ctx.base_path_cam0 + "/" + fname + AUDIO_PARTIAL_FILE_SUFFIX ;
            }
        }
        pthread_mutex_unlock(&nextVideoName_mutex);

        // Block to define scope of sessionMapCountMutex
        {
            std::lock_guard<std::mutex> lock(sessionMapCountMutex);
            LOG_I(TAG, "sessionCount_string: %s, sessionMapCount_size: %d", ctx.sessionCount_string.c_str(), sessionMapCount.size() );
            std::unordered_map<std::string, int64_t>::iterator it = sessionMapCount.begin();
            while (it != sessionMapCount.end()) {
                if (it->second < sessionCount-2) {
                    it = sessionMapCount.erase(it);
                } else {
                    it++;
                }
            }
            LOG_I(TAG, "%s is added in map with session_count %d", nextvid_fname.c_str(), sessionCount);
            sessionMapCount[nextvid_fname] = sessionCount ;
       }

        if (nextvid_fname_temp == "") {
            nextvid_fname_temp = nextvid_fname;
#ifdef BAGHEERA2
            string next_session_fname = nextvid_fname + ".mp4";
            LOG_I(TAG, "send START_NEXT_SESSION message to cam_rec service with Filename: %s", next_session_fname.c_str());
            send_start_next_session_msg_to_cam_rec_service(Q_NAME, next_session_fname.c_str());
#endif
        }
        currvid_fname = nextvid_fname;

        if (file_is_present(lpw_no_record_persistent_file)) {
            /* For handling mix privacy in LPW no record */
            int num_files_recording = get_num_digital_cam_files_recording();
            string cur_session_fname = currvid_fname;

            if (cams_enabled[DEVICE_CAMERA_POSITION_BACK]) {
                if (cur_session_fname != "") {
                    set_device_mode_for_fname(cur_session_fname, ctx.fused_privacy, ctx.idle_mode_RT_thread, num_files_recording, privacy_reason);
                    set_device_mode_for_fname(cur_session_fname, ctx.off_duty_privacy, ctx.idle_mode_RT_thread, num_files_recording, REASON_OFFDUTY);
                    set_device_mode_for_fname(cur_session_fname, ctx.geofence_privacy, ctx.idle_mode_RT_thread, num_files_recording, REASON_GEOFENCE);
                    set_privacy_mode_led(ctx.fused_privacy && ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_BACK]);
                    check_set_irled(REASON_PRIVACY);
                }
            }
        }

        pthread_mutex_lock(&meta_partial_file_mutex);
        if (ctx.meta_csv_partial_path == "") {
            ctx.meta_csv_partial_path = ctx.base_path_cam0 + "/" + fname + "_partial.csv" ;
            ctx.fstream_partial_meta_file.open (ctx.meta_csv_partial_path.c_str() );
            LOG_I (TAG,"opening partial meta file: %s", ctx.meta_csv_partial_path.c_str());
            ctx.fstream_partial_meta_file << "latitude, longitude, altitude, accuracy, bearing, speed, timestamp" << endl;

            stringstream ss_p_meta("");
            ss_p_meta.str("");
            ss_p_meta << "outward_privacy" << ", " << ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_FRONT];
            ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;

            ss_p_meta.str("");
            ss_p_meta << "inward_privacy" << ", " << ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_BACK];
            ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;

            ss_p_meta.str("");
            ss_p_meta << "left_privacy" << ", " << ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_LEFT];
            ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;

            ss_p_meta.str("");
            ss_p_meta << "right_privacy" << ", " << ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_RIGHT];
            ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;

            ss_p_meta.str("");
            ss_p_meta << "ext_cam_privacy" << ", " << ctx.privacy_params.ext_cam_privacy;
            ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;

            ss_p_meta.str("");
            ss_p_meta << "driveri_audio_privacy" << ", " << ctx.privacy_params.driveri_audio_privacy;
            ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;

            ss_p_meta.str("");
            ss_p_meta << "ext_cam_audio_privacy" << ", " << ctx.privacy_params.ext_cam_audio_privacy;
            ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;

            ss_p_meta.str("");
            ss_p_meta << "gps_privacy" << ", " << ctx.privacy_params.gps_privacy;
            ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;

            ss_p_meta.str("");
            ss_p_meta << "personal_privacy" << ", " << ctx.off_duty_privacy;
            ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;

            ss_p_meta.str("");
            ss_p_meta << "geofence_privacy" << ", " << ctx.geofence_privacy;
            ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;

            ss_p_meta.str("");
            ss_p_meta << "enhanced_privacy" << ", " << ctx.privacy_params.enhanced_privacy;
            ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;

            ss_p_meta.str("");
            ss_p_meta << "save_user_alert_video" << ", " << ctx.privacy_params.save_user_alert_video;
            ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;

            ss_p_meta.str("");
            ss_p_meta << "upload_video_outward" << ", " << ctx.privacy_params.upload_video[DEVICE_CAMERA_POSITION_FRONT];
            ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;

            ss_p_meta.str("");
            ss_p_meta << "upload_video_inward" << ", " << ctx.privacy_params.upload_video[DEVICE_CAMERA_POSITION_BACK];
            ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;

            ss_p_meta.str("");
            ss_p_meta << "upload_video_left" << ", " << ctx.privacy_params.upload_video[DEVICE_CAMERA_POSITION_LEFT];
            ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;

            ss_p_meta.str("");
            ss_p_meta << "upload_video_right" << ", " << ctx.privacy_params.upload_video[DEVICE_CAMERA_POSITION_RIGHT];
            ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;

            ss_p_meta.str("");
            ss_p_meta << "upload_video_ext_cam" << ", " << ctx.privacy_params.upload_video_ext_cam;
            ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;
        }
        ctx.next_meta_csv_partial_path = ctx.base_path_cam0 + "/" + fname + "_partial.csv" ;
        LOG_I(TAG, "next_session_partial_meta_filename: %s", ctx.next_meta_csv_partial_path.c_str() );
        pthread_mutex_unlock(&meta_partial_file_mutex);

        //Send message to signify new camera session started
        ndc_cam_restart_msg_t restart_cam_msg;
        restart_cam_msg.type = RESTART_CAMERA;
        restart_cam_msg.len = sizeof( ndc_cam_restart_msg_t );
        restart_cam_msg.cam_num = -1;
        nd_msgq_t::nd_msg_t msg((char *)&restart_cam_msg, sizeof(restart_cam_msg), false);
        ctx.msg_q->send(msg, nd_msgq_t::ND_MSG_MED);

        // determine DIS here so that the same will be communicated to RT thread
        pthread_mutex_lock(&dis_mutex);
        if (ctx.dis_status == 1) {
            ctx.dis_uid_string = ctx.device_config->getConfig("identity", "deviceId", "") + std::to_string(ctx.dis_update_ts);
        } else {
            ctx.dis_uid_string = ctx.device_config->getConfig("identity", "deviceId", "") + std::to_string(get_system_time());
        }
        pthread_mutex_unlock(&dis_mutex);
    } else {
        ss.str("");
        ss << cam_num << nextvid_fname;
        fname = ss.str();
    }

    ss.str("");
    ss << ctx.base_path_cam0 << "/" << fname << video_file_extn;
    LOG_I(TAG, "File name: %s", ss.str().c_str());

    string rt_string = ctx.base_path_cam0 + "/" + fname + ".mkv";

#ifdef BAGHEERA2
    pthread_mutex_lock(&rt_session_id_mutex);
    //This change is to ensure that the session id will be updated for inward camera as well
    //since the cam_rec updates the session id in RECORD_STOP which happens after completion
    //of first session
    for (int i = 0; i <= DEVICE_CAMERA_POSITION_DMS; i++)
    {
        ctx.sessionid_rt[i] = rt_string;
        ctx.yuv_frame_count[i] = 0;
        ctx.sessio_drop_message[i] = 0;
    }
    pthread_mutex_unlock(&rt_session_id_mutex);
#elif KRAIT
    int index = rt_string.find("1_trip", 0);

    pthread_mutex_lock(&rt_session_id_mutex);
    if (index != std::string::npos)
        ctx.sessionid_rt[cam_num] = rt_string.replace(index, sizeof("1_trip") - 1, "0_trip");
    else
        ctx.sessionid_rt[cam_num] = rt_string;
    pthread_mutex_unlock(&rt_session_id_mutex);

    ctx.yuv_frame_count[cam_num] = 0;
    ctx.sessio_drop_message[cam_num] = 0;
#endif
    if ((pub_rt_session_id == 1)) {
        rt_sessionid_msg_t sessionID;
        pthread_mutex_lock(&rt_session_id_mutex);
        strncpy(sessionID.session_id, ctx.sessionid_rt[0].c_str(), ctx.sessionid_rt[0].length() + 1);
        pthread_mutex_unlock(&rt_session_id_mutex);
        bool res = send_msg((generic_msg_t *)&sessionID, RT_SESSION_ID,
                        sizeof( sessionID ), get_msgq_name(), Q_NAME_OBD_PUB, 0);
        LOG_I(TAG, "sessionID %d sent %s", res, sessionID.session_id);
    }

    return ss.str();
}

void record_timestamp_cb(uint64_t time_ld, uint64_t pts_time_ld, void *app)
{
    int cam_num = *((int*)(&app));

    LOG_I(TAG, "record_timestamp: cam = %d, time_ld = %lld, pts_time_ld = %lld, session_cnt_ld = %d", cam_num, time_ld, pts_time_ld, (int)ctx.session_cnt_ld[cam_num]);
    ctx.camStartTime_ld[cam_num][ctx.session_cnt_ld[cam_num] % 2] = time_ld;
    ctx.camPtsStartTime_ld[cam_num][ctx.session_cnt_ld[cam_num] % 2] = pts_time_ld;
    ctx.session_cnt_ld[cam_num]++;

    return;
}

void record_data_prod_cb(uint64_t time_dp, uint64_t pts_time_dp, void *app)
{
    int cam_num = *((int*)(&app));

	LOG_I(TAG, "record_data_prod: epoch_time_ms = %lld, pts_time = %lld, session_cnt = %d", time_dp, pts_time_dp, (int)ctx.session_cnt_dp[cam_num]);

	ctx.camStartTime_dp[cam_num][ctx.session_cnt_dp[cam_num] % 2] = time_dp;
    ctx.camPtsStartTime_dp[cam_num][ctx.session_cnt_dp[cam_num] % 2] = pts_time_dp;

    ctx.session_cnt_dp[cam_num]++;

    return;
}

#ifdef BAGHEERA2
static bool init_camera()
{
    int i=0;
    int cam_boot_status = 0;
    stringstream property_str;

    if (get_cams_enabled() != true)
        LOG_E (TAG, "No configuration info regarding cameras enabled");


    cam_boot_status = check_outward_camera_boot_status();

    if (!(cam_boot_status & OUTWARD_CAM_STATUS_MASK)) {
        LOG_E (TAG, "ISP boot is not successful");
        nd_service_obj->send_err_msg(SM_E_NDC_INIT_CAM_FAIL, 0, "ISP Fail");
        property_str << "CAM0" << "_CRASH_COUNT";

        cam_crash_count[0]++;
        LOG_I (TAG,"set_property with %d, %s", cam_crash_count[0], to_string(cam_crash_count[0]).c_str());

        if (db_handle_camera_crash != NULL) {
            if ((set_property_DB(property_str.str(), to_string(cam_crash_count[0]), db_handle_camera_crash, CAMERA_CRASH_DB_TABLE)) == false) {
                nd_service_obj->send_err_msg(SM_E_NDC_SET_PROP_DB_FAIL, 0, "set_property_DB failed" );
                LOG_E(TAG, "set_property_DB failed for outward cam");
            }
        }
        return false;
    }
    // always enable outward camera irrespective of cloud request.
    cams_enabled[DEVICE_CAMERA_POSITION_FRONT] = true;

    bool val_overridden = false;

    if ("true" == ctx.bagheera_config->getConfig("hdmaps_mode", "enable", "false", true, val_overridden)) {
        ctx.hdmaps_mode_enabled = true;
        LOG_I (TAG, "HDMaps mode enabled");

        if ("true" == ctx.bagheera_config->getConfig("hdmaps_mode", "imu_data", "false", true, val_overridden)) {
            ctx.imu_data = true;
        } else {
            ctx.imu_data = false;
        }
    } else {
        ctx.hdmaps_mode_enabled = false;
        ctx.imu_data = false;
    }

    for (i = 0; i < (NUM_CAMERAS+1); i++) {
        /* Added this condition to support DMS camera */
        if (i == NUM_CAMERAS)
            i += NUM_CAMERAS;
        if ((i == DEVICE_CAMERA_POSITION_FRONT) && (cams_enabled[i])) {
            stringstream cam_name;
            cam_name << "CAM" << i;

            Config_parser bag_conf(BAGHEERACONFIG_INI);
            Config_parser nd_conf_analytics(ND_CONFIG_ANALYTICS);

            fill_rt_config((cam_pos_t)i, &bag_conf, &nd_conf_analytics, &ctx.rt_config[i]);

            if(false == set_rt_status((cam_pos_t)i, &ctx.rt_config[i]))
            {
                LOG_C(TAG, " set_rt_status returned false; check for streaming settings ");
                //return false; // need not return if set_rt_status fails
            }

            // Fill native camera config
            ctx.native_cam_config[i].cam_pos = (cam_pos_t)i;
            ctx.native_cam_config[i].name = cam_name.str();

            ctx.media_recorder[i] = MediaRecorder::create_media_recorder(ctx.native_cam_config[i], ctx.rt_config[i]);
            if( NULL == ctx.media_recorder[i] ) {
                LOG_E(TAG, "Camera %d initialization failed", i);
                return false;
            }

            LOG_I(TAG, "cam_name %s ctx.media_recorder[i] %d", cam_name.str().c_str(), ctx.media_recorder[i]);

            int cam_num = i;

            if (false == ctx.media_recorder[cam_num]->set_record_state_cb(
                       &(mediarecorder_callback), (void *)(size_t)cam_num)) {
                LOG_C(TAG,"Cannot register Media Recorder Front video filename callback");
                return false;
            }

            if (false == ctx.media_recorder[cam_num]->register_first_camera_buffer_notify_callback(
                       &(mediarecorder_firstframe_callback), (void *)(size_t)cam_num ) ) {
                LOG_C(TAG,"Cannot register Media Recorder first frame timer callback");
                return false;
            }

            if (false == ctx.media_recorder[cam_num]->register_error_callback(
                       &(record_component_errorcb), (void *)(&cam_crash_status))) {
                LOG_C(TAG,"Cannot register Media Recorder error callback");
                return false;
            }

            if (false == ctx.media_recorder[cam_num]->register_fname_callback(
                       &(record_fnamecb), (void *)(size_t)cam_num)) {
                LOG_C(TAG,"Cannot register Media Recorder fname callback");
                return false;
            }

            if (false == ctx.media_recorder[cam_num]->register_livestream_framedrop_callback(
                       &(livestream_framedrop_cb), (void *)(size_t)cam_num)) {
                LOG_C(TAG,"Cannot register Media Recorder live stream framedrop callback");
                return false;
            }

            if (false == ctx.media_recorder[cam_num]->register_timestamp_callback(
                       &(record_timestamp_cb), (void *)(size_t)cam_num ) ) {
                LOG_C(TAG,"Cannot register Media Recorder timestamp callback");
                return false;
            }

            if (false == ctx.media_recorder[cam_num]->register_data_prod_callback(
                        &(record_data_prod_cb), (void *)(size_t)cam_num)) {
                LOG_C(TAG, "Cannot register Media Recorder data product callback");
                return false;
            }

            if (false == ctx.media_recorder[cam_num]->register_frame_write_callback(
                       &(frame_write_cb), (void *)(size_t)cam_num)) {
                LOG_C(TAG,"Cannot register Media Recorder frame capture callback");
                return false;
            }

            if (false == ctx.media_recorder[cam_num]->register_appsink_callback(
                       &(appsink_camera_callback), (void *)(size_t)cam_num)) {
                LOG_C(TAG,"Cannot register register_appsink_callback callback");
                return false;
            }

            bool rc = ctx.media_recorder[cam_num]->init_camera_record_session();
            if (rc == false) {
                LOG_E(TAG, "init_camera_record_session failed for cam %d", cam_num);
                nd_service_obj->send_err_msg(SM_E_NDC_CAM_CRASH, cam_num, "init_camera_record_session Fail");
                return false;
            }
        } else if ((i == DEVICE_CAMERA_POSITION_BACK) && (cams_enabled[i])) { // RT configuration for back camera
        	Config_parser bag_conf(BAGHEERACONFIG_INI);
        	Config_parser nd_conf_analytics(ND_CONFIG_ANALYTICS);

            fill_rt_config((cam_pos_t)i, &bag_conf, &nd_conf_analytics, &ctx.rt_config[i]);
            set_rt_status((cam_pos_t)i, &ctx.rt_config[i]);

            if (ctx.rt_config[i].enable_streaming) {
                pthread_t th_inward_RT;
                if (pthread_create(&th_inward_RT, NULL, check_inward_cam_RT_thread, NULL)) {
                    LOG_E (TAG, "Failed to create check_inward_cam_RT_thread.");
                }
            }
        } else if ((i == DEVICE_CAMERA_POSITION_DMS) && (cams_enabled[i])) { // RT configuration for dms camera
#ifdef DMS_CAMERA_SUPPORTED
            Config_parser bag_conf(BAGHEERACONFIG_INI);
            Config_parser nd_conf_analytics(ND_CONFIG_ANALYTICS);

            fill_rt_config((cam_pos_t)i, &bag_conf, &nd_conf_analytics, &ctx.rt_config[i]);

            if (ctx.rt_config[i].enable_streaming) {
                /* Allocate a pool of DMA buffers to share DMS RT data with analytics service */
                create_dmscam_rt_shared_memory(&ctx.rt_config[i]);

                /* Set up a Unix Domain socket to accept connection from analytics service */
                pthread_t socket_setup_th;
                pthread_create(&socket_setup_th, NULL, socket_setup, NULL);

                /* Thread to receive DMA FD from cam_rec service over the socket */
                pthread_t th_dms_RT;
                pthread_create(&th_dms_RT, NULL, rcv_dmabuf_fd_thread, NULL);
            }
#endif
        }
    }

    return true;
}
#elif KRAIT
static bool init_camera()
{
    int i=0;

    if (get_cams_enabled() != true)
        LOG_E (TAG, "No configuration info regarding cameras enabled");

    // always enable outward camera irrespective of cloud request.
    cams_enabled[DEVICE_CAMERA_POSITION_FRONT] = true;

    bool val_overridden = false;

    if ("true" == ctx.bagheera_config->getConfig("hdmaps_mode", "enable", "false", true, val_overridden)) {
        ctx.hdmaps_mode_enabled = true;
        LOG_I (TAG, "HDMaps mode enabled");

        if ("true" == ctx.bagheera_config->getConfig("hdmaps_mode", "imu_data", "false", true, val_overridden)) {
            ctx.imu_data = true;
        } else {
            ctx.imu_data = false;
        }
    } else {
        ctx.hdmaps_mode_enabled = false;
        ctx.imu_data = false;
    }

    for (i = 0; i < NUM_CAMERAS; i++) {
        if (cams_enabled[i]) {
            bool rc = false;
            stringstream cam_name;
            cam_name << "CAM" << i;

            Config_parser bag_conf(BAGHEERACONFIG_INI);
            Config_parser nd_conf_analytics(ND_CONFIG_ANALYTICS);

            fill_rt_config((cam_pos_t)i, &bag_conf, &nd_conf_analytics, &ctx.rt_config[i]);

            if(false == set_rt_status((cam_pos_t)i, &ctx.rt_config[i]))
            {
                LOG_C(TAG, " set_rt_status returned false; check for streaming settings ");
                //return false; // need not return if set_rt_status fails
            }

            // Fill native camera config
            ctx.native_cam_config[i].cam_pos = (cam_pos_t)i;
            ctx.native_cam_config[i].name = cam_name.str();

            ctx.media_recorder[i] = MediaRecorder::create_media_recorder(ctx.native_cam_config[i], ctx.rt_config[i]);
            if( NULL == ctx.media_recorder[i] ) {
                LOG_E(TAG, "Camera %d initialization failed", i);
                return false;
            }

            LOG_I(TAG, "cam_name %s ctx.media_recorder[i] %d", cam_name.str().c_str(), ctx.media_recorder[i]);

            int cam_num = i;

            if (false == ctx.media_recorder[cam_num]->set_record_state_cb(
                       &(mediarecorder_callback), (void *)(size_t)cam_num)) {
                LOG_C(TAG,"Cannot register Media Recorder Front video filename callback");
                return false;
            }

            if (false == ctx.media_recorder[cam_num]->register_first_camera_buffer_notify_callback(
                       &(mediarecorder_firstframe_callback), (void *)(size_t)cam_num ) ) {
                LOG_C(TAG,"Cannot register Media Recorder first frame timer callback");
                return false;
            }

            if (false == ctx.media_recorder[cam_num]->register_error_callback(
                       &(record_component_errorcb), (void *)(&cam_crash_status))) {
                LOG_C(TAG,"Cannot register Media Recorder error callback");
                return false;
            }

            if (false == ctx.media_recorder[cam_num]->register_fname_callback(
                       &(record_fnamecb), (void *)(size_t)cam_num)) {
                LOG_C(TAG,"Cannot register Media Recorder fname callback");
                return false;
            }

            if (false == ctx.media_recorder[cam_num]->register_livestream_framedrop_callback(
                       &(livestream_framedrop_cb), (void *)(size_t)cam_num)) {
                LOG_C(TAG,"Cannot register Media Recorder live stream framedrop callback");
                return false;
            }

            if (false == ctx.media_recorder[cam_num]->register_timestamp_callback(
                       &(record_timestamp_cb), (void *)(size_t)cam_num ) ) {
                LOG_C(TAG,"Cannot register Media Recorder timestamp callback");
                return false;
            }

            if ((i == DEVICE_CAMERA_POSITION_FRONT) || (i == DEVICE_CAMERA_POSITION_BACK)) {
                if (false == ctx.media_recorder[cam_num]->register_appsink_callback(
                           &(appsink_camera_callback),
                           (void *)(size_t)cam_num)) {
                    LOG_C(TAG,"Cannot register register_appsink_callback callback");
                    return false;
                }
            }

            if (i == DEVICE_CAMERA_POSITION_BACK) {
                if (false == ctx.media_recorder[cam_num]->register_qrscan_callback(
                           &(qr_scan_callback),
                           (void *)(size_t)cam_num)) {
                    LOG_C(TAG, "Cannot register qr_scan_callback");
                    return false;
                }
            }

            rc = ctx.media_recorder[cam_num]->init_camera_record_session();
            if (rc == false) {
                LOG_E(TAG, "init_camera_record_session failed for cam %d", cam_num);
                nd_service_obj->send_err_msg(SM_E_NDC_CAM_CRASH, cam_num, "init_camera_record_session Fail");
                if (cam_num == DEVICE_CAMERA_POSITION_FRONT) {
                    return false;
                }
            }
        }
    }

    return true;
}
#endif

static bool init_gps() {
  

    if( false == fill_rt_config_gps() ) {
        LOG_C(TAG,"false == fill_rt_config_gps(); ");
    }

    if( false == fill_rt_config_gps(gps_geo_fence_streaming_socket) ) {
	    LOG_C(TAG,"false == fill_rt_config_gps(gps_geo_fence_streaming_socket); ");
    }


    string counter =  ctx.bagheera_config->getConfig("gps","gps_fail_counter","5");
    LOG_I(TAG, "gps_fail_counter values %s",counter.c_str());
    if(!string_to_integer(counter, ctx.gps_counter))
    {
        LOG_E(TAG, "string to integer gps_counter failed");
    }

    server.create_topic(TOPIC_APM_GPS_DATA);
   
    return true;
}

#ifdef BAGHEERA2
static bool init_ublox() {

    bool override_val = true;
    bool overriden = false;

    bool send_ubx_cfg_msgs = false;
    bool log_ubx_msgs = false;
    bool disable_nmea_msgs = false;
    bool save_ubx_cfg = false;
    Ublox::ublox_constellation_data_t data;
    ctx.ublox_led_indication = false;

    if (ctx.bagheera_config) {
        if ((ctx.bagheera_config->getConfig ("ublox", "enabled","false", override_val, overriden)) == "false") {
            return true;
        }

	    ublox_enabled = true;
	    ctx.ublox_enabled = true;
        LOG_I (TAG, "Ublox data will be recorded if connected");

        if ((ctx.bagheera_config->getConfig ("ublox", "led_indication","true", override_val, overriden)) == "true") {
            ctx.ublox_led_indication = true;
            LOG_I (TAG, "Ublox LED indication");
        }
        if ((ctx.bagheera_config->getConfig ("ublox","send_ubx_cfg_msgs","true", override_val, overriden)) == "true") {
            send_ubx_cfg_msgs =  true;
            LOG_I (TAG, "Will send UBX-CFG-MSG");
        }
        if ((ctx.bagheera_config->getConfig ("ublox","log_ubx_msgs","false", override_val, overriden)) == "true") {
            log_ubx_msgs =  true;
            LOG_I (TAG, "Will log UBX messages");
        }
        if ((ctx.bagheera_config->getConfig ("ublox","save_ubx_cfg","false", override_val, overriden)) == "true") {
            save_ubx_cfg =  true;
            LOG_I (TAG, "Will save UBX configurations");
        }

        if ((ctx.bagheera_config->getConfig ("ublox","disable_nmea_msgs","true", override_val, overriden)) == "true") {
            disable_nmea_msgs =  true;
            LOG_I (TAG, "NMEA messages will be disabled");
        }
        else {
            LOG_I (TAG, "NMEA messages will be enabled");
        }

        if ((ctx.bagheera_config->getConfig ("ublox","enable_GPS","true", override_val, overriden)) == "true") {
            data.enable_GPS = 1;
            LOG_I (TAG, "GPS will be enabled");
        }
        else {
            data.enable_GPS = 0;
            LOG_I (TAG, "GPS will be disabled");
        }

        if ((ctx.bagheera_config->getConfig ("ublox","enable_SBAS","true", override_val, overriden)) == "true") {
            data.enable_SBAS =  1;
            LOG_I (TAG, "SBAS will be enabled");
        }
        else {
            data.enable_SBAS = 0;
            LOG_I (TAG, "SBAS will be disabled");
        }

        if ((ctx.bagheera_config->getConfig ("ublox","enable_Galileo","false", override_val, overriden)) == "true") {
            data.enable_Galileo =  1;
            LOG_I (TAG, "Galileo will be enabled");
        }
        else {
            data.enable_Galileo = 0;
            LOG_I (TAG, "Galileo will be disabled");
        }

        if ((ctx.bagheera_config->getConfig ("ublox","enable_BeiDou","false", override_val, overriden)) == "true") {
            data.enable_BeiDou =  1;
            LOG_I (TAG, "BeiDou will be enabled");
        }
        else {
            data.enable_BeiDou = 0;
            LOG_I (TAG, "BeiDou will be disabled");
        }

        if ((ctx.bagheera_config->getConfig ("ublox","enable_GLONASS","true", override_val, overriden)) == "true") {
            data.enable_GLONASS =  1;
            LOG_I (TAG, "GLONASS will be enabled");
        }
        else {
            data.enable_GLONASS = 0;
            LOG_I (TAG, "GLONASS will be disabled");
        }

        if ((ctx.bagheera_config->getConfig ("ublox","enable_IMES","false", override_val, overriden)) == "true") {
            data.enable_IMES =  1;
            LOG_I (TAG, "IMES will be enabled");
        }
        else {
            data.enable_IMES = 0;
            LOG_I (TAG, "IMES will be disabled");
        }

        if ((ctx.bagheera_config->getConfig ("ublox","enable_QZSS","false", override_val, overriden)) == "true") {
            data.enable_QZSS =  1;
            LOG_I (TAG, "QZSS will be enabled");
        }
        else {
            data.enable_QZSS = 0;
            LOG_I (TAG, "QZSS will be disabled");
        }

    }

    ctx.ublox = Ublox::get_ublox ("UBLOX", UBLOX_PORT_NAME, UBLOX_BAUD_RATE, send_ubx_cfg_msgs, log_ubx_msgs, disable_nmea_msgs, save_ubx_cfg, data);
    if (NULL == ctx.ublox) {
        LOG_C (TAG, "Can't get Ublox object");
        return false;
    }

    if (false == ctx.ublox->register_ublox_callback ((Ublox::ublox_callback_t*)ublox_callback)) {
        LOG_C(TAG,"Cannot register ublox callback");
        return false;
    }

    if (false == ctx.ublox->register_ublox_gps_callback ((Ublox::ublox_gps_callback_t*)ublox_gps_callback)) {
        LOG_C(TAG,"Cannot register ublox gps callback");
        return false;
    }

    if (false == ctx.ublox->register_ublox_pps_callback ((Ublox::ublox_pps_callback_t*)ublox_pps_callback)) {
        LOG_C(TAG,"Cannot register ublox pps callback");
        return false;
    }

    if (false == ctx.ublox->register_ublox_constellation_callback ((Ublox::ublox_constellation_callback_t*)ublox_constellation_callback)) {
        LOG_C(TAG,"Cannot register ublox constellation callback");
        return false;
    }

    if (ctx.ublox_led_indication) {
        ctx.flash_ublox_led = false;
        pthread_t th_led_ublox;
        if (pthread_create (&th_led_ublox, NULL, ublox_led_flash_thread, NULL))
        {
            LOG_E (TAG, "Failed to create ublox_led_flash_thread.");
        }
    }

    ctx.saved_ublox_gps = def_ublox_gps;
    return true;
}
#endif

static bool init_imu() {
    ctx.imu = Imu::get_imu("ND_IMU");

    if( false == fill_rt_config_imu() ) {
        LOG_C(TAG,"false == fill_rt_config_imu(); ");
        //return false; // need not return if fill_rt_config_imu fails
    }

    if( NULL == ctx.imu ) {
        LOG_C(TAG,"Cannot open IMU");
        return false;
    }

    server.create_topic(TOPIC_APM_IMU_DATA);
    if (! DriveSimulation::drive_simulation_enabled ) {
        if( false == ctx.imu->register_imu_callback( imu_callback ) ) {
            LOG_E(TAG,"Cannot register IMU callback");
            imu_init_done = false;
            return false;
        } else {
            imu_init_done = true;
        }
    }
    else{
        if( false == ctx.imu->register_imu_callback( DriveSimulation::dummy_imu_callback ) ) {
            LOG_E(TAG,"Cannot register IMU callback");
            imu_init_done = false;
            return false;
        } else {
            imu_init_done = true;
        }
    }

    if( false == ctx.imu->register_imu_temperature_callback( imu_temperature_callback ) ) {
        LOG_E(TAG,"Cannot register callback");
        return false;
    }

    return true;
}

static bool init_audio() {
    ctx.audio = Audio::get_audio();

    if( NULL == ctx.audio ) {
        LOG_C(TAG,"Cannot instantiate audio session");
        return false;
    }

    if( false == ctx.audio->register_audio_callback( audio_callback, audio_err_callback ) ) {
        LOG_E(TAG,"Cannot register audio callback");
        return false;
    }

    return true;
}

static bool init_irled_on_off_hrs () {

    if (!cams_enabled [DEVICE_CAMERA_POSITION_BACK]) {
        LOG_I (TAG, "Inward camera is not enabled. Not initializing IRLED hrs");
        return true;
    }
    bool get_override_val = true;
    bool is_val_overridden = false;

    string ir_on_hr = "", ir_off_hr = "", ir_on_intensity = "", irled_level = "", analytics_control_str;
    string ir_on_min = "", ir_off_min = "";

    IRLED_ON_HR = IRLED_ON_HR_DEFAULT;
    IRLED_OFF_HR = IRLED_OFF_HR_DEFAULT;

    Config_parser c(BAGHEERACONFIG_INI);
    if (!c.getParseStatus ()) {
        LOG_E (TAG, "Error parsing bagheera_config");
        return false;
    }

    if ((ir_on_hr = c.getConfig ("NightMode", "irled_on_hr", "", get_override_val, is_val_overridden)) != "") {
        if (!string_to_integer (ir_on_hr, IRLED_ON_HR)) {
            IRLED_ON_HR = IRLED_ON_HR_DEFAULT;
        }
    }
    if ((ir_off_hr = c.getConfig ("NightMode", "irled_off_hr", "", get_override_val, is_val_overridden)) != "") {
        if (!string_to_integer (ir_off_hr, IRLED_OFF_HR)) {
            IRLED_OFF_HR = IRLED_OFF_HR_DEFAULT;
        }
    }

    if ((irled_level = c.getConfig ("NightMode", "irled_level",
            std::to_string(default_irled_level), get_override_val, is_val_overridden)) != "") {
        if (!string_to_integer (irled_level, ctx.irled_level)) {
            ctx.irled_level = default_irled_level;
        }
        // range check
        if((ctx.irled_level < eIrledBrightnessLevelMin) || (ctx.irled_level > eIrledBrightnessLevelMax)) {
            LOG_I(TAG, "IRLED brightness level is beyond limits(%d). Setting to default(%d)", ctx.irled_level, default_irled_level);
            ctx.irled_level = default_irled_level;
        }
    }

    LOG_I (TAG, "IRLED_ON_HR: %d IRLED_OFF_HR: %d, irled_level = %d", IRLED_ON_HR, IRLED_OFF_HR, ctx.irled_level);
    return true;
}

static bool init_photodiode()
{
    if (!cams_enabled [DEVICE_CAMERA_POSITION_BACK]) {
        LOG_I (TAG, "Inward camera is not enabled. Not initializing Photodiode");
        return true;
    }
    if (false == ctx.photodiode->register_photodiode_callback(photodiode_callback)) {
        LOG_E(TAG,"Cannot register photodiode callback");
        nd_service_obj->send_err_msg(SM_E_NDC_PHOTOSENSOR_FAIL, 0, "Cannot register photodiode callback");
        return false;
    }
    if (false == start_photodiode()) {
        LOG_E(TAG,"Cannot start Photodiode");
        nd_service_obj->send_err_msg(SM_E_NDC_PHOTOSENSOR_FAIL, 0, "Cannot start Photodiode");
        return false;
    }
    LOG_I (TAG,"photodiode initialized");

    return true;
}

static void video_encryption_config_init()
{
    Config_parser bagheera_config(BAGHEERACONFIG_INI);
    bool get_override_val = true;
    bool is_val_overridden = false;
    string video_encryption_str = bagheera_config.getConfig("camera","video_encryption", video_encryption_default, get_override_val, is_val_overridden );

    LOG_I(TAG, "video_encryption from config %s", video_encryption_str.c_str());
    if ("true" == video_encryption_str) {
        LOG_I(TAG, " video_encryption is enabled ");
        video_encryption = true;
    } else {
        video_encryption = false;
        LOG_I(TAG, " video_encryption is disabled ");
    }

    return;
}

static int get_delta_size(string file_name)
{
    if ((true == video_encryption) && (file_name.find("mp4") != file_name.npos) ) {
        return OPERATE_DELTA_SIZE;
    }
    return 0;
}

static bool init_genmeta() {

    ctx.genmeta = new Genmeta("ND_GENMETA");
    if( ctx.genmeta == NULL ) {
        LOG_E(TAG, "Cannot allocate genmeta");
        return false;
    }

    return true;
}

static bool init_config() {

    ctx.device_config = new Config_parser(DEVICE_CONFIG_INI);
    if( ctx.device_config->getParseStatus() != true) {
        LOG_E(TAG, "Cannot allocate Config");
        nd_service_obj->send_err_msg(SM_E_NDC_CONFIG_READING_FAIL, NDService::UNUSED_ERR_AUX_CODE, "parsing deviceconfig.ini is failed");
        return false;
    }
    ctx.nd_config = new Config_parser(ND_DEVICE_INI);
    if( ctx.nd_config->getParseStatus() != true) {
        LOG_E(TAG, "Cannot allocate Config2, nddevice.ini got corrupted, skip exiting");
        is_nd_config_corrupted = true;
        //return false;
    }
    ctx.bagheera_config = new Config_parser(BAGHEERACONFIG_INI);
    if( ctx.bagheera_config->getParseStatus() != true) {
        LOG_E(TAG, "Cannot allocate Config");
        nd_service_obj->send_err_msg(SM_E_NDC_CONFIG_READING_FAIL, NDService::UNUSED_ERR_AUX_CODE, "parsing bagheera_config.ini is failed");
        return false;
    }
    ctx.nd_config_analytics = new Config_parser(ND_CONFIG_ANALYTICS);
    if( ctx.nd_config_analytics->getParseStatus() != true) {
        LOG_E(TAG, "Cannot allocate nd_config_analytics");
        nd_service_obj->send_err_msg(SM_E_NDC_CONFIG_READING_FAIL, NDService::UNUSED_ERR_AUX_CODE, "parsing nd_config.ini is failed");
        return false;
    }

    return true;
}
bool init_modules(string base_path) {
    if( base_path == "" ) {
        LOG_C(TAG, "Base path is NULL");
        return false;
    }

    ctx.pipeline_enabled = false;
    ctx.base_path_cam0 = base_path;
    //Init modules
    if( init_imu() == false ) {
        LOG_C(TAG,"Error initing IMU");
        nd_service_obj->send_err_msg(SM_E_NDC_IMU_FAIL, 0, "Error initing IMU");
        // return false;
    }
    


    if( init_genmeta() == false ) {
        LOG_C(TAG,"Error initing genmeta");
        return false;
    }

#ifdef BAGHEERA2
    if (init_ublox () ==false) {
       LOG_C (TAG, "Error initing ublox");
       //return false;
    }
#elif KRAIT
    ctx.ublox_led_indication = false;
    ublox_enabled = false;
    ctx.ublox_enabled = false;
#endif
    if (ublox_enabled == false) {
        if (init_gps() == false) {
            LOG_C(TAG,"Error initing GPS");
            nd_service_obj->send_err_msg(SM_E_NDC_GPS_FAIL, 0, "Error initing GPS");
            //return false;
        }
    }

    if(audio_enable == true) {
        if( init_audio() == false ) {
            LOG_C(TAG,"Error initing Audio");
        }
    }
    //init_camera also  should happen only after
    //init_config is done.
    if( init_camera() == false ) {
        LOG_C(TAG,"Error initing camera");
        return false;
    }

    if (init_photodiode() == false) {
        LOG_C(TAG,"Error initing photodiode");
    }

    if (init_irled_on_off_hrs() == false) {
        LOG_C (TAG, "No IRLED on/off hour info in config file");
    }

    if( init_speaker() == false ) {
        LOG_C(TAG,"Error initing speaker");
    }


    return true;
}
//End of Init

//Deinit section

bool deinit_camera()
{
    bool success = true;

#ifdef BAGHEERA2
    if (false ==  MediaRecorder::release_media_recorder(ctx.media_recorder[DEVICE_CAMERA_POSITION_FRONT])) {
        success = false;
        LOG_E(TAG, "Front camera deinit failed");
    } else {
        ctx.media_recorder[DEVICE_CAMERA_POSITION_FRONT] = NULL;
        LOG_I(TAG, "Front camera deinited");
    }
    if (ctx.zmq_publisher[DEVICE_CAMERA_POSITION_FRONT]) {
        zmq_close (ctx.zmq_publisher[DEVICE_CAMERA_POSITION_FRONT]);
        ctx.zmq_publisher[DEVICE_CAMERA_POSITION_FRONT] = NULL;
    }
    if (ctx.zmq_context[DEVICE_CAMERA_POSITION_FRONT]) {
        zmq_ctx_destroy (ctx.zmq_context[DEVICE_CAMERA_POSITION_FRONT]);
        ctx.zmq_context[DEVICE_CAMERA_POSITION_FRONT] = NULL;
    }
#elif KRAIT
    for (int i = 0; i < NUM_CAMERAS; i++)
    {
        if (false ==  MediaRecorder::release_media_recorder(ctx.media_recorder[i])) {
            success = false;
            LOG_E(TAG, "Camera %d deinit failed", i);
            continue;
        }
        ctx.media_recorder[i] = NULL;
        LOG_I(TAG, "Camera %d deinited", i);
        if (ctx.zmq_publisher[i]) {
            zmq_close (ctx.zmq_publisher[i]);
            ctx.zmq_publisher[i] = NULL;
        }
        if (ctx.zmq_context[i]) {
            zmq_ctx_destroy (ctx.zmq_context[i]);
            ctx.zmq_context[i] = NULL;
        }
    }
#endif
    return success;
}

bool deinit_gps() {
    if (ctx.gps == NULL) {
        LOG_I(TAG, "GPS was not initialized. Hence skipping deinit");
        return true;
    }
    if (false == Gps::release_gps(ctx.gps)) {
        LOG_E(TAG, "GPS deinit failed");
        return false;
    }
    if (ctx.zmq_publisher_gps) {
      zmq_close (ctx.zmq_publisher_gps);
      ctx.zmq_publisher_gps = NULL;
    }
    if (ctx.zmq_context_gps) {
        zmq_ctx_destroy (ctx.zmq_context_gps);
        ctx.zmq_context_gps = NULL;
    }

    LOG_I(TAG, "GPS deinited");
    ctx.gps = NULL;
    return true;
}

#ifdef BAGHEERA2
bool deinit_ublox() {
    if (ctx.ublox) {
        if( false == Ublox::release_ublox(ctx.ublox) ) {
            LOG_E(TAG, "UBLOX deinit failed");
            return false;
        }
    }

    LOG_I(TAG, "UBLOX deinited");
    ctx.ublox = NULL;
    return true;
}
#endif

bool deinit_imu() {
    if (ctx.imu == NULL) {
        LOG_I(TAG, "IMU was not initialized. Hence skipping deinit");
        return true;
    }
    if (false == Imu::release_imu(ctx.imu)) {
        LOG_E(TAG, "IMU deinit failed");
        return false;
    }
    if (ctx.zmq_publisher_imu) {
        zmq_close (ctx.zmq_publisher_imu);
        ctx.zmq_publisher_imu = NULL;
    }
    if (ctx.zmq_context_imu) {
        zmq_ctx_destroy (ctx.zmq_context_imu);
        ctx.zmq_context_imu = NULL;
    }

    LOG_I(TAG, "IMU deinited");
    ctx.imu = NULL;
    return true;
}

bool deinit_photodiode() {

#if defined(BAGHEERA2) || defined(KRAIT)
    if (ctx.photodiode == NULL)
    {
        return false;
    }

    if( false == ctx.photodiode->disable_sensor() ) {
            LOG_E(TAG,"Cannot disable PHOTODIODE");
            return false;
    }

    if( false == ctx.photodiode->release_photodiode(ctx.photodiode) ) {
            LOG_E(TAG, "PHOTODIODE deinit failed");
            return false;
    }
    LOG_I(TAG, "PHOTODIODE deinited");
    ctx.photodiode = NULL;
#endif
    return true;
}

bool deinit_genmeta() {
    if(NULL == ctx.genmeta)
    {
        LOG_E(TAG, "ctx.genmeta == null");
        return false;
    }

    delete ctx.genmeta;
    ctx.genmeta = NULL;

    LOG_I(TAG, "genmeta deinited");
    return true;
}

static bool deInit_config() {

    bool retval1 = false;
    bool retval2 = false;
    bool retval3 = false;
    bool retval4 = false;

    if (ctx.device_config) {
        delete ctx.device_config;
        ctx.device_config = NULL;
        retval1 = true;
    }
    else {
        LOG_E(TAG, "ctx.device_config == null");
    }

    if (ctx.nd_config) {
        delete ctx.nd_config;
        ctx.nd_config = NULL;
        retval2 = true;
    }
    else {
        LOG_E(TAG, "ctx.nd_config == null");
    }

    if (ctx.bagheera_config) {
        delete ctx.bagheera_config;
        ctx.bagheera_config = NULL;
        retval3 = true;
    }
    else {
        LOG_E(TAG, "ctx.bagheera_config == null");
    }
    if (ctx.nd_config_analytics) {
        delete ctx.nd_config_analytics;
        ctx.nd_config_analytics = NULL;
        retval4 = true;
    }
    else {
        LOG_E(TAG, "ctx.nd_config_analytics == null");
    }

    return retval1 && retval2 && retval3 && retval4;
}

static bool deinit_modules() {
    ctx.pipeline_enabled = false;
    ctx.base_path_cam0 = "";

    //Init modules
    if( deinit_camera() == false ) {
        LOG_E(TAG,"Error deiniting camera");
        return false;
    }

    if( deinit_gps() == false ) {
        LOG_E(TAG,"Error deniting GPS");
        return false;
    }

#ifdef BAGHEERA2
    if( deinit_ublox() == false ) {
        LOG_E(TAG,"Error deniting UBLOX");
        return false;
    }
#endif

    if( deinit_imu() == false ) {
        LOG_E(TAG,"Error deiniting IMU");
        return false;
    }

    if( deinit_photodiode() == false ) {
        LOG_E(TAG,"Error deiniting PHOTODIODE");
    }

    if( deinit_genmeta() == false ) {
        LOG_E(TAG,"Error deiniting genmeta");
        return false;
    }

    if( deInit_config() ==false ) {
        LOG_E(TAG,"Error deiniting config");
        return false;
    }


    return true;
}

//End of Deinit section

//Configure section
static bool configure_modules() {
    if(ublox_enabled == false) {
        if( ctx.gps && (false == ctx.gps->configure(Gps::config_refresh, "1")) ) {
            LOG_C(TAG,"Cannot configure GPS refresh rate");
            nd_service_obj->send_err_msg(SM_E_NDC_GPS_FAIL, 0, "Cannot configure GPS refresh rate");
            return false;
	}
    }

    if(ctx.imu) {
        if( false == ctx.imu->configure(Imu::config_refresh, Imu::config_refresh_med) ) {
            LOG_E(TAG,"Cannot configure IMU refresh rate");
            nd_service_obj->send_err_msg(SM_E_NDC_IMU_FAIL, 0, "Cannot configure IMU refresh rate");
            //return false;
        }
    }

    return true;
}
//

static bool start_camera(int cam_num) {

    if( false == ctx.media_recorder[cam_num]->start_record_session() ) {
        LOG_C(TAG,"Cannot enable Media Recorder for cam num %d", cam_num);
        return false;
    }

    LOG_I(TAG, "Camera started");

    return true;
}

static bool start_kinesis( req_livestreaming_data_t *start_kinesis_msg )
{
    if( false == ctx.media_recorder[start_kinesis_msg->camera]->start_kinesis_session( start_kinesis_msg ) ) {
        LOG_C(TAG,"Cannot enable kinesis Media Recorder for cam num %d", start_kinesis_msg->camera);
        return false;
    }

    LOG_I(TAG, "kinesis started for camera %d", start_kinesis_msg->camera);
    if (ctx.live_stream_audio_notification && !start_kinesis_msg->dual_streaming) {
        send_live_streaming_status_audio_play(start_kinesis_msg->camera, START);
    }
    return true;
}

static bool stop_kinesis( req_livestreaming_data_t *stop_kinesis_msg )
{
    dual_streaming_audio_start_played.store(false, std::memory_order_relaxed);
    dual_streaming_audio_end_played.store(false, std::memory_order_relaxed);

    if( false == ctx.media_recorder[stop_kinesis_msg->camera]->stop_kinesis_session( stop_kinesis_msg ) ) {
        LOG_C(TAG, "Cannot disable kinesis Media Recorder for cam num %d", stop_kinesis_msg->camera);
        return false;
    }
    LOG_I(TAG, "kinesis stopped for camera %d", stop_kinesis_msg->camera);
    if (ctx.live_stream_audio_notification && !stop_kinesis_msg->dual_streaming) {
        send_live_streaming_status_audio_play(stop_kinesis_msg->camera, END);
    }
    return true;
}

static bool stop_camera(int cam_num) {

    if( false == ctx.media_recorder[cam_num]->stop_record_session() ) {
        LOG_C(TAG,"Cannot disable Media Recorder Front");
        return false;
    }
    LOG_I(TAG, "Camera stopped");

    disable_all_sensors();

    return true;
}

//This function will check for different privacy conditions and finally return the video privacy for that session
static void check_video_privacy(int cam_num, bool &record_privacy, bool &upload_privacy)
{
    bool need_to_copy = ctx.privacy_params.save_user_alert_video && session_contains_usr_alert;
    bool lpw_is_active = file_is_present(lpw_no_record_persistent_file);

    record_privacy = false;
    upload_privacy = false;

    int temp_cam_num = cam_num;
    if (cam_num == DEVICE_CAMERA_POSITION_DMS) {
        temp_cam_num = DEVICE_CAMERA_POSITION_BACK; //This is to apply same privacy to DMS as Inward
    }

    if (device_mode_global_partial.privacy_status_geofence != PRIVACY_OFF) {
        // Priority: Geofence > User Alert > LPW
        // Case 1: Geofence ON (regardless of offduty status)
        if (device_mode_global_partial.privacy_status_geofence == PRIVACY_ON) {
            LOG_C(TAG, "Geofence ON (offduty=%d, enhanced=%d, regular=%d) - Will DELETE CAM%d video",
                device_mode_global_partial.privacy_status_offduty, 
                ctx.privacy_params.enhanced_privacy,
                device_mode_global_partial.privacy_status, cam_num);
            record_privacy = true;
            upload_privacy = true;
        }
        // Case 2: Geofence MIXED - handle all combinations within this block
        else { // device_mode_global_partial.privacy_status_geofence == PRIVACY_MIXED
            // Sub-case 2a: Geofence MIXED + Offduty ON
            if (device_mode_global_partial.privacy_status_offduty == PRIVACY_ON) {
                LOG_I(TAG, "Geofence MIXED + Offduty ON: Will delete CAM%d video", cam_num);
                record_privacy = true;
                upload_privacy = true;
            }
            // Sub-case 2b: Geofence MIXED + Offduty MIXED or OFF - check enhanced/regular
            else {
                if (ctx.privacy_params.enhanced_privacy) {
                    // Geofence MIXED + Enhanced privacy
                    // Delete inward/DMS/ext cameras always
                    if ((temp_cam_num == DEVICE_CAMERA_POSITION_BACK) || 
                        (temp_cam_num >= EXT_CAMERA_POSITION_CH1 && temp_cam_num <= EXT_CAMERA_POSITION_CH4)) {
                        LOG_I(TAG, "Geofence MIXED + Enhanced Privacy: Will delete CAM%d video", cam_num);
                        record_privacy = true;
                        upload_privacy = true;
                    }
                    // Outward cameras: will be edited (not setting record_privacy/upload_privacy)
                } else {
                    // Geofence MIXED + Regular privacy
                    if (device_mode_global_partial.privacy_status == PRIVACY_ON) {
                        // Geofence MIXED + Regular Privacy ON
                        if ((cam_num >= EXT_CAMERA_POSITION_CH1 && cam_num <= EXT_CAMERA_POSITION_CH4) ||
                            (temp_cam_num < DEVICE_CAMERA_POSITION_MAX && ctx.privacy_params.cam_privacy[temp_cam_num])) {
                            LOG_C(TAG, "Geofence MIXED + Regular Privacy ON (offduty=%d) - Will DELETE CAM%d video",
                                device_mode_global_partial.privacy_status_offduty, cam_num);
                            record_privacy = true;
                            upload_privacy = true;
                        } else {
                            LOG_I(TAG, "Geofence MIXED + Regular Privacy ON - CAM%d will be EDITED", cam_num);
                        }
                        // Other cameras: will be edited
                    } else {
                        // Geofence MIXED + Regular privacy OFF or MIXED
                        if (cam_num >= EXT_CAMERA_POSITION_CH1 && cam_num <= EXT_CAMERA_POSITION_CH4) {
                            LOG_C(TAG, "Geofence MIXED + Regular Privacy %d (offduty=%d) - Will DELETE EXT CAM%d video",
                                device_mode_global_partial.privacy_status, device_mode_global_partial.privacy_status_offduty, cam_num);
                            record_privacy = true;
                            upload_privacy = true;
                        }                        // Other cameras: will be edited
                    }
                }
            }
        }
    } else if (need_to_copy) {
        LOG_I(TAG, "Will copy CAM%d video because of user alert", cam_num);
    } else if (lpw_is_active) {
        LOG_I(TAG, "LPW no record: Will delete CAM%d video", cam_num);
        record_privacy = true;
        upload_privacy = true;
    } else if (device_mode_global_partial.privacy_status_offduty != PRIVACY_OFF) {
        // In between a session, if off-duty is enabled, then we need to check the privacy params and decide whether to record
        // or upload the video. Privacy mixed case not considered for EXT CAM, as editing not handled for EXT CAM
        if (device_mode_global_partial.privacy_status_offduty == PRIVACY_ON) { // Entire session in Off-duty
            LOG_I(TAG, "Off-duty privacy: Will delete CAM%d video", cam_num);
            record_privacy = true;
            upload_privacy = true;
        } else {
            if (ctx.privacy_params.enhanced_privacy) { // Offduty + Enhanced privacy case
                if ((temp_cam_num == DEVICE_CAMERA_POSITION_BACK) || (temp_cam_num >= EXT_CAMERA_POSITION_CH1 && temp_cam_num <= EXT_CAMERA_POSITION_CH4)) {
                    LOG_I(TAG, "In Off duty + Enhanced Privacy case: Will delete CAM%d video", cam_num);
                    record_privacy = true;
                    upload_privacy = true;
                }
            } else {
                if (device_mode_global_partial.privacy_status == PRIVACY_ON) { // Offduty + Regular privacy case
                    if ((cam_num >= EXT_CAMERA_POSITION_CH1 && cam_num <= EXT_CAMERA_POSITION_CH4) ||
                            (temp_cam_num < DEVICE_CAMERA_POSITION_MAX && ctx.privacy_params.cam_privacy[temp_cam_num])) {
                        LOG_I(TAG, "Offduty + Regular Privacy: Will delete CAM%d video", cam_num);
                        record_privacy = true;
                        upload_privacy = true;
                    }
                } else { // Mixed privacy case
                    if (cam_num >= EXT_CAMERA_POSITION_CH1 && cam_num <= EXT_CAMERA_POSITION_CH4) {
                        LOG_I(TAG, "Mixed privacy for EXT_CAM: Will delete CAM%d video", cam_num);
                        record_privacy = true;
                        upload_privacy = true;
                    }
                }
            }
        }
    } else if (ctx.privacy_params.enhanced_privacy) {
        if ((cam_num == DEVICE_CAMERA_POSITION_BACK) || (cam_num == DEVICE_CAMERA_POSITION_DMS)) {
            LOG_I(TAG, "Enhanced Privacy: Will delete CAM%d video", cam_num);
            record_privacy = true;
            upload_privacy = true;
        }
    } else if (device_mode_global_partial.privacy_status != PRIVACY_OFF) { // Regular privacy case
        if (device_mode_global_partial.privacy_status == PRIVACY_ON) {
            if (cam_num >= EXT_CAMERA_POSITION_CH1 && cam_num <= EXT_CAMERA_POSITION_CH4) {
                if (ctx.privacy_params.ext_cam_privacy) {
                    LOG_I(TAG, "Regular Privacy: Will delete CAM%d video", cam_num);
                    record_privacy = true;
                }
                if (ctx.privacy_params.upload_video_ext_cam == false) {
                    LOG_I(TAG, "Regular Privacy: Will not upload CAM%d video", cam_num);
                    upload_privacy = true;
                }
            } else if (temp_cam_num < DEVICE_CAMERA_POSITION_MAX) {
                if (ctx.privacy_params.cam_privacy[temp_cam_num]) {
                    LOG_I(TAG, "Regular Privacy: Will delete CAM%d video", cam_num);
                    record_privacy = true;
                }
                if (ctx.privacy_params.upload_video[temp_cam_num] == false) {
                    LOG_I(TAG, "Regular Privacy: Will not upload CAM%d video", cam_num);
                    upload_privacy = true;
                }
            }
        } else if (device_mode_global_partial.privacy_status == PRIVACY_MIXED) {
            if (cam_num >= EXT_CAMERA_POSITION_CH1 && cam_num <= EXT_CAMERA_POSITION_CH4) {
                if (ctx.privacy_params.ext_cam_privacy) {
                    LOG_I(TAG, "Regular Privacy: Will delete CAM%d video", cam_num);
                    record_privacy = true;
                }
                if (ctx.privacy_params.upload_video_ext_cam == false) {
                    LOG_I(TAG, "Regular Privacy: Will not upload CAM%d video", cam_num);
                    upload_privacy = true;
                }
            }
        }
    }
    return;
}

//This function will check for different privacy conditions and finally return the video privacy for the partial session
static void check_partial_session_video_privacy(int cam_num, bool &record_privacy, bool &upload_privacy)
{
    bool need_to_copy = device_mode_global_partial.partial_privacy_params.save_user_alert_video &&
                        device_mode_global_partial.partial_privacy_params.has_user_alert;
    bool lpw_is_active = file_is_present(lpw_no_record_persistent_file);

    record_privacy = false;
    upload_privacy = false;

    int temp_cam_num = cam_num;
    if (cam_num == DEVICE_CAMERA_POSITION_DMS) {
        temp_cam_num = DEVICE_CAMERA_POSITION_BACK; //This is to apply same privacy to DMS as Inward
    }

    LOG_I(TAG, "check_partial_session_video_privacy: cam_num=%d, privacy_status=%d, privacy_status_offduty=%d, privacy_status_geofence=%d",
          cam_num, device_mode_global_partial.privacy_status, device_mode_global_partial.privacy_status_offduty, device_mode_global_partial.privacy_status_geofence);

    // Priority: Geofence > User Alert > LPW > Offduty > Enhanced > Regular

    if(device_mode_global_partial.privacy_status_geofence != PRIVACY_OFF) {
        // Case 1: Geofence ON (regardless of offduty status)
        if (device_mode_global_partial.privacy_status_geofence == PRIVACY_ON) {
            LOG_I(TAG, "Geofence ON: Will delete CAM%d video", cam_num);
            record_privacy = true;
            upload_privacy = true;
        }
        // Case 2: Geofence MIXED - handle all combinations within this block
        else { // device_mode_global_partial.privacy_status_geofence == PRIVACY_MIXED
            // Sub-case 2a: Geofence MIXED + Offduty ON
            if (device_mode_global_partial.privacy_status_offduty == PRIVACY_ON) {
                LOG_I(TAG, "Geofence MIXED + Offduty ON: Will delete CAM%d video", cam_num);
                record_privacy = true;
                upload_privacy = true;
            }
            // Sub-case 2b: Geofence MIXED + Offduty MIXED or OFF - check enhanced/regular
            else {
                if (device_mode_global_partial.partial_privacy_params.enhanced_privacy) {
                    // Geofence MIXED + Enhanced privacy
                    // Delete inward/DMS/ext cameras always
                    if ((temp_cam_num == DEVICE_CAMERA_POSITION_BACK) || 
                        (temp_cam_num >= EXT_CAMERA_POSITION_CH1 && temp_cam_num <= EXT_CAMERA_POSITION_CH4)) {
                        LOG_I(TAG, "Geofence MIXED + Enhanced Privacy: Will delete CAM%d video", cam_num);
                        record_privacy = true;
                        upload_privacy = true;
                    }
                    // Outward cameras: will be edited (not setting record_privacy/upload_privacy)
                } else {
                    // Geofence MIXED + Regular privacy
                    if (device_mode_global_partial.privacy_status == PRIVACY_ON) {
                        // Geofence MIXED + Regular Privacy ON
                        if ((cam_num >= EXT_CAMERA_POSITION_CH1 && cam_num <= EXT_CAMERA_POSITION_CH4) ||
                            (temp_cam_num < DEVICE_CAMERA_POSITION_MAX && 
                             device_mode_global_partial.partial_privacy_params.cam_privacy[temp_cam_num])) {
                            LOG_I(TAG, "Geofence MIXED + Regular Privacy ON: Will delete CAM%d video", cam_num);
                            record_privacy = true;
                            upload_privacy = true;
                        }
                        // Other cameras: will be edited
                    } else {
                        // Geofence MIXED + Regular privacy OFF or MIXED
                        if (cam_num >= EXT_CAMERA_POSITION_CH1 && cam_num <= EXT_CAMERA_POSITION_CH4) {
                            LOG_I(TAG, "Geofence MIXED: Will delete EXT CAM%d video", cam_num);
                            record_privacy = true;
                            upload_privacy = true;
                        }
                        // Other cameras: will be edited
                    }
                }
            }
        }
    }
    else if (need_to_copy) {
        LOG_I(TAG, "Will copy CAM%d video because of user alert", cam_num);
        // No privacy set - allow recording and uploading
    }
    //  LPW no record
    else if (lpw_is_active) {
        LOG_I(TAG, "LPW no record: Will delete CAM%d video", cam_num);
        record_privacy = true;
        upload_privacy = true;
    }
    // Offduty privacy (no geofence)
    else if (device_mode_global_partial.privacy_status_offduty != PRIVACY_OFF) {
        // In between a session, if off-duty is enabled, then we need to check the privacy params and decide whether to record
        // or upload the video. Privacy mixed case not considered for EXT CAM, as editing not handled for EXT CAM
        if (device_mode_global_partial.privacy_status_offduty == PRIVACY_ON) { // Entire session in Off-duty
            LOG_I(TAG, "Off-duty privacy: Will delete CAM%d video", cam_num);
            record_privacy = true;
            upload_privacy = true;
        } else {
            if (device_mode_global_partial.partial_privacy_params.enhanced_privacy) { // Offduty + Enhanced privacy case
                if ((temp_cam_num == DEVICE_CAMERA_POSITION_BACK) || (temp_cam_num >= EXT_CAMERA_POSITION_CH1 && temp_cam_num <= EXT_CAMERA_POSITION_CH4)) {
                    LOG_I(TAG, "In Off duty + Enhanced Privacy case: Will delete CAM%d video", cam_num);
                    record_privacy = true;
                    upload_privacy = true;
                }
            } else {
                if (device_mode_global_partial.privacy_status == PRIVACY_ON) { // Offduty + Regular privacy case
                    if ((cam_num >= EXT_CAMERA_POSITION_CH1 && cam_num <= EXT_CAMERA_POSITION_CH4) ||
                            (temp_cam_num < DEVICE_CAMERA_POSITION_MAX && device_mode_global_partial.partial_privacy_params.cam_privacy[temp_cam_num])) {
                        LOG_I(TAG, "Offduty + Regular Privacy: Will delete CAM%d video", cam_num);
                        record_privacy = true;
                        upload_privacy = true;
                    }
                } else { // Mixed privacy case
                    if (cam_num >= EXT_CAMERA_POSITION_CH1 && cam_num <= EXT_CAMERA_POSITION_CH4) {
                        LOG_I(TAG, "Mixed privacy for EXT_CAM: Will delete CAM%d video", cam_num);
                        record_privacy = true;
                        upload_privacy = true;
                    }
                }
            }
        }
    } else if (device_mode_global_partial.partial_privacy_params.enhanced_privacy) {
        if ((cam_num == DEVICE_CAMERA_POSITION_BACK) || (cam_num == DEVICE_CAMERA_POSITION_DMS)) {
            LOG_I(TAG, "Enhanced Privacy: Will delete CAM%d video", cam_num);
            record_privacy = true;
            upload_privacy = true;
        }
    } else if (device_mode_global_partial.privacy_status != PRIVACY_OFF) { // Regular privacy case
        if (device_mode_global_partial.privacy_status == PRIVACY_ON) {
            if (cam_num >= EXT_CAMERA_POSITION_CH1 && cam_num <= EXT_CAMERA_POSITION_CH4) {
                if (device_mode_global_partial.partial_privacy_params.ext_cam_privacy) {
                    LOG_I(TAG, "Regular Privacy: Will delete CAM%d video", cam_num);
                    record_privacy = true;
                }
                if (device_mode_global_partial.partial_privacy_params.upload_video_ext_cam == false) {
                    LOG_I(TAG, "Regular Privacy: Will not upload CAM%d video", cam_num);
                    upload_privacy = true;
                }
            } else if (temp_cam_num < DEVICE_CAMERA_POSITION_MAX) {
                if (device_mode_global_partial.partial_privacy_params.cam_privacy[temp_cam_num]) {
                    LOG_I(TAG, "Regular Privacy: Will delete CAM%d video", cam_num);
                    record_privacy = true;
                }
                if (device_mode_global_partial.partial_privacy_params.upload_video[temp_cam_num] == false) {
                    LOG_I(TAG, "Regular Privacy: Will not upload CAM%d video", cam_num);
                    upload_privacy = true;
                }
            }
        } else if (device_mode_global_partial.privacy_status == PRIVACY_MIXED) {
            if (cam_num >= EXT_CAMERA_POSITION_CH1 && cam_num <= EXT_CAMERA_POSITION_CH4) {
                if (device_mode_global_partial.partial_privacy_params.ext_cam_privacy) {
                    LOG_I(TAG, "Regular Privacy: Will delete CAM%d video", cam_num);
                    record_privacy = true;
                }
                if (device_mode_global_partial.partial_privacy_params.upload_video_ext_cam == false) {
                    LOG_I(TAG, "Regular Privacy: Will not upload CAM%d video", cam_num);
                    upload_privacy = true;
                }
            }
        }
    }

    LOG_I(TAG, "check_partial_session_video_privacy: cam_num=%d, final decision: record_privacy=%d, upload_privacy=%d", cam_num, record_privacy, upload_privacy);
    return;
}

//This function will check for different privacy conditions and finally return the audio privacy for that session
static void check_audio_privacy(bool &record_privacy)
{
    bool need_to_copy = session_contains_usr_alert && ctx.privacy_params.save_user_alert_video;
    bool lpw_is_active = file_is_present(lpw_no_record_persistent_file);

    record_privacy = false;
    
    // Priority: Geofence > User Alert > LPW > Offduty > Enhanced > Regular
    if(device_mode_global_partial.privacy_status_geofence != PRIVACY_OFF) {
        LOG_C(TAG, "Audio DELETED - geofence=%d (offduty=%d, enhanced=%d, regular=%d)",
            device_mode_global_partial.privacy_status_geofence,
            device_mode_global_partial.privacy_status_offduty,
            ctx.privacy_params.enhanced_privacy,
            device_mode_global_partial.privacy_status);
        record_privacy = true;
    } else if (need_to_copy) {
        LOG_I(TAG, "Will copy audio because of user alert");
    } else if (lpw_is_active) {
        LOG_I(TAG, "LPW no record: Will delete audio");
        record_privacy = true;
    } else if (device_mode_global_partial.privacy_status_offduty != PRIVACY_OFF) {
        LOG_I(TAG, "Off-duty: Will delete audio privacy_status_offduty = %d", device_mode_global_partial.privacy_status_offduty);
        record_privacy = true;
    } else if (ctx.privacy_params.enhanced_privacy) {
        if (ctx.privacy_params.driveri_audio_privacy) {
            LOG_I(TAG, "Enhanced Privacy: Will delete audio");
            record_privacy = true;
        }
    } else if (ctx.privacy_params.driveri_audio_privacy && device_mode_global_partial.privacy_status != PRIVACY_OFF) {
        LOG_I(TAG, "Regular Privacy: Will delete audio");
        record_privacy = true;
    }
    return;
}

//This function will check for different privacy conditions and finally return the audio privacy for the partial session
static void check_partial_session_audio_privacy(bool &record_privacy)
{
    bool need_to_copy = device_mode_global_partial.partial_privacy_params.save_user_alert_video &&
                        device_mode_global_partial.partial_privacy_params.has_user_alert;
    bool lpw_is_active = file_is_present(lpw_no_record_persistent_file);

    record_privacy = false;
    
    // Priority: Geofence > User Alert > LPW > Offduty > Enhanced > Regular
    if(device_mode_global_partial.privacy_status_geofence != PRIVACY_OFF) {
        LOG_C(TAG, "Partial session audio DELETED - geofence=%d (offduty=%d, enhanced=%d, regular=%d)",
            device_mode_global_partial.privacy_status_geofence,
            device_mode_global_partial.privacy_status_offduty,
            device_mode_global_partial.partial_privacy_params.enhanced_privacy,
            device_mode_global_partial.privacy_status);
        record_privacy = true;
    } else if (need_to_copy) {
        LOG_I(TAG, "Will copy audio because of user alert");
    } else if (lpw_is_active) {
        LOG_I(TAG, "LPW no record: Will delete audio");
        record_privacy = true;
    } else if (device_mode_global_partial.privacy_status_offduty != PRIVACY_OFF) {
        LOG_I(TAG, "Off-duty: Will delete audio privacy_status_offduty = %d", device_mode_global_partial.privacy_status_offduty);
        record_privacy = true;
    } else if (device_mode_global_partial.partial_privacy_params.enhanced_privacy) {
        if (device_mode_global_partial.partial_privacy_params.driveri_audio_privacy) {
            LOG_I(TAG, "Enhanced Privacy: Will delete audio");
            record_privacy = true;
        }
    } else if (device_mode_global_partial.partial_privacy_params.driveri_audio_privacy &&
               device_mode_global_partial.privacy_status != PRIVACY_OFF) {
        LOG_I(TAG, "Regular Privacy: Will delete audio");
        record_privacy = true;
    }
    return;
}

int GetFileSize(std::string filename)
{
    struct stat stat_buf;
    int rc = stat(filename.c_str(), &stat_buf);
    return rc == 0 ? stat_buf.st_size : -1;
}

bool post_circular_buffer_add_file_db(string file_name, int file_type, int cam_type)
{
    LOG_I(TAG,"post_circular_buffer_add_file_db filename %s", file_name.c_str());

    circular_buffer_add_file_db_msg_t addfile_msg;
    if (ctx.rtcValid_string != "0") {
        addfile_msg.file_info.time = get_system_time();
    } else {
        addfile_msg.file_info.time = 0;
        LOG_D(TAG, "ctx.rtcValid_string  %s, addfile_msg.file_info.time: %d", ctx.rtcValid_string.c_str(), addfile_msg.file_info.time);
    }

    nd_strncpy(addfile_msg.file_info.base_file_name, file_name.c_str(), sizeof(addfile_msg.file_info.base_file_name));

    string fullfile_name = CIRCULAR_BUFFER_PATH + "/" + file_name;
    addfile_msg.file_info.file_size       = GetFileSize(fullfile_name);

    LOG_I(TAG, "GetFileSize: %d", addfile_msg.file_info.file_size);

    addfile_msg.file_info.file_type      = (circular_buffer_filetype_t)file_type;
    addfile_msg.file_info.camtype        = (circular_buffer_camtype_t)cam_type;
    addfile_msg.file_info.duration       = 60000;

    // Need to make sure file has valid session format
    if (is_video_audio_file_name_valid(file_name) == false) {
        LOG_E(TAG, "Invalid file name: %s, skip posting to CB DB", file_name.c_str());
        return false;
    }

    int pos_trip = file_name.find("_trip");
    int pos_y = file_name.find("_y");
    string sessionName = file_name.substr(pos_trip, pos_y - pos_trip + 2);

    // Block to define scope of sessionMapCountMutex
    {
        std::lock_guard<std::mutex> lock(sessionMapCountMutex);
        std::unordered_map<std::string, int64_t>::const_iterator gotSessionName = sessionMapCount.find(sessionName);
        if (gotSessionName == sessionMapCount.end()) {
            LOG_I(TAG, "%s is not found in map", sessionName.c_str() );
            string_to_int64( ctx.sessionCount_string, addfile_msg.file_info.sessionCount);
        } else {
            addfile_msg.file_info.sessionCount = gotSessionName->second;
        }
    }
    string_to_int64( ctx.udid_string, addfile_msg.file_info.udid);
    LOG_I(TAG, "post ctx.udid : %lld, ctx.sessionCount : %lld", addfile_msg.file_info.udid, addfile_msg.file_info.sessionCount);

    if ((cam_type == DEVICE_CAMERA_POSITION_FRONT || cam_type == DEVICE_CAMERA_POSITION_BACK || cam_type == DEVICE_CAMERA_POSITION_DMS) &&
        (file_name.find(".aac") == string::npos)) {
    	if (file_name.find(ld_extn.c_str(), 0) != std::string::npos) {
    	    addfile_msg.file_info.tc_status = CIRCULAR_BUFFER_TC_STATUS_TRANSCODED;
    	} else {
    	    addfile_msg.file_info.tc_status = CIRCULAR_BUFFER_TC_STATUS_WAITING;
    	}
    } else {
        addfile_msg.file_info.tc_status      = CIRCULAR_BUFFER_TC_STATUS_TRANSCODED;
    }

    /* To handle Audio files */
    if (file_name.find(".aac") != string::npos) {
        bool record_privacy = false;

        /* To handle partial session */
        if (file_name.find(partial_session_name) != std::string::npos) {
            check_partial_session_audio_privacy(record_privacy);
        }
        /* To handle full session */
        else {
            check_audio_privacy(record_privacy);
        }

        //if privacy is true, recording should be disabled and vice versa
        if (record_privacy)
            addfile_msg.file_info.rec_vid_enabled = 0;
        else
            addfile_msg.file_info.rec_vid_enabled = 1;

        /* If record privacy is enabled for audio files, we disable upload by default */
        addfile_msg.file_info.upl_vid_enabled = addfile_msg.file_info.rec_vid_enabled;
    }
    /* To handle Video files */
    else {
        bool record_privacy = false;
        bool upload_privacy = false;

        /* To handle partial session */
        if (file_name.find(partial_session_name) != std::string::npos) {
            check_partial_session_video_privacy(cam_type, record_privacy, upload_privacy);
        }
        /* To handle full session */
        else {
            check_video_privacy(cam_type, record_privacy, upload_privacy);
        }

        //if privacy is true, recording should be disabled and vice versa
        if (record_privacy)
            addfile_msg.file_info.rec_vid_enabled = 0;
        else
            addfile_msg.file_info.rec_vid_enabled = 1;

        if (upload_privacy)
            addfile_msg.file_info.upl_vid_enabled = 0;
        else
            addfile_msg.file_info.upl_vid_enabled = 1;
    }

    if ((addfile_msg.file_info.rec_vid_enabled) && (addfile_msg.file_info.file_size == -1)) {
        LOG_C(TAG, "unable to find the file/GetFileSize returned -1");
        return false;
    }

    LOG_I(TAG, "rec_vid_enabled: %d, upl_vid_enabled: %d", addfile_msg.file_info.rec_vid_enabled, addfile_msg.file_info.upl_vid_enabled);
    if(true == use_extended_attributes){
        if(false == write_addfile_metadata_to_xattrs(addfile_msg.file_info)){
            LOG_C(TAG, "Failed to write extended attributes for file %s", file_name.c_str());
        }
    }

    // send add file db msg to CB
    send_msg((generic_msg_t *)&addfile_msg, REQ_CIRCULAR_BUFFER_ADD_FILE_DB,
              sizeof(circular_buffer_add_file_db_msg_t), get_msgq_name(), ctx.circular_buffer_q_name, 0);

    return true;
}

bool post_uploader_add_ea_file_db(string file_name, int cam_type, ea_image_privacy_type privacy_type)
{
    LOG_I(TAG, "post_uploader_add_ea_file_db filename %s", file_name.c_str());

    uploader_add_ea_file_db_msg_t addfile_msg;
    string sessionName;
    bool session_name_found = get_session_name_from_string(file_name, sessionName);
    if (session_name_found) {
        nd_strncpy(addfile_msg.base_file_name, file_name.c_str(), sizeof(addfile_msg.base_file_name));
        addfile_msg.session_count = sessionCount_from_file(file_name);
        addfile_msg.udid = udid_from_file(file_name);

        if (privacy_type == NO_PRIVACY) {
            string fullfile_name = CIRCULAR_BUFFER_PATH_EA + "/" + file_name;
            addfile_msg.file_size_bytes = GetFileSize(fullfile_name);
        } else {
            addfile_msg.file_size_bytes = 0;
        }
    } else {
        LOG_E(TAG, "Invalid filename: %s", file_name.c_str());

        nd_strncpy(addfile_msg.base_file_name, "", sizeof(addfile_msg.base_file_name));
        addfile_msg.session_count = -1;
        addfile_msg.udid = -1;
        addfile_msg.file_size_bytes = 0;
    }
    addfile_msg.privacy_type = privacy_type;
    addfile_msg.cam_type = (circular_buffer_camtype_t)cam_type;

    LOG_I(TAG, "ea_image_fname: %s, FileSize: %d, udid: %lld, sessionCount: %lld, privacy_type: %d", file_name.c_str(), addfile_msg.file_size_bytes, addfile_msg.udid, addfile_msg.session_count, addfile_msg.privacy_type);

    // send message to CB to add EA image file to images DB
    send_msg((generic_msg_t *)&addfile_msg, REQ_UPLOAD_ADD_EA_FILE_DB,
            sizeof(uploader_add_ea_file_db_msg_t), get_msgq_name(), Q_NAME_UPLOADER, 0);

    return true;
}

void mediarecorder_firstframe_callback (void *app)
{
    int cam_num = *((int*)(&app));
    ctx.firstframe_time[cam_num] = get_system_time();
    LOG_I(TAG, "first frame for cam num %d registered@ %lld", cam_num, ctx.firstframe_time[cam_num]);
    ctx.gps_start_time = ctx.saved_gps.timestamp;

    return;
}

void frame_write_cb(uint64_t frame_number, uint64_t raw_time_us, uint64_t epoch_time_us, void *arg)
{
    //LOG_I(TAG, "Inside %s", __func__);
    FrameInfo::frameinfo_t frame_info;
    frame_info.raw_time_micro = raw_time_us;
    frame_info.epoch_time_micro = epoch_time_us;
    frame_info.frame_number = frame_number;
    ctx.meta_buff[ctx.session_flipflop].push(frame_info);
}

#ifdef PLAY_BEEP
static void play_beep(int start_or_end)
{
    int val = -1;

    fstream status_file (boot_status_file.c_str());
    if (status_file)
    {
        status_file >> val;
        // only beep if 0 at start of session and 1 at end of session
        if ((start_or_end == 0 && val == 0) || (start_or_end == 1 && val == 1))
        {
            string aplay_audio_device = "";
#ifdef BAGHEERA
            aplay_audio_device = " -D hw:1,3 ";
#endif
            string beep_cmd = "aplay " + aplay_audio_device + beep_file;
            system (beep_cmd.c_str());
        }
        else
        {
            LOG_I (TAG, "Not playing beep because status file contains %d at %s",val,
                                ((start_or_end==0)?"start of session":"end of session"));
        }
        val++;
        status_file.seekp(0, ios::beg);
        status_file << val << endl;
    }
    else
    {
        LOG_E (TAG, "No boot_status file found. Not playing beep");
    }
    status_file.close();
}
#endif

void send_msg_ext_cam_partial_files(string folder_name, string filename, bool need_to_copy) {
    vector<string> vec = split_by_delim(filename, "_");
    int64_t video_start_time;
    if(string_to_int64(vec[6], video_start_time) == false) {
        LOG_E(TAG, "failed to get timestamp from filename");
        return;
    }

    stream_recorded_file_msg_t record_msg;
    for(int i = 0; i < NUM_EXT_CAMERAS; i++) {
        if (ctx.ext_cam_enabled[i] == false) {
            continue;
        }
        filename = filename.replace(0, 1, to_string(i + DEVICE_CAMERA_POSITION_MAX));
        string file = folder_name + "/" + filename;
        memset(&record_msg, 0, sizeof(&record_msg));
        int pos_trip = filename.find("_trip");
        int pos_y = filename.find("_y");
        string sessionName = filename.substr(pos_trip, pos_y - pos_trip + 2);
        // Block to define scope of sessionMapCountMutex
        {
            std::lock_guard<std::mutex> lock(sessionMapCountMutex);
            std::unordered_map<std::string, int64_t>::const_iterator gotSessionName = sessionMapCount.find(sessionName);
            if (gotSessionName == sessionMapCount.end()) {
                LOG_I(TAG, "ext_cam partial file: %s is not found in map", sessionName.c_str() );
                string_to_int64( ctx.sessionCount_string, record_msg.sessionCount);
            }
            else {
                record_msg.sessionCount = gotSessionName->second;
            }
        }
        LOG_I(TAG, "filename: %s  record_msg.sessionCount: %d", filename.c_str() , record_msg.sessionCount );
        strncpy(record_msg.filename, file.c_str(), sizeof(record_msg.filename));
        record_msg.filename_len = sizeof(record_msg.filename);
        record_msg.start_time = video_start_time;
        record_msg.end_time = video_start_time + ONE_MINUTE_DURATION_MS;
        record_msg.cam_num = i + DEVICE_CAMERA_POSITION_MAX;
        record_msg.framerate = ctx.ext_cam_framerate[i];
        record_msg.mdvr_ch_num = i + 1;
        record_msg.audio_enable = ctx.ext_cam_audio_enable[i];
        string_to_int64( ctx.udid_string, record_msg.udid);

        /* Check if EXT CAM privacy is enabled */
        // For geofence/offduty: Only skip if FULL (PRIVACY_ON), MIXED needs to be requested for editing
        // User alert overrides privacy
        if (!need_to_copy) {
            if (device_mode_global_partial.privacy_status_geofence == PRIVACY_ON ||
                device_mode_global_partial.privacy_status_offduty == PRIVACY_ON ||
                (ctx.privacy_params.ext_cam_privacy && (device_mode_global_partial.privacy_status == PRIVACY_ON))) {
                LOG_I(TAG, "FULL privacy enabled for EXT CAM (geofence=%d, offduty=%d, regular=%d), not requesting video",
                    device_mode_global_partial.privacy_status_geofence,
                    device_mode_global_partial.privacy_status_offduty,
                    device_mode_global_partial.privacy_status);
                post_circular_buffer_add_file_db(filename, CIRCULAR_BUFFER_TYPE_NORMAL, record_msg.cam_num);
                continue;
            }
        }

        // Send message to pull file only if engine idle is not enabled
        create_dummy_file(CIRCULAR_BUFFER_PATH + "/" + filename);
        post_circular_buffer_add_file_db(filename, CIRCULAR_BUFFER_TYPE_NORMAL, record_msg.cam_num);
        LOG_I(TAG, "Sending message for record stream, starttime: %lld, endtime: %lld, filename: %s",
                                 record_msg.start_time, record_msg.end_time, record_msg.filename);
        send_msg((generic_msg_t *)&record_msg, REQ_STREAM_RECORDED_FILE,
                     sizeof( stream_recorded_file_msg_t ), get_msgq_name(), get_ext_cam_msgq_name(), 0);
    }
}

void send_msg_ext_cam_thread(uint64_t time, bool need_to_copy) {
    stream_recorded_file_msg_t record_msg;
    for (int i = 0; i < NUM_EXT_CAMERAS; i++) {
        if (ctx.ext_cam_enabled[i] == false) {
            continue;
        }

        string filename_prefix = ctx.fname[i + DEVICE_CAMERA_POSITION_MAX];
        string filename = filename_prefix + video_file_extn;
        stringstream ss;
        ss << ctx.base_path_cam0 << "/" << filename_prefix;
        string fname = ss.str() + video_file_extn;
        memset(&record_msg, 0, sizeof(&record_msg));
        int pos_trip = filename_prefix.find("_trip");
        int pos_y = filename_prefix.find("_y");
        string sessionName = filename_prefix.substr(pos_trip, pos_y - pos_trip + 2);
        // Block to define scope of sessionMapCountMutex
        {
            std::lock_guard<std::mutex> lock(sessionMapCountMutex);
            std::unordered_map<std::string, int64_t>::const_iterator gotSessionName = sessionMapCount.find(sessionName);
            if (gotSessionName == sessionMapCount.end()) {
                LOG_I(TAG, "ext_cam partial file: %s is not found in map", sessionName.c_str());
                string_to_int64( ctx.sessionCount_string, record_msg.sessionCount);
            } else {
                record_msg.sessionCount = gotSessionName->second;
            }
        }
        LOG_I(TAG, "filename_prefix: %s  record_msg.sessionCount: %d", filename_prefix.c_str(), record_msg.sessionCount);
        strncpy(record_msg.filename, fname.c_str(), sizeof(record_msg.filename));
        record_msg.filename_len = sizeof(record_msg.filename);
        pthread_mutex_lock(&file_start_time_vec_mutex);
        int j = 0;
        for (j = 0; j < file_start_time_vec_pair.size(); j++) {
            if (file_start_time_vec_pair[j].first == ctx.fname[i + DEVICE_CAMERA_POSITION_MAX]) {
                break;
            }
        }
        if (abs((long long int)(time - file_start_time_vec_pair[j].second)) > TOLERANCE_VIDEO_DURATION_MS) {
            record_msg.start_time = time - ONE_MINUTE_DURATION_MS;
        } else {
            record_msg.start_time = file_start_time_vec_pair[j].second;
        }
        pthread_mutex_unlock(&file_start_time_vec_mutex);
        record_msg.end_time = time;
        record_msg.cam_num = i + DEVICE_CAMERA_POSITION_MAX;
        record_msg.framerate = ctx.ext_cam_framerate[i];
        record_msg.mdvr_ch_num = i + 1;
        record_msg.audio_enable = ctx.ext_cam_audio_enable[i];
        string_to_int64(ctx.udid_string, record_msg.udid);

        /* Check if EXT CAM privacy is enabled - only skip if FULL privacy (PRIVACY_ON) */
        if (!need_to_copy) {
            if (device_mode_global_partial.privacy_status_geofence == PRIVACY_ON ||
                device_mode_global_partial.privacy_status_offduty == PRIVACY_ON ||
                (ctx.privacy_params.ext_cam_privacy && (device_mode_global_partial.privacy_status == PRIVACY_ON))) {
                LOG_I(TAG, "FULL privacy enabled for EXT CAM, not requesting video");
                post_circular_buffer_add_file_db(filename, CIRCULAR_BUFFER_TYPE_NORMAL, record_msg.cam_num);
                continue;
            }
        }

        // Send message to pull file only if engine idle is not enabled
        if (!device_mode_global_partial.engine_idle) {
            create_dummy_file(CIRCULAR_BUFFER_PATH + "/" + filename);
            post_circular_buffer_add_file_db(filename, CIRCULAR_BUFFER_TYPE_NORMAL, record_msg.cam_num);
            LOG_I(TAG, "Sending message for record stream, starttime: %lld, endtime: %lld",
                                 record_msg.start_time, record_msg.end_time);
            send_msg((generic_msg_t *)&record_msg, REQ_STREAM_RECORDED_FILE,
                                 sizeof( stream_recorded_file_msg_t ), get_msgq_name(), get_ext_cam_msgq_name(), 0);
        } else {
            LOG_I(TAG, "ND_EXT_CAM: Not sending message to stream file due to engine idle");
        }
    }
}

//frame_time - monotonic raw micro seconds for front camera
//frame_time_extra - epoch micro seconds for front camera
void mediarecorder_callback(RecordState state, uint64_t frame_time, uint64_t frame_time_extra,
                            uint64_t pts_time, void *app, uint64_t session_frame_count, bool is_ld, void* filename)
{
    int cam_num = *((int*)(&app));
    static bool is_gps_at_command_executed  = false;

    if (!cams_enabled[cam_num]) {
        LOG_E (TAG, "camera %d is not enabled, but received mediarecorder_callback",cam_num);
        return;
    }

    if (state == RECORD_START) {
        uint64_t epoch_time_micro = frame_time_extra;
        uint64_t raw_time_micro = frame_time;

        uint64_t epoch_time_ms = epoch_time_micro / ONE_MILLI_IN_MICRO;
        ctx.firstframe_time[cam_num] = epoch_time_ms;
        // store cam start time for last two sessions; use based on out_meta_count value
        ctx.camStartTime[cam_num][(ctx.session_cnt[cam_num]) % 2] = epoch_time_ms;
        ctx.camPtsStartTime[cam_num][ctx.session_cnt[cam_num] % 2] = pts_time;

        LOG_I(TAG, "RECORD_START for cam num %d with session_cnt %d received @ epoch_time_ms: %lld, pts_time: %lld", cam_num, ctx.session_cnt[cam_num], epoch_time_ms, pts_time);

        ctx.session_cnt[cam_num]++;
        ctx.gps_start_time = ctx.saved_gps.timestamp;

        ndc_start_meta_msg_t sm_msg;
        sm_msg.type = START_META;
        sm_msg.cam_pos = cam_num;
        sm_msg.epoch_time = (cam_num == DEVICE_CAMERA_POSITION_FRONT) ? epoch_time_micro : epoch_time_ms;
        sm_msg.raw_time = raw_time_micro;

        stringstream ss;
        ss << cam_num << currvid_fname;
        nd_strncpy(sm_msg.f_name, ss.str().c_str(), sizeof(sm_msg.f_name));

        sm_msg.len = sizeof( ndc_start_meta_msg_t );

        nd_msgq_t::nd_msg_t msg((char *)&sm_msg, sizeof(sm_msg), false);

        ctx.msg_q->send(msg,nd_msgq_t::ND_MSG_MED);

    } else if (state == RECORD_STOP) {
        // Removing filepath and adding cam_num at start of session name
        string session_name;
        string filename_str = string((char *)filename);
        if (!get_session_name_from_string(filename_str, session_name))
            session_name = filename_str;
        session_name = to_string(cam_num) + session_name.substr(1); //Removed 0 from start and added cam_num at the same position

        if (is_ld) {
            LOG_I(TAG, "LD: RECORD_STOP for cam num %d received for filename %s", cam_num, session_name.c_str());
            stop_meta((char *)session_name.c_str(), cam_num, !ctx.session_flipflop, frame_time_extra, frame_time, pts_time, true);
            return;
        }
        ndc_stop_meta_msg_t sm_msg;
        bool curr_ff = ctx.session_flipflop;

        //plug code to send healthstats info for recording complete metadata info
        uint64_t epoch_time_micro = frame_time_extra;
        uint64_t raw_time_micro = frame_time;
        uint64_t epoch_time_ms = epoch_time_micro / ONE_MILLI_IN_MICRO;

        LOG_I(TAG, "RECORD_STOP for cam_num %d filename %s session_cnt %d received @ epoch_time_ms: %lld, pts_time %lld", cam_num, session_name.c_str(), ctx.session_cnt[cam_num] - 1, epoch_time_ms, pts_time);

        if (cam_num == DEVICE_CAMERA_POSITION_FRONT) {
            //Clear the back up buffer, before switching
            end_of_session_atomic = true;
            ctx.meta_buff[!curr_ff].clear();

            // clear qr code counts for the next session
            ctx.qr_codes_detected[!curr_ff] = 0;
            ctx.qr_codes_decoded[!curr_ff] = 0;
            ctx.qr_codes_mismatched[!curr_ff] = 0;

            //Switch the active meta_buff to !curr_ff buffer
            ctx.session_flipflop = !curr_ff;

            //Cut cur_ff buffer at time, copy extra content to !curr_ff buffer
            ctx.meta_buff[curr_ff].split_after(epoch_time_micro, raw_time_micro, ctx.meta_buff[!curr_ff], ctx.hdmaps_mode_enabled);

            //Push buffer contents to genmeta for generating json
            ctx.meta_buff[curr_ff].fill_genmeta(*(ctx.genmeta), curr_ff);

            if (gps_failure == true && is_gps_at_command_executed == false ) {
                gps_auto_start_counter++ ;
                if (gps_auto_start_counter >=  ctx.gps_counter) {
                    gps_auto_gps_config = std::async(std::launch::async,gps_config_recover);
                    LOG_E (TAG,"run_at_command_gps_config_recover is called with async");
                    is_gps_at_command_executed = true;
                }
            } else {
                gps_auto_start_counter = 0;
            }
            LOG_I(TAG, "gps_counter_value: %d, gps_max_fail_counter: %d", gps_auto_start_counter, ctx.gps_counter);

            pthread_mutex_lock(&nextVideoName_mutex);
            if (nextvid_fname_temp == nextvid_fname) {
                prop_data_t entry;
                if (get_property_DB("udid", &entry, db_handle))
                    ctx.udid_string = entry.value;

                if (!increment_property_DB("sessionCount"))
                    LOG_E(TAG, "failed to increment sessionCount to gen props DB");

                ctx.sessionCount_string = "";
                if (get_property_DB("sessionCount", &entry, db_handle))
                	ctx.sessionCount_string = entry.value;

                string sessionCountHex_string, udidHex_string ;
                decimalStr_to_hexStr(ctx.sessionCount_string, sessionCountHex_string, sessionCountSize);
                decimalStr_to_hexStr(ctx.udid_string, udidHex_string, udidSize);

                string fileNameBase = "trip" + udidHex_string + "_part" + sessionCountHex_string ;
                LOG_D(TAG, "file: %s, udid: %s, sessionCount: %s, hexStr: %s", fileNameBase.c_str(), ctx.udid_string.c_str(), ctx.sessionCount_string.c_str(), sessionCountHex_string.c_str());

                nextvid_fname = get_fname(fileNameBase, epoch_time_ms);
                LOG_I(TAG, "Filling nextvid_fname with %s during mediarecorder_callback", nextvid_fname.c_str());
            }
            pthread_mutex_unlock(&nextVideoName_mutex);

            nextvid_fname_temp = nextvid_fname;
            ctx.nextVideoName = "0" + nextvid_fname;
        }

        g_session_frame_count[cam_num] = session_frame_count;
        //curr_ff would have changed to reflect latest session after STOP_DUMP_META execution for outward
        //That is why for other cameras changing it again to reflect the old session in STOP_DUMP_META
        if (cam_num == DEVICE_CAMERA_POSITION_FRONT)
            sm_msg.flipflop = curr_ff;
        else
            sm_msg.flipflop = !curr_ff;

        sm_msg.type = STOP_DUMP_META;
        sm_msg.len = sizeof( ndc_stop_meta_msg_t );
        sm_msg.cam_num = cam_num;
        sm_msg.raw_time = raw_time_micro;
        sm_msg.epoch_time = (cam_num == DEVICE_CAMERA_POSITION_FRONT) ? epoch_time_micro : epoch_time_ms;
        sm_msg.pts_time = pts_time;
        nd_strncpy(sm_msg.f_name, session_name.c_str(), sizeof(sm_msg.f_name));

        nd_msgq_t::nd_msg_t msg((char *)&sm_msg, sizeof(sm_msg), false);
        ctx.msg_q->send(msg, nd_msgq_t::ND_MSG_MED);

#ifdef BAGHEERA2
        string next_session_fname = nextvid_fname + ".mp4";
        LOG_I(TAG, "send START_NEXT_SESSION message to cam_rec service with Filename: %s", next_session_fname.c_str());
        send_start_next_session_msg_to_cam_rec_service(Q_NAME, next_session_fname.c_str());
#endif

    }
    return;
}

void record_start_cb(const char *fname, uint64_t frame_time, uint64_t pts_time, int cam_num)
{
    if (!cams_enabled[cam_num]) {
        LOG_E (TAG, "camera %d is not enabled, but received record_start_cb",cam_num);
        return;
    }
    //for inward and side cameras, frame_time is epoch time in milli seconds.
    ctx.firstframe_time[cam_num] = frame_time;
    // store cam start time for last two sessions; use based on out_meta_count value
    ctx.camStartTime[cam_num][(ctx.session_cnt[cam_num]) % 2] = frame_time;
    ctx.camPtsStartTime[cam_num][(ctx.session_cnt[cam_num]) % 2] = pts_time;

    LOG_I(TAG, "RECORD_START for cam num %d with session_cnt %d received @ epoch_time_ms: %lld, pts_time: %lld", cam_num, ctx.session_cnt[cam_num], frame_time, pts_time);

    ctx.session_cnt[cam_num]++;
    ctx.gps_start_time = ctx.saved_gps.timestamp;
    ndc_start_meta_msg_t sm_msg;
    sm_msg.type = START_META;
    sm_msg.cam_pos = cam_num;
    sm_msg.epoch_time = frame_time;
    sm_msg.raw_time = 0;

    nd_strncpy(sm_msg.f_name, fname, sizeof(sm_msg.f_name));

    sm_msg.len = sizeof( ndc_start_meta_msg_t );

    nd_msgq_t::nd_msg_t msg((char *)&sm_msg, sizeof(sm_msg), false);
    ctx.msg_q->send(msg,nd_msgq_t::ND_MSG_MED);

    return;
}

void record_stop_cb(const char *fname, uint64_t time, uint64_t pts_time, int cam_num, uint64_t session_frame_count, bool is_LD)
{
    ndc_stop_meta_msg_t sm_msg;

    if (!cams_enabled[cam_num]) {
        LOG_E (TAG, "camera %d is not enabled, but received record_stop_cb",cam_num);
        return;
    }

    if (is_LD) {
        LOG_I(TAG, "LD: RECORD_STOP for cam num %d received for filename %s", cam_num, (char*)fname);
        stop_meta((char *)fname, cam_num, !ctx.session_flipflop, time, 0, pts_time, is_LD);
        return;
    }

    LOG_I(TAG, "Record stop for cam_num: %d, epoch_time: %lld, pts_time: %lld", cam_num, time, pts_time);

    if (cam_num == DEVICE_CAMERA_POSITION_BACK) {
        string rt_string = ctx.base_path_cam0 + "/" + "0" + nextvid_fname + ".mkv";
    }

    g_session_frame_count[cam_num] = session_frame_count;

    //curr_ff would have changed to reflect latest session after STOP_DUMP_META execution for outward
    //That is why for other cameras changing it again to reflect the old session in STOP_DUMP_META
    sm_msg.flipflop = !ctx.session_flipflop;
    sm_msg.type = STOP_DUMP_META;
    sm_msg.len = sizeof( ndc_stop_meta_msg_t );
    sm_msg.cam_num = cam_num;
    nd_strncpy(sm_msg.f_name, fname, sizeof(sm_msg.f_name));
    sm_msg.raw_time = 0;
    sm_msg.epoch_time = time;
    sm_msg.pts_time = pts_time;

    nd_msgq_t::nd_msg_t msg((char *)&sm_msg, sizeof(sm_msg), false);
    ctx.msg_q->send(msg, nd_msgq_t::ND_MSG_MED);

    return;
}

static bool enable_imu_sensor()
{
    if(ctx.imu && (imu_init_done == true)) {
        if( false == ctx.imu->enable_sensor(Imu::IMU_ACCEL) ) {
            LOG_C(TAG,"Cannot enable Accel");
            nd_service_obj->send_err_msg(SM_E_NDC_IMU_FAIL, 0, "Cannot enable Accel");
            return false;
        }
    }

    if(ctx.imu && (imu_init_done == true)) {
        if( false == ctx.imu->enable_sensor(Imu::IMU_GYRO) ) {
            LOG_C(TAG,"Cannot enable gyro");
            nd_service_obj->send_err_msg(SM_E_NDC_IMU_FAIL, 0, "Cannot enable gyro");
            return false;
        }
    }
    return true;
}

static bool enable_all_sensors()
{
    LOG_I(TAG, "Enabling IMU sensor");
    enable_imu_sensor();

    if(ublox_enabled == false) {
        if(ctx.gps) {
            if( false == ctx.gps->enable_gps(ctx.hdmaps_mode_enabled) ) {
                LOG_C(TAG,"Cannot enable GPS");
                nd_service_obj->send_err_msg(SM_E_NDC_GPS_FAIL, 0, "Cannot enable gps");
                //return false;
            }
        }
    }

#ifdef BAGHEERA2
    if (ctx.ublox) {
        if (false == ctx.ublox->enable_ublox ()) {
            LOG_C(TAG,"Cannot enable Ublox");
            return false;
        }
    }
#endif

    ctx.pipeline_enabled = true;

    return true;
}

static bool start_meta(string fname, uint64_t epoch_time_micro, uint64_t raw_time_micro)
{
    stringstream ss;
    ss << fname << video_file_extn;

    if (end_of_session_atomic) {
        LOG_I(TAG, "delete old partial meta file: %s", ctx.meta_csv_partial_path.c_str());

        pthread_mutex_lock(&meta_partial_file_mutex);
        ctx.fstream_partial_meta_file.close();
        file_delete( ctx.meta_csv_partial_path );

        ctx.meta_csv_partial_path = ctx.next_meta_csv_partial_path ;
        ctx.fstream_partial_meta_file.open (ctx.meta_csv_partial_path.c_str() );
        LOG_I (TAG, "opening partial meta file: %s", ctx.meta_csv_partial_path.c_str());

        ctx.fstream_partial_meta_file << "latitude, longitude, altitude, accuracy, bearing, speed, timestamp" << endl;

        stringstream ss_p_meta("");
        ss_p_meta.str("");
        ss_p_meta << "outward_privacy" << ", " << ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_FRONT];
        ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;

        ss_p_meta.str("");
        ss_p_meta << "inward_privacy" << ", " << ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_BACK];
        ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;

        ss_p_meta.str("");
        ss_p_meta << "left_privacy" << ", " << ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_LEFT];
        ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;

        ss_p_meta.str("");
        ss_p_meta << "right_privacy" << ", " << ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_RIGHT];
        ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;

        ss_p_meta.str("");
        ss_p_meta << "ext_cam_privacy" << ", " << ctx.privacy_params.ext_cam_privacy;
        ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;

        ss_p_meta.str("");
        ss_p_meta << "driveri_audio_privacy" << ", " << ctx.privacy_params.driveri_audio_privacy;
        ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;

        ss_p_meta.str("");
        ss_p_meta << "ext_cam_audio_privacy" << ", " << ctx.privacy_params.ext_cam_audio_privacy;
        ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;

        ss_p_meta.str("");
        ss_p_meta << "gps_privacy" << ", " << ctx.privacy_params.gps_privacy;
        ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;

        ss_p_meta.str("");
        ss_p_meta << "personal_privacy" << ", " << ctx.off_duty_privacy;
        ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;

        ss_p_meta.str("");
        ss_p_meta << "geofence_privacy" << ", " << ctx.geofence_privacy;
        ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;

        ss_p_meta.str("");
        ss_p_meta << "enhanced_privacy" << ", " << ctx.privacy_params.enhanced_privacy;
        ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;

        ss_p_meta.str("");
        ss_p_meta << "save_user_alert_video" << ", " << ctx.privacy_params.save_user_alert_video;
        ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;

        ss_p_meta.str("");
        ss_p_meta << "upload_video_outward" << ", " << ctx.privacy_params.upload_video[DEVICE_CAMERA_POSITION_FRONT];
        ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;

        ss_p_meta.str("");
        ss_p_meta << "upload_video_inward" << ", " << ctx.privacy_params.upload_video[DEVICE_CAMERA_POSITION_BACK];
        ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;

        ss_p_meta.str("");
        ss_p_meta << "upload_video_left" << ", " << ctx.privacy_params.upload_video[DEVICE_CAMERA_POSITION_LEFT];
        ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;

        ss_p_meta.str("");
        ss_p_meta << "upload_video_right" << ", " << ctx.privacy_params.upload_video[DEVICE_CAMERA_POSITION_RIGHT];
        ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;

        ss_p_meta.str("");
        ss_p_meta << "upload_video_ext_cam" << ", " << ctx.privacy_params.upload_video_ext_cam;
        ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;

        pthread_mutex_unlock(&meta_partial_file_mutex);

        end_of_session_atomic = false;
    }

    pthread_mutex_lock(&meta_partial_file_mutex);
    if ( false == enable_genmeta(ss.str(), epoch_time_micro, raw_time_micro) ) {
        LOG_C(TAG,"Cannot enable genmeta");
        pthread_mutex_unlock(&meta_partial_file_mutex);
        return false;
    }
    pthread_mutex_unlock(&meta_partial_file_mutex);

    return true;
}

void create_session_ndout(string session_str) {
    stringstream ss;
    ss << ND_OUTPUT_PATH;
    ss << DEVICE_CAMERA_POSITION_FRONT;
    ss << session_str;
    string session_path = ss.str();

    if(file_mkdir(session_path, 0777, true) == false) {
        LOG_I(TAG, "Failed to create folder for session %s in ndout", session_path.c_str());
    }
}

static string get_fname(string header, uint64_t session_first_frame_epoch_time_ms)
{
    stringstream ss;
    stringstream ss_temp;
    ss_temp << std::fixed  << std::setprecision(4);

    int64_t time = session_first_frame_epoch_time_ms;

    fname_const_part = "";
    LOG_I(TAG, "changing streamname to everyone");
    ss_temp << ctx.saved_gps.latitude << "_" <<
        ctx.saved_gps.longitude << "_" << "0.0" << "_" << time;

    fname_const_part = ss_temp.str();

    ss << "_" << header << "_" << fname_const_part << "_y";

    string session_str = ss.str();
    LOG_I (TAG,"creating folder for session %s in ndout", session_str.c_str());
    nd_out_sess_dir_fut = std::async(std::launch::async, create_session_ndout, session_str);
    LOG_I (TAG,"create_session_ndout() is called with async");

    return ss.str();
}

#ifdef KRAIT
bool read_adc_status_and_channel_two_data(float *adc_val,int *adc_state) {
    struct timeval now;
    struct timespec timeToWait;
    int ret;
    if(NULL == obd_data_ptr) {
        LOG_E(TAG,"obd_adc_open failed to allocate memory for obd_data_ptr");
        return false;
    }
    gettimeofday(&now,NULL);
    timeToWait.tv_sec = now.tv_sec+COND_VAR_TIMEOUT;
    timeToWait.tv_nsec = (now.tv_usec+1000UL*COND_VAR_TIMEOUT)*1000UL;
    pthread_mutex_lock(&obd_data_lock);
    ret = pthread_cond_timedwait (&obd_data_cv,&obd_data_lock, &timeToWait);
    if (ETIMEDOUT == ret) {
        LOG_W(TAG, "read_adc_channel_two_data: CV Timeout when trying to read value.");
        pthread_mutex_unlock(&obd_data_lock);
        return false;
    }
    *adc_state = obd_data_ptr->adc_state;
    memcpy(adc_val,&obd_data_ptr->adc_value,sizeof(float));
    pthread_mutex_unlock(&obd_data_lock);

    return true;
}

bool msg_cb_obd(ndmb_generic_msg_t *msg) {
    ndmbmsg_obd_adc_data_t *ptr1=NULL;
    ptr1 = reinterpret_cast<ndmbmsg_obd_adc_data_t *>( msg );
    if(NULL == ptr1) {
        LOG_E(TAG, "obd publisher failed to send data");
        return false;
    }
    if(NULL == obd_data_ptr) {
        LOG_E(TAG,"obd_adc_open failed to allocate memory for obd_data_ptr");
        return false;
    }
    if( ptr1->topic != TOPIC_OBD_ADC_DATA ) {
        LOG_I(TAG, "Unkown topic: ->%s",ptr1->topic);
        return false;
    }
    pthread_mutex_lock(&obd_data_lock);
    bool ret = memcpy(obd_data_ptr,ptr1, sizeof(ndmbmsg_obd_adc_data_t));
    if (false == ret) {
        LOG_E(TAG,"Memory copy failed for obd_data_ptr with return:%d",ret);
        pthread_cond_signal(&obd_data_cv);
        pthread_mutex_unlock(&obd_data_lock);
        return false;
    }
    is_adc_updated = true;
    pthread_cond_signal(&obd_data_cv);
    pthread_mutex_unlock(&obd_data_lock);

    LOG_D(TAG,"obd topic:%s",obd_data_ptr->topic);
    LOG_D(TAG,"obd adc value:%f",obd_data_ptr->adc_value);
    LOG_D(TAG,"obd state:%d",obd_data_ptr->adc_state);

    return true;
}

void* obd_adc_subs_funcptr(void *args) {
    std::string obd_adc_client = "NDMB_ND_ADC_SERVICE";
    NDMBClient msg_client(obd_adc_client);
    bool ret = msg_client.subscribe(TOPIC_OBD_ADC_DATA, msg_cb_obd);
    if(false == ret) {
        LOG_E(TAG,"msg_client.subscribe failed");
        return (void *)false;
    }
    while (1){ sleep(THREAD_TIMEOUT); }

    return (void *)true;
}

void* adc_update_main(void *args) {
	while (!is_adc_updated)
	{
		sleep(1);
	}
	LOG_E(TAG, "Updating the voltage with %f", obd_data_ptr->adc_value);
	stringstream ss_p_meta("");
	stringstream ss_p_value;
	ss_p_meta << "Voltage in Volts" << ", " << obd_data_ptr->adc_value;
	ss_p_value << obd_data_ptr->adc_value;
	ctx.genmeta->update_genmeta_header("Voltage in Volts", ss_p_value.str(), ctx.session_flipflop);
	ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;

	return (void*) true;
}
#endif

static bool enable_genmeta(string fname, uint64_t epoch_time_micro, uint64_t raw_time_micro)
{
    genmeta_header_t header;
    stringstream ss_p_meta("");
#ifdef BAGHEERA2
    header.volts = read_adc_channel_two_data();
#elif KRAIT
    header.volts = obd_data_ptr->adc_value;
#endif
    ss_p_meta << "Voltage in Volts" << ", " << header.volts;
    ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;
    header.udid_string = ctx.udid_string;
    ss_p_meta.str("");
    ss_p_meta << "udid" << ", " << header.udid_string;
    ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;

    prop_data_t entry;
    ctx.sessionCount_string = "";
    if (get_property_DB("sessionCount", &entry, db_handle)) {
        ctx.sessionCount_string = entry.value;
    }
    LOG_I(TAG, "ctx.sessionCount_string is %s", ctx.sessionCount_string.c_str());
    header.sessionCount_string = ctx.sessionCount_string;
    ss_p_meta.str("");
    ss_p_meta << "sessionCount" << ", " << ctx.sessionCount_string;
    ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;

    ctx.rtcValidTime_string = "";
    if (get_property_DB("rtcValidTime", &entry, db_handle)) {
        ctx.rtcValidTime_string = entry.value;
    }
    int64_t savedRtcTime, currTime = get_system_time();
    string_to_int64(ctx.rtcValidTime_string, savedRtcTime);
    if( ( currTime > savedRtcTime ) && ( currTime < (savedRtcTime + month_in_seconds * 1000 ) ) ) {
        ctx.rtcValidTime_string = to_string(currTime);
        LOG_I(TAG, "ctx.rtcValidTime_string is %s", ctx.rtcValidTime_string.c_str());

        if ((set_property_DB("rtcValidTime", ctx.rtcValidTime_string)) == false) {
            nd_service_obj->send_err_msg(SM_E_NDC_SET_PROP_DB_FAIL, NDService::UNUSED_ERR_AUX_CODE, "set_property_DB failed" );
            LOG_E(TAG, "set_property_DB failed for rtcValidTime");
        }
        if (ctx.rtcValid_string ==  "0") {
            ctx.rtcValid_string =  "2";
        }
        else{
            ctx.rtcValid_string =  "1";
        }
    }
    else {
        LOG_E(TAG, "RTC Not valid. currTime: %lld savedRtcTime: %lld", currTime, savedRtcTime);
        ctx.rtcValid_string =  "0";
    }
    ss_p_meta.str("");
    ss_p_meta << "rtcValid" << ", " << ctx.rtcValid_string;
    ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;

    header.lpw_no_record = file_is_present(lpw_no_record_persistent_file);
    ss_p_meta.str("");
    ss_p_meta << "lpw_no_record" << ", " << header.lpw_no_record ;
    ctx.fstream_partial_meta_file << ss_p_meta.str() << endl ;

    ss_p_meta.str("");
    ss_p_meta << "privacy" << ", " << to_string(ctx.off_duty_privacy) << ", " << to_string(ctx.privacy_params.enhanced_privacy) << ", " << to_string(ctx.fused_privacy) << ", " << to_string(ctx.geofence_privacy);
    ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;
    LOG_I(TAG, "enable_genmeta(): offduty_privacy: %d, enhanced_privacy: %d, fused_privacy: %d, geofence_privacy: %d",
          ctx.off_duty_privacy, ctx.privacy_params.enhanced_privacy, ctx.fused_privacy, ctx.geofence_privacy);

    /*ToDo: make parser agnostic to case*/
    header.device_id = ctx.device_config->getConfig("identity","deviceId","");
    if (header.device_id == "") {
        header.device_id = ctx.device_config->getConfig("identity", "deviceid", "");
        if (header.device_id == "") {
            LOG_C(TAG,"cannot find deviceId");
            return false;
        }
    }
    LOG_I(TAG, "Device Id: %s", header.device_id.c_str());
    ss_p_meta.str("");
    ss_p_meta << "deviceId" << ", " << header.device_id;
    ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;

    header.driver_id = get_driver_id();
    ss_p_meta.str("");
    ss_p_meta << "driverId" << ", " << header.driver_id;
    ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;

    header.driver_id_v2 = get_driver_id_v2();
    ss_p_meta.str("");
    ss_p_meta << "driverId_v2" << ", " << header.driver_id_v2;
    ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;

    header.session_id = ctx.device_config->getConfig("identity", "sessionId", "");
    if (header.session_id == "") {
        header.session_id = ctx.device_config->getConfig("identity", "sessionid", "");
        if (header.session_id == "") {
            LOG_C(TAG,"cannot find sessionId");
            return false;
        }
    }
    LOG_I(TAG, "Session Id: %s", header.session_id.c_str());
    ss_p_meta.str("");
    ss_p_meta << "sessionId" << ", " << header.session_id;
    ctx.fstream_partial_meta_file << ss_p_meta.str();
    ctx.fstream_partial_meta_file << endl;

    if (false == is_nd_config_corrupted) {
        header.app_ver = ctx.nd_config->getConfig("version", "ndDevice", "");
        if (header.app_ver == "") {
            header.app_ver = ctx.nd_config->getConfig("version", "nddevice", "");
            if (header.app_ver == "") {
                LOG_C(TAG,"cannot find version, assigning default app version 2.0.0");
                header.app_ver = "2.0.0";
            }
        }
    } else {
        LOG_I( TAG, "nddevice.ini is corrupted, assigning default app version 2.0.0");
        header.app_ver = "2.0.0";
    }
    header.metadata_ver = "3.0";
    LOG_I(TAG, "app version: %s metadata version: %s", header.app_ver.c_str(), header.metadata_ver.c_str());
    ss_p_meta.str("");
    ss_p_meta << "app_ver" << ", " << header.app_ver;
    ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;

//   System UP time
#ifdef BAGHEERA2
    header.system_uptime = (get_system_monotonic_time() ) / 1000;
    LOG_I(TAG, "systemUpTime: %lld", header.system_uptime);
#elif KRAIT
    struct sysinfo info;
    if (!sysinfo (&info))
    {
        header.system_uptime = info.uptime;
        LOG_I( TAG, "systemUpTime: %lld", header.system_uptime);
    }
    else
    {
        header.system_uptime = -1;
        LOG_E (TAG, "Failed to get sysinfo with errno %d",errno);
    }
#endif
    ss_p_meta.str("");
    ss_p_meta << "systemUpTime" << ", " << header.system_uptime;
    ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;


//   Service UP time
    header.service_uptime  = (get_system_monotonic_time() - service_start_time)/1000; // In Seconds
    LOG_I( TAG, "serviceUpTime: %lld", header.service_uptime);
    ss_p_meta.str("");
    ss_p_meta << "serviceUpTime" << ", " << header.service_uptime;
    ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;

    header.devicetype = ctx.device_config->getConfig("identity", "deviceType", "");
    if (header.devicetype == "") {
        header.devicetype = ctx.device_config->getConfig("identity", "devicetype", "");
        if (header.devicetype == "") {
            LOG_C(TAG, "cannot find deviceType");
            return false;
        }
    }
    LOG_I(TAG, "devicetype :%s:", header.devicetype.c_str());
    ss_p_meta.str("");
    ss_p_meta << "deviceType" << ", " << header.devicetype;
    ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;

    header.devicesubtype = ctx.device_config->getConfig("identity", "deviceSubType", "");
    if (header.devicesubtype == "") {
        header.devicesubtype = ctx.device_config->getConfig("identity", "devicesubtype", "");
        if (header.devicesubtype == "") {
            LOG_C(TAG, "cannot find devicesubtype");
            return false;
        }
    }
    LOG_I(TAG, "devicesubtype:%s:", header.devicesubtype.c_str());
    ss_p_meta.str("");
    ss_p_meta << "deviceSubType" << ", " << header.devicesubtype;
    ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;

    header.vehclass = ctx.device_config->getConfig("vehicle", "vehClass", "");
    if (header.vehclass == "") {
        header.vehclass = ctx.device_config->getConfig("vehicle", "vehclass", "");
        if (header.vehclass == "") {
            LOG_C(TAG, "cannot find vehclass");
            return false;
        }
    }
    LOG_I(TAG, "vehclass :%s:", header.vehclass.c_str());
    ss_p_meta.str("");
    ss_p_meta << "vehClass" << ", " << header.vehclass;
    ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;

    bool is_val_overridden = false;
    header.audio_enable = audio_enable;
    ss_p_meta.str("");
    ss_p_meta << "audioEnable" << ", " << header.audio_enable;
    ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;

    LOG_I(TAG, "audio_enable :%d:", header.audio_enable);

    header.vertical_angle = 53.359547;
    header.horizontal_angle = 68.202896;

    header.vehicle_id = "565656";
    header.speed_unit = "mph";

    header.trip_no = 1;
    header.part_no = 1;
    header.offset = 0;
    //header.is_last = true;

    header.time = get_date();
    ss_p_meta.str("");
    ss_p_meta << "Time" << ", " << header.time;
    ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;

    header.start_time_micro = epoch_time_micro;
    header.start_time_raw_micro = raw_time_micro;
    header.start_time = (epoch_time_micro / ONE_MILLI_IN_MICRO); //"startTime" in metadata needs to be in milli
    LOG_I(TAG, "header.start_time_micro = %lld\t header.star_time_raw_micro:%lld", header.start_time_micro, header.start_time_raw_micro);

    ss_p_meta.str("");
    ss_p_meta << "startTime" << ", " << header.start_time;
    ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;

    ss_p_meta.str("");
    ss_p_meta << "startTimeRawMicro" << ", " << header.start_time_raw_micro;
    ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;

    header.video_name = fname;
    ss_p_meta.str("");
    ss_p_meta << "videoName" << ", " << header.video_name;
    ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;
    header.prevVideoName = ctx.prevVideoName;
    ss_p_meta.str("");
    ss_p_meta << "prevVideoName" << ", " << header.prevVideoName;
    ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;

    header.gps_end_time = 0;
    header.end_time = 0;

    int cameras_enabled = 0;
    for (int i = 0; i < NUM_CAMERAS; i++) {
        if (cams_enabled[i]) {
            cameras_enabled |= (1 << i);
        }
    }
    for (int i = 0; i < NUM_EXT_CAMERAS; i++) {
        if (ctx.ext_cam_enabled[i]) {
            cameras_enabled |= (1 << (4+i));
        }
    }
    if (cams_enabled[DEVICE_CAMERA_POSITION_DMS])
        cameras_enabled |= (1 << 8);

    header.cams_enabled = cameras_enabled;
    ss_p_meta.str("");
    ss_p_meta << "cameras" << ", " << header.cams_enabled;
    ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;
    ctx.genmeta->start_data_collection(header, ctx.session_flipflop);

    return true;
}

static bool start_photodiode()
{
    if (ctx.photodiode->enable_sensor() == false) {
        LOG_E (TAG, "Can't enable photodiode sensor");
        return false;
    }
    return true;
}

#define CHANGE_FILE_PERMISSION

bool send_fname_update(string fname, int camera_id) {
    for (int i=0; i<(sizeof (fname_update_clients)/sizeof (string)); i++)
    {
        res_fname_update_msg_t m;
        m.handle = 0;
        strncpy(m.fname, fname.c_str(), sizeof(m.fname));
        m.camera_id = camera_id;

        bool res = send_msg( (generic_msg_t *)&m, RES_FNAME_UPDATE, sizeof(m),
                get_msgq_name(), fname_update_clients[i], msg_idx++ );
        LOG_I( TAG, "Sending message %d to %s status: %d, handle=%d",
                RES_FNAME_UPDATE, fname_update_clients[i].c_str(), res,m.handle);
    }
}

static bool disable_all_sensors()
{
    if( ctx.gps && (false == ctx.gps->disable_gps( )) ) {
        LOG_E(TAG,"Cannot disable GPS");
        return false;
    }

#ifdef BAGHEERA2
    if (ctx.ublox) {
        if( false == ctx.ublox->disable_ublox( ) ) {
            LOG_E(TAG,"Cannot disable UBLOX");
            return false;
        }
    }
#endif

    if(ctx.imu) {
        if( false == ctx.imu->disable_sensor(Imu::IMU_ACCEL) ) {
            LOG_E(TAG,"Cannot disable Accel");
            //return false;
        }
    }

    if(ctx.imu) {
        if( false == ctx.imu->disable_sensor(Imu::IMU_GYRO) ) {
            LOG_E(TAG,"Cannot disable Gyro");
            //return false;
        }
    }

    ctx.pipeline_enabled = false;

    return true;
}

static bool move_files(string fname, string dest)
{
    stringstream command;
    string folder_name, file_name;
    vector<string> vec;

    //Check for the existence and size of input file
    int ret = GetFileSize(fname);
    if (ret < 0) {
        LOG_E (TAG, "Source file with name %s does not exist", fname.c_str());
        return false;
    } else if (ret == 0) {
        LOG_E (TAG, "Source file with name %s is of size 0", fname.c_str());
        return false;
    }

    if (!get_folder_file_names(fname, folder_name, file_name)) {
        LOG_E(TAG, "Failed to get folder and file names from given path");
        return false;
    }

    //Destination file
    string final_video_full_name = dest + "/" + file_name;
    LOG_I(TAG, "Move file %s to folder %s", fname.c_str(), final_video_full_name.c_str());

    if (rename(fname.c_str(), final_video_full_name.c_str()) < 0) {
        LOG_E(TAG, "moving file to circular buffer failed with error: %s", strerror(errno));
        nd_service_obj->send_err_msg(SM_E_NDC_SD_CARD_CPY_FAIL, NDService::UNUSED_ERR_AUX_CODE, "Move to circular buffer failed");
        return false;
    }

    ret = GetFileSize(final_video_full_name);
    if (ret < 0) {
        LOG_E (TAG, "Destination file with name %s does not exist", final_video_full_name.c_str());
        return false;
    } else if (ret == 0) {
        LOG_E (TAG, "Destination file with name %s is of size 0", final_video_full_name.c_str());
        return false;
    }

    if (file_fd_sync(final_video_full_name) == false) {
        LOG_E(TAG, "failed to sync file after move");
    }

    return true;
}

bool file_copy_wrapper(string src, string dest)
{
    int64_t copy_starttime = get_system_monotonic_time();
    int copy_retry = 0;
    bool copy_done = false;
    bool copy_status = false;

    if (file_is_present(src) == false) {
        LOG_E(TAG, "src file %s is not present, so copy not possible", src.c_str());
        return false;
    }

    do {
        if (video_encryption)
            copy_status = operate_if_video_and_copy(src, dest, true, 0644, true);
        else
            copy_status = file_copy(src, dest, true, 0644, true);

	    if (false == copy_status) {
		    LOG_E(TAG, "NDC: failed to copy file %s to %s; ignoring this request",
				    src.c_str(), dest.c_str());
		    file_delete(dest.c_str());
		    copy_retry++;
		    copy_done = false;
		    sleep(5);
	    } else {
		    copy_done = true;
		    break;
	    }
    } while(copy_retry < 2);

    if (copy_done == false) {
        LOG_E(TAG, "file copy to circular buffer failed : %s to %s", src.c_str(), dest.c_str());
        file_delete(src);
        return false;
    }
    LOG_D(TAG, "Copy success; proceeding after copying");

    if (video_encryption)
    {
        int32_t src_file_size = file_size(src);
        int32_t dest_file_size = file_size(dest);
        int32_t operate_delta_size = get_delta_size(src);

        // This is a work around and not fix to retry copying when file size in msg and actual size mismatch
        if (dest_file_size < ( src_file_size + operate_delta_size))
        {
            LOG_I(TAG, "src_file_size: %d, dest_file_size: %d, operate_delta_size: %d",
                    src_file_size, dest_file_size, operate_delta_size);
	        sleep(5);
	        file_delete(dest.c_str());
            copy_status = operate_if_video_and_copy(src, dest, true, 0644, true);

            if (false == copy_status) {
                LOG_E(TAG, "NDC: failed to copy file second time: %s to %s; ignoring this request",
                        src.c_str(), dest.c_str());
                file_delete(src);
                return false;
            }
        }
    }
    int64_t copy_endtime = get_system_monotonic_time();
    LOG_I(TAG, "File copy successful from %s to %s. Sizes src = %d, dest = %d",
                src.c_str(), dest.c_str(), file_size(src), file_size(dest));
    LOG_I(TAG, "time taken to copy = %lld", copy_endtime - copy_starttime);
    file_delete(src);
    return true;
}

#ifdef BAGHEERA2
static const string edit_file_list_path = "/dev/shm/nd_files_c/video_edit_filelist.txt";
static const string temp_chunk_path = "/dev/shm/nd_files_c/";
#elif KRAIT
static const string edit_file_list_path = "/dev/shm/video_edit_filelist.txt";
static const string temp_chunk_path = "/dev/shm/";
#endif
static const string blackout_video = "/home/ubuntu/.nddevice/blackvideo_editing.mp4";
static const string blackout_video_dms = "/home/ubuntu/.nddevice/blackvideo_editing_dms.mp4";
static const string blackout_video_dms_ld = "/home/ubuntu/.nddevice/blackvideo_editing_dms_LD.mp4";
static const string blackout_video_fhd = "/home/ubuntu/.nddevice/blackvideo_editing_FHD.mp4";
static const string blackout_video_fld = "/home/ubuntu/.nddevice/blackvideo_editing_FLD.mp4";
static const string blackout_video_ld = "/home/ubuntu/.nddevice/blackvideo_editing_LD.mp4";
static const string blackout_video_side = "/home/ubuntu/.nddevice/blackvideo_editing_side.mp4";


bool clean_temp_chunks(vector <string> all_chunks) {
    LOG_I(TAG, "inside clean_temp_chunks");
    for (auto& it : all_chunks) {
        LOG_I(TAG, "deleting %s of %d size", it.c_str(), GetFileSize(it) );
        file_delete(it);
    }
    file_delete(edit_file_list_path);
    return true;
}

bool blackout_privacy_and_save_video(string src_file, string dest_path, device_mode_t &device_mode, int cam_num,
        bool ldFile, int video_length_sec = 60) {

    //Check for existance of at least input videofile
    if (GetFileSize (src_file) < 0) {
        LOG_E (TAG, "GetFileSize failed for input file %s", src_file.c_str());
        return false;
    }

    string folder_name, file_name;
    if (!get_folder_file_names(src_file, folder_name, file_name)) {
        LOG_E(TAG, "Failed to get folder and file names from given path");
        return false;
    }
    //Destination file
    string dest_file = dest_path + "/" + file_name;
    LOG_I(TAG, "editing video file %s to folder %s", src_file.c_str(), dest_file.c_str());

    // get the privacy times and edit accordingly
    int privacy_switch_count = 0;
    int privacy_switches = device_mode.individual_states_len;

    int temp_cam_num = cam_num;
    if (temp_cam_num == DEVICE_CAMERA_POSITION_DMS)
        temp_cam_num = DEVICE_CAMERA_POSITION_BACK;

    bool cam_privacy;

    if (src_file.find(partial_session_name) == std::string::npos) {
        LOG_I(TAG, "Full session case in blackout");
        cam_privacy = ctx.privacy_params.cam_privacy[temp_cam_num];
    }
    else {
        LOG_I(TAG, "Partial session case in blackout");
        cam_privacy = device_mode_global_partial.partial_privacy_params.cam_privacy[temp_cam_num];
    }
    LOG_I(TAG, "cam_privacy: %d", cam_privacy);

    if (device_mode.individual_states_len < 2) {
        LOG_E(TAG, "No switch of privacy ; shouldnt have called this function");
        return false;
    }

    int64_t start_time = device_mode.individual_states[0].event_time_monotonic, pres_time;
    int pres_reason, chunk_cam_privacy;
    vector <string> all_chunks;
    float total_duration = 0;
    float video_total_duration = 0.0f;
    if (video_length_sec < ASSUMED_FULL_SESSION_DURATION) {
        //partial video length fetched from privacy entries in csv was sometimes less than actual duration so now we are manually calculating duration
        //using ffprobe frames/fps
        string command_to_calc_frames = "ffprobe -v error -select_streams v:0 -count_packets -show_entries stream=nb_read_packets -of csv=p=0:nk=1 " + src_file;
        string resp = "";
        int res = system_execute_with_resp("CALC_DUR", command_to_calc_frames, resp);
        Config_parser bagheera_config(BAGHEERACONFIG_INI);
        if (res == 0) {
            video_total_duration = static_cast<float>(video_length_sec);
            LOG_I(TAG, "failed to execute command_to_calc_frames so setting video_total_duration to video_length_sec = %d", video_length_sec);
        }
        else {
            int calc_total_frames = 0;
            string_to_integer(resp.c_str(), calc_total_frames);
            int fps = 15;

            if (cam_num == DEVICE_CAMERA_POSITION_FRONT) {
                string temp = "";
                fps = 30;
                bool is_val_overridden = false;
                temp = bagheera_config.getConfig("camera", "outward_nrt_fps", "30", true, is_val_overridden);
                string_to_integer(temp.c_str(), fps);
            }

            if (cam_num == DEVICE_CAMERA_POSITION_BACK) {
                string temp = "";
                fps = 15;
                bool is_val_overridden = false;
                temp = bagheera_config.getConfig("camera", "inward_nrt_fps", "15", true, is_val_overridden);
                string_to_integer(temp.c_str(), fps);
            }

            if (cam_num == DEVICE_CAMERA_POSITION_DMS) {
                string temp = "";
                fps = 30;
                bool is_val_overridden = false;
                temp = bagheera_config.getConfig("camera", "dms_nrt_fps", "30", true, is_val_overridden);
                string_to_integer(temp.c_str(), fps);
            }

            if (cam_num == DEVICE_CAMERA_POSITION_LEFT || cam_num == DEVICE_CAMERA_POSITION_RIGHT) {
                string temp = "";
                fps = OTHER_FPS;
                string_to_integer(temp.c_str(), fps);
            }
            float duration = static_cast<float>(calc_total_frames) / fps;
            video_total_duration = std::floor(duration * 1000.0f) / 1000.0f; // truncate to 3 decimals
            video_total_duration = std::min(60.0f, video_total_duration);
            LOG_I(TAG, "calc_total_frames = %d, fps = %d, video_total_duration from frames/fps calc = %0.3f", calc_total_frames, fps, video_total_duration);
        }
    } else {
        video_total_duration = static_cast<float>(video_length_sec);
    }
    
    while (privacy_switch_count < privacy_switches) {
        string videofile;
        float from_time, to_time;
        pres_reason = device_mode.individual_states[privacy_switch_count].privacy_reason;
        pres_time = device_mode.individual_states[privacy_switch_count].event_time_monotonic;
        LOG_I(TAG, "pres_reason %d pres_time %lld", pres_reason, pres_time);

        from_time = (pres_time - start_time-500) / 1000.0; // -500 to round off to closest I frame
        string chunk_dest_file = temp_chunk_path + "edit_" + std::to_string(privacy_switch_count) + ".mp4";
        if (privacy_switch_count == privacy_switches -1) { // for last entry make equals session duration
            to_time = video_total_duration;
        } else {
            to_time = (device_mode.individual_states[privacy_switch_count+1].event_time_monotonic - start_time)/1000.0;
        }
        privacy_switch_count++;

        // do sanity before using the values
        if (from_time > to_time) {
            LOG_E(TAG, "from_time %f > to_time %f", from_time, to_time);
            continue;
        }

        if (pres_reason == REASON_OFFDUTY || pres_reason == REASON_GEOFENCE) {
            chunk_cam_privacy = 1;
        } else if (pres_reason == REASON_NO_PRIVACY) {
            chunk_cam_privacy = 0;
        } else if(pres_reason == REASON_ENHANCED) {
            if (temp_cam_num == DEVICE_CAMERA_POSITION_BACK) {
                chunk_cam_privacy = 1;
            } else {
                chunk_cam_privacy = 0;
            }
        } else if (pres_reason == REASON_SPEED || pres_reason == REASON_IGNITION || pres_reason == REASON_BUTTON_BASED) {
            chunk_cam_privacy = cam_privacy;
        } else {
            LOG_E(TAG, "Wrong privacy_reason: %d", pres_reason);
            continue;
        }

        if (chunk_cam_privacy == true) {
            switch (cam_num) {
                case DEVICE_CAMERA_POSITION_FRONT:
                    if (ldFile)
                        videofile = blackout_video_fld;
                    else
                        videofile = blackout_video_fhd;
                    break;
                case DEVICE_CAMERA_POSITION_BACK:
                    if (ldFile)
                        videofile = blackout_video_ld;
                    else
                        videofile = blackout_video;
                    break;
                case DEVICE_CAMERA_POSITION_LEFT:
                case DEVICE_CAMERA_POSITION_RIGHT:
                    videofile = blackout_video_side;
                    break;
                case DEVICE_CAMERA_POSITION_DMS:
                    if (ldFile)
                        videofile = blackout_video_dms_ld;
                    else
                        videofile = blackout_video_dms;
                    break;
                default:
                    LOG_E(TAG, "Wrong cam_num:%d came in blackout_privacy_and_save_video", cam_num);
                    break;
            }

            // if using blackout video always start from 0th sec
            to_time = to_time - from_time;
            from_time = (from_time - from_time); // effectively making this to 0
        } else {
            videofile = src_file;
        }
        // use appropriate input file(videofile) and duration from_time and (to_time-from_time)
        // crop the file and create a chunk
        string command_create_chunk = "ffmpeg -y -i " + videofile + " -ss " + std::to_string(from_time) +
                        " -t " + std::to_string(to_time-from_time) + " -vcodec copy " + chunk_dest_file + " 2>&1 ";
        LOG_I(TAG, "command_create_chunk %s", command_create_chunk.c_str());

        // lock before using src_file
        pthread_mutex_lock(&file_mutex);
        system_execute("EDIT_INWV", command_create_chunk);
        pthread_mutex_unlock(&file_mutex);

        total_duration += to_time-from_time;
        LOG_I(TAG, "Combined duration accumulated = %f", total_duration);
        all_chunks.push_back(chunk_dest_file);
    }

    ofstream edit_file_list;
    edit_file_list.open (edit_file_list_path.c_str());
    for (auto& it : all_chunks) {
        edit_file_list << "file '" << it << "'" << endl;
    }
    edit_file_list.close();

    // write file chunks locations to edit_file_list_path and use concat to strich them to a single file
    string concat_command = "ffmpeg -y -f concat -safe 0 -i " + edit_file_list_path + " -vcodec copy " +  dest_file;
    LOG_I(TAG, "concat_command %s", concat_command.c_str());
    system_execute("EDIT_INWV", concat_command);
    file_fd_sync(dest_file);
    LOG_I( TAG, " %s file_size %d", dest_file.c_str(), GetFileSize(dest_file) );

    clean_temp_chunks(all_chunks);
    return true;
}

bool apply_partial_privacy_and_save_video(string src_file, string dest_path, device_mode_t &device_mode, int cam_num,
        bool ldFile, int video_length_sec = 60) {
    //Check for existance of input videofile
    if (GetFileSize (src_file) < 0) {
        LOG_E (TAG, "GetFileSize failed for input file %s", src_file.c_str());
        return false;
    }

    string folder_name, file_name;
    if (!get_folder_file_names(src_file, folder_name, file_name)) {
        LOG_E(TAG, "Failed to get folder and file names from given path");
        return false;
    }

    //intermediate file is created after editing, which is later encrypted & copied to circular_buffer folder
    string intermediate_path = "/dev/shm";
    bool edited = false;
    bool copied = false;

    /* Not checking for video_encryption config and decrypting unconditionally to avoid failure in partial files */
    /* Decrypt the encrypted file first and then apply blackout for privacy */
    string dest_file_decrypted = intermediate_path + "/" + "decrypted_" + file_name;
    bool copy_status = true;
    int copy_retry = 0;
    bool copy_done = false;

    do {
        pid_t pid = fork();
        if (pid < 0 ) {
            LOG_C (TAG, "%s cannot create a child. Fork Status %d",__func__, pid);
        } else if (pid == 0) {
            copy_status = reoperate_if_video_and_copy(src_file, dest_file_decrypted, true, 0644, true);
            _exit(0);
        } else {
            task_status_t task_status = nd_set_timeout_for_task(pid, ND_REOPERATE_MAX_TIMEOUT);
            if (task_status != TASK_STATUS_SUCCESS)
            {
                LOG_E (TAG, "reoperate_if_video_and_copy was not succesful, status %d", task_status);
                copy_status =false;
            }
            else
            {
                LOG_I (TAG, "reoperate_if_video_and_copy was succesful, status %d", task_status);
                copy_status =true;
            }
        }

        if (false == copy_status) {
            LOG_E(TAG, "NDC: failed to decrypt file %s to %s; ignoring this request",
                    src_file.c_str(), dest_file_decrypted.c_str());
            file_delete(dest_file_decrypted.c_str());
            copy_retry++;
            copy_done = false;
            sleep(5);
        } else {
            copy_done = true;
            break;
        }
    } while(copy_retry < 2);

    if (copy_done == false) {
        LOG_E(TAG, "file decryption failed : %s to %s", src_file.c_str(), dest_file_decrypted.c_str());
        file_delete(src_file);
        return false;
    }
    LOG_I(TAG, "Decryption success; proceeding for blackout after decrypting");
    file_delete(src_file);
    //partial blackout
    edited = blackout_privacy_and_save_video(dest_file_decrypted, intermediate_path, device_mode, cam_num, ldFile, video_length_sec);
    if (!edited)
        return false;

    //copy and encrypt
    copied = file_copy_wrapper(dest_file_decrypted, (CIRCULAR_BUFFER_PATH + "/" + file_name));

    return copied;
}

static bool remove_files(string fname)
{
    bool ret = true;
    string folder_name, file_prefix;
    vector<string> vec;

    if (!get_folder_file_names(fname, folder_name, file_prefix)) {
        LOG_E(TAG, "Failed to get folder and file names from given path");
        return false;
    }
    LOG_I(TAG, "Delete files with prefix %s in folder %s\n", file_prefix.c_str(), folder_name.c_str());

    if (!get_files(folder_name, vec)) {
        LOG_E(TAG, "Failed to get file details from given path %s", folder_name.c_str());
        return false;
    }
    for (vector<string>::iterator iter= vec.begin(), end = vec.end(); iter!=end; iter++) {
        if ((*iter).find(file_prefix) != string::npos)
           if (!file_delete(folder_name + "/" + (*iter)))
               ret = false;
    }
    return ret;
}

static bool add_filenames_to_db (string vid_fname_prefix, string chm_fname_suffix, int cam_num, bool is_ld)
{
    string vid_fname;
    if (is_ld)
        vid_fname = vid_fname_prefix + video_file_extn + ld_extn;
    else
        vid_fname = vid_fname_prefix + video_file_extn;

    post_circular_buffer_add_file_db(vid_fname, CIRCULAR_BUFFER_TYPE_NORMAL, cam_num); //mp4

    if ((cam_num == DEVICE_CAMERA_POSITION_FRONT) && !is_ld && (dp_enabled == true)) {
        string vid_fname_dp = vid_fname_prefix + video_file_extn + dp_extn;
        post_circular_buffer_add_file_db(vid_fname_dp, CIRCULAR_BUFFER_TYPE_NORMAL, cam_num); //DP mp4
    }

    if ((cam_num == DEVICE_CAMERA_POSITION_FRONT) && !is_ld && (audio_enable == true)) {
        string audio_fname = vid_fname_prefix + audio_file_extn;
        post_circular_buffer_add_file_db(audio_fname, CIRCULAR_BUFFER_TYPE_TEXT, cam_num); // aac audio
    }

    return true;
}

string form_json_privacymodes(device_mode_t &device_mode) {
    int privacy_switch_count = 0;
    int privacy_switches = device_mode.individual_states_len;
    json_t *root_json_data = json_array();
    int prev_privacy = -1;
    while(privacy_switch_count < privacy_switches) {
        if (prev_privacy == device_mode.individual_states[privacy_switch_count].event_state) {
            privacy_switch_count++;
            continue;
        }
        json_t *element = json_object();
        json_object_set_new( element, "privacyState",
            json_integer(device_mode.individual_states[privacy_switch_count].event_state) );
        json_object_set_new( element, "eventTimeEpoch",
            json_integer(device_mode.individual_states[privacy_switch_count].event_time_epoch) );
        json_object_set_new( element, "eventTimeMonotonic",
            json_integer(device_mode.individual_states[privacy_switch_count].event_time_monotonic) );

        json_array_append_new(root_json_data, element);

        prev_privacy = device_mode.individual_states[privacy_switch_count].event_state;
        privacy_switch_count++;
    }

    char* req_params = json_dumps(root_json_data, 0);
    json_decref(root_json_data);

    if (req_params == NULL) {
        return "[]";
    }
    string output;
    output += req_params;
    if (req_params != NULL) {
        free(req_params);
    }
    return output;
}

string form_json_irledmodes(irled_mode_t &irled_mode)
{
    int irled_state_count = 0;
    int irled_states = irled_mode.irled_states_len;
    json_t *root_json_data = json_array();

    while (irled_state_count < irled_states) {
        json_t *element = json_object();
        json_object_set_new(element, "status",
            json_integer(irled_mode.irled_states[irled_state_count].status));
        json_object_set_new(element, "time",
            json_integer(irled_mode.irled_states[irled_state_count].time));

        json_array_append_new(root_json_data, element);

        irled_state_count++;
    }

    char* req_params = json_dumps(root_json_data, 0);
    json_decref(root_json_data);

    if (req_params == NULL)
        return "[]";

    string output;
    output += req_params;
    if (req_params != NULL)
        free(req_params);

    return output;
}

static const string get_metadata_json(int privacy_status, bool engine_idle,
    power_crank_levels_t crank_level, device_mode_t &device_mode_global,
    int flipflop, uint64_t epoch_time_micro,
    uint64_t raw_time_micro, bool hdmaps_mode_enabled, bool imu_data,
    irled_mode_t &irled_mode_current_session)
{
    string Json = "";

    stringstream ss_key, ss_value;
    ss_key << "end_time";
    ss_value << (epoch_time_micro/ONE_MILLI_IN_MICRO); // "endTime" in metadata needs to be in milli
    ctx.genmeta->update_genmeta_header(ss_key.str(), ss_value.str(), flipflop);
    ss_key.str("");
    ss_value.str("");
    ss_key << "end_time_micro";
    ss_value << epoch_time_micro;
    ctx.genmeta->update_genmeta_header(ss_key.str(), ss_value.str(), flipflop);

    ss_key.str("");
    ss_value.str("");
    ss_key << "end_time_raw_micro";
    ss_value << raw_time_micro;
    ctx.genmeta->update_genmeta_header(ss_key.str(), ss_value.str(), flipflop);

    LOG_I(TAG, "header.end_time_micro = %lld \t header.end_time_raw_micro: %lld", epoch_time_micro, raw_time_micro);

    ss_key.str("");
    ss_value.str("");
    ss_key << "offset";
    ss_value << ctx.saved_gps.system_timestamp - ctx.saved_gps.timestamp;
    ctx.genmeta->update_genmeta_header(ss_key.str(), ss_value.str(), flipflop);
    string drvid = get_driver_id();
    string drvid_v2 = get_driver_id_v2();
    ctx.genmeta->update_genmeta_header("drvId", drvid, flipflop);
    ctx.genmeta->update_genmeta_header("drvId_v2", drvid_v2, flipflop);
    ctx.genmeta->update_genmeta_header("rtcValid", ctx.rtcValid_string, flipflop);
    ctx.genmeta->update_genmeta_header("rtc_jump_from", ctx.rtc_jump_from_string, flipflop);
    ctx.genmeta->update_genmeta_header("rtc_jump_to", ctx.rtc_jump_to_string, flipflop);

    string ntwrk_info = get_network_info();
    if (ntwrk_info != "")
    {
        LOG_I (TAG,"get_network_info returned %s",ntwrk_info.c_str());
        ntwrk_info = "{" + ntwrk_info + "}";
        ctx.genmeta->update_genmeta_header("networkInfo", ntwrk_info, flipflop);
    }

    string privacy_mode = PRIVACY_OFF_STR;
    string privacy_mode_offduty = PRIVACY_OFF_STR;
    string privacy_mode_geofence = PRIVACY_OFF_STR;
    string idle_status = "0";
    string processing_mode = "0";
    string ignition_status = "0";
    if (privacy_status == PRIVACY_ON)
    {
        privacy_mode = PRIVACY_ON_STR;
    }
    else if(privacy_status == PRIVACY_MIXED)
    {
        privacy_mode = PRIVACY_MIXED_STR;
    }
    if (engine_idle)
    {
        idle_status = "1";
    }

    if (device_mode_global.privacy_status_offduty == PRIVACY_ON)
    {
        privacy_mode_offduty = PRIVACY_ON_STR;
    }
    else if(device_mode_global.privacy_status_offduty == PRIVACY_MIXED)
    {
        privacy_mode_offduty = PRIVACY_MIXED_STR;
    }

    if (device_mode_global.privacy_status_geofence == PRIVACY_ON)
    {
        privacy_mode_geofence = PRIVACY_ON_STR;
    }
    else if(device_mode_global.privacy_status_geofence == PRIVACY_MIXED)
    {
        privacy_mode_geofence = PRIVACY_MIXED_STR;
    }

    //If ignition was high during any duration of the minute or
    //the crank_level read is high, then set processing_mode = 0
    if ( (ctx.ignition_override == true) || (crank_level == CRANK_HIGH) )
    {
        LOG_I (TAG, "prcessing_mode = 0");
        processing_mode = "0"; //Normal mode
        ignition_status = "1";
    }
    else //TODO Do we have to consider CRANK_ERROR as HIGH on LOW
    {
        LOG_I (TAG, "prcessing_mode = 1");
        processing_mode = "1"; //Low power mode
        ignition_status = "0";
    }

    // Disable ignition override only if crank level is LOW.
    // If we disable ignition override after use regardless of crank level,
    // If current crank level is HIGH and if ignition was turned OFF in between
    // the next one minute session, we will end up setting processing_mode to 0
    // based on the check above which is not what we want.
    if (crank_level == CRANK_LOW)
    {
        LOG_I (TAG, "setting ignition_override to false");
        ctx.ignition_override = false;
    }
    else
    {
        LOG_I (TAG, "setting ignition_override to true");
        ctx.ignition_override = true;
    }

    string privacy_me = form_json_privacymodes(device_mode_global);

    LOG_I (TAG, "Setting privacy mode: %d and idle status: %d processing_mode %d in metadata",
            privacy_status, engine_idle, crank_level);

    ctx.genmeta->update_genmeta_header("privacyMode", privacy_mode, flipflop);
    ctx.genmeta->update_genmeta_header("privacyModeOffduty", privacy_mode_offduty, flipflop);
    ctx.genmeta->update_genmeta_header("privacyModeGeofence", privacy_mode_geofence, flipflop);
    ctx.genmeta->update_genmeta_header("idleStatus", idle_status, flipflop);
    ctx.genmeta->update_genmeta_header("processingMode", processing_mode, flipflop);
    ctx.genmeta->update_genmeta_header("ignitionStatus", ignition_status, flipflop);
    ctx.genmeta->update_genmeta_header("privacyModeEvents", privacy_me, flipflop);

    if (irled_mode_current_session.irled_status != -1) {
        int irled_status = irled_mode_current_session.irled_status;
        string irled_status_str = IRLED_OFF_STR;
        if (irled_status == IRLED_ON)
            irled_status_str = IRLED_ON_STR;
        else if(irled_status == IRLED_MIXED)
            irled_status_str = IRLED_MIXED_STR;

        string irled_states_str = form_json_irledmodes(irled_mode_current_session);

        LOG_I(TAG, "Setting irled status: %s, irled states: %s in metadata", irled_status_str.c_str(), irled_states_str.c_str());
        ctx.genmeta->update_genmeta_header("irled_status", irled_status_str, flipflop);
        ctx.genmeta->update_genmeta_header("irled_states", irled_states_str, flipflop);
    }

    string obd_proto_str = "";

    switch(obd_protocol)
    {
        case OBD_500K: obd_proto_str = "OBDII 500K";
            break;
        case OBD_250K: obd_proto_str = "OBDII 250K";
            break;
        case EXOBD_500K: obd_proto_str = "EXOBDII 500K";
            break;
        case EXOBD_250K: obd_proto_str = "EXOBDII 250K";
            break;
        case J1939_500K: obd_proto_str = "J1939 500K";
            break;
        case J1939_250K: obd_proto_str = "J1939 250K";
            break;
        default: obd_proto_str = "unknown";
    }

    ctx.genmeta->update_genmeta_header("protocol_info", obd_proto_str, flipflop);

    std::string str(obd_vin);
    ctx.genmeta->update_genmeta_header("vin", str, flipflop);

    std::string can_fw_ver_str(can_firmware_ver);
    ctx.genmeta->update_genmeta_header("can_firmware_ver", can_fw_ver_str, flipflop);

    std::string can_sn_str(can_sn);
    ctx.genmeta->update_genmeta_header("can_sn", can_sn_str, flipflop);

    std::string can_model_str(can_model);
    ctx.genmeta->update_genmeta_header("can_model", can_model_str, flipflop);

    std::stringstream can_connectivity_status_ss;
    can_connectivity_status_ss << can_connectivity_status;
    ctx.genmeta->update_genmeta_header("can_status", can_connectivity_status_ss.str(), flipflop);

    std::stringstream can_src_ss;
    can_src_ss << can_src;
	LOG_D(TAG, "can src string is %s can_src is %d", can_src_ss.str(),can_src);
    ctx.genmeta->update_genmeta_header("can_src", can_src_ss.str(), flipflop);

    std::string engine_status_str = "";

    if (engine_status == ENGINE_OFF) {
      engine_status_str = "0";
     } else if (engine_status == ENGINE_ON) {
    engine_status_str = "1";
    } else if (engine_status == CONNECTED_BUT_NO_DATA){
    engine_status_str = "3";
    } else {
    engine_status_str = "2";
    }
    engine_status = ENGINE_STATE_UNKNOWN;
    ctx.genmeta->update_genmeta_header("engine_status", engine_status_str, flipflop);
    LOG_I(TAG, "ctx.prevVideoName %s", ctx.prevVideoName.c_str());
    LOG_I(TAG, "ctx.presVideoName %s", ctx.presVideoName.c_str());

    pthread_mutex_lock( &nextVideoName_mutex );
    ctx.genmeta->update_genmeta_header("nextVideoName", ctx.nextVideoName, flipflop);
    LOG_I(TAG, "ctx.nextVideoName %s", ctx.nextVideoName.c_str());
    pthread_mutex_unlock( &nextVideoName_mutex );
    ctx.genmeta->get_meta_json(Json, flipflop, (epoch_time_micro/ONE_MILLI_IN_MICRO), hdmaps_mode_enabled, imu_data);
    return Json;
}

static void fill_sensor_data_size_map_healthstats(map< string, int > &sensor_data_size_map, int flipflop)
{
    sensor_data_size_map.insert(make_pair(HS_JSON_IMU, ctx.genmeta->get_imu_data_size(flipflop)));
    sensor_data_size_map.insert(make_pair(HS_JSON_OBD, ctx.genmeta->get_obd_data_size(flipflop)));
    sensor_data_size_map.insert(make_pair(HS_JSON_GPS, ctx.genmeta->get_gps_data_size(flipflop)));
    sensor_data_size_map.insert(make_pair(HS_JSON_ALERT, ctx.genmeta->get_usr_alert_size(flipflop)));
}

int get_value_sensor_map(map<string, int> sensor_data_size_map, string key)
{
    if(sensor_data_size_map.find(key) != sensor_data_size_map.end()){
        return sensor_data_size_map[key];
    }
    return -1;
}

bool ndmb_obddata_cb(ndmb_generic_msg_t *msg);

bool get_video_flip_register(void* args) {
    std::string cmd;
    VideoFlipRegisterInput* input = static_cast<VideoFlipRegisterInput*>(args);
    int cam_num = input->cam_num;

    if (nd_device_obj->getDeviceType() == eBagheera_3) {
        if (cam_num == DEVICE_CAMERA_POSITION_FRONT) {
            cmd = BAGHEERA3_OUTWARD_CAM_FLIP_REG_READ_COMMAND;
        } else if (cam_num == DEVICE_CAMERA_POSITION_BACK) {
            cmd = BAGHEERA3_INWARD_CAM_FLIP_REG_READ_COMMAND;
        } else if (cam_num == DEVICE_CAMERA_POSITION_LEFT) {
            cmd = BAGHEERA3_LEFT_CAM_FLIP_REG_READ_COMMAND;
        } else if (cam_num == DEVICE_CAMERA_POSITION_RIGHT) {
            cmd = BAGHEERA3_RIGHT_CAM_FLIP_REG_READ_COMMAND;
        } else if (cam_num >= EXT_CAMERA_POSITION_CH1 && cam_num < CAMERA_POSITION_MAXIMUM) {
            // currently this functionality is not implemented for cameras other than outward, inward, left and right
            return false;
        } else {
            LOG_E(TAG, "get_video_flip_register: invalid cam_num %d", cam_num);
            return false;
        }
    } else {
        return false;
    }

    FILE* fp = popen(cmd.c_str(), "r");
    if (!fp) {
        LOG_E(TAG, "get_video_flip_register: popen failed for cam_num %d, cmd: %s", cam_num, cmd.c_str());
        return false;
    }

    char buf[16] = {0};
    if (!fgets(buf, sizeof(buf), fp)) {
        LOG_E(TAG, "get_video_flip_register: fgets failed for cam_num %d", cam_num);
        pclose(fp);
        return false;
    }
    pclose(fp);

    input->hex_val = buf;
    if (!input->hex_val.empty() && input->hex_val.back() == '\n') {
        input->hex_val.pop_back();
    }

    return true;
}

string get_video_flip_register_output(int cam_num, string fname_no_extn) {
    VideoFlipRegisterInput input;
    input.cam_num = cam_num;
    input.hex_val = "";
    task_result_t task_result = nd_timed_task(get_video_flip_register, GET_FLIP_MIRROR_REGISTER_MAX_TIME, (void *)&input, "get video flip/mirror status");
    if (task_result == TASK_TIMEOUT) {
        LOG_E(TAG, "nd_timed_task for get_video_flip_register timedout");
        return "";
    } else {
        if (input.hex_val.empty()) {
            // Video flip/mirror register reading is only implemented for Bagheera3 devices and only for camera numbers 0, 1, 2, 3 as of now
            if ((nd_device_obj->getDeviceType() == eBagheera_3) && (cam_num >= DEVICE_CAMERA_POSITION_FRONT && cam_num <= DEVICE_CAMERA_POSITION_RIGHT)) {
                LOG_E(TAG, "Failed to read video flip/mirror register for file %s", fname_no_extn.c_str());
                nd_service_obj->send_err_msg(SM_E_NDC_VIDEO_FLIP_MIRROR_STATUS_READ_FAILED, cam_num, "Error reading video flip/mirror register for file " + fname_no_extn);
            }
        } else {
            unsigned int flip_status_val = 0;
            sscanf(input.hex_val.c_str(), "%x", &flip_status_val);
            // For outward cam, correct last 2 bits for flip/mirror register are 0x2 (binary "10") and for inward/side cams, correct last 2 bits for flip/mirror register are 0x0 (binary "00")
            if ((cam_num == DEVICE_CAMERA_POSITION_FRONT && (flip_status_val & 0x3) != 0x2) ||
                ((cam_num == DEVICE_CAMERA_POSITION_BACK || cam_num == DEVICE_CAMERA_POSITION_LEFT || cam_num == DEVICE_CAMERA_POSITION_RIGHT) && (flip_status_val & 0x3) != 0x0)) {
                LOG_E(TAG, "Video is not properly oriented - either flipped or mirrored for file %s", fname_no_extn.c_str());
                nd_service_obj->send_err_msg(SM_E_NDC_VIDEO_FLIP_MIRROR, cam_num, "Video is flipped/mirrored for file " + fname_no_extn);
            }
        }
        return input.hex_val;
    }
}

void fill_map_add_file_healthstats(string video_fname, string dest, int cam_num, int64_t starttime, int64_t endtime, int copied, string reason, int flipflop)
{
    json_t *video_jobj = json_object();
    json_t *ib_jobj = json_object();
    string fname_no_extn = "", fname = "", folder = "";
    if(!get_folder_file_names(video_fname, folder, fname)){
        LOG_E(TAG, "Invalid path: %s", video_fname.c_str());
    }
    if(!remove_extension_from_session(fname, fname_no_extn)){
        LOG_I(TAG, "file name doesn't contain any . : %s",fname.c_str());
    }
    string dest_fname = dest + "/" + fname_no_extn + video_file_extn;
    int duration = ctx.cam_session_stop_time[cam_num][flipflop] - ctx.cam_session_start_time[cam_num][flipflop];
    json_object_set_new( video_jobj, "starttime", json_integer(ctx.cam_session_start_time[cam_num][flipflop]) );
    json_object_set_new( video_jobj, "endtime", json_integer(ctx.cam_session_stop_time[cam_num][flipflop]) );
    json_object_set_new( video_jobj, "duration", json_integer(duration) );
    json_object_set_new( video_jobj, "size", json_integer(file_size(dest_fname)) );
    json_object_set_new( video_jobj, "frame_count", json_integer(g_session_frame_count[cam_num]));
    // Currently we are sending flip/mirror register status only for full session videos
    if (video_fname.find(partial_session_name) == string::npos) {
        string flip_mirror_status_hex = get_video_flip_register_output(cam_num, fname_no_extn);
        if (!flip_mirror_status_hex.empty()) {
            json_object_set_new(video_jobj, "video_flip_register", json_string(flip_mirror_status_hex.c_str()));
        }
    }
    json_object_set_new( ib_jobj, "copy_start", json_integer(starttime) );
    json_object_set_new( ib_jobj, "copy_end", json_integer(endtime) );
    json_object_set_new( ib_jobj, "copied", json_integer(copied) );
    json_object_set_new( ib_jobj, "fail_reason", json_string(reason.c_str()) );
    json_object_set_new( video_jobj, "sdcard", ib_jobj );
    send_video_message_healthstats(nd_service_obj, dest_fname, video_jobj);
}

void fill_map_add_file_audio_healthstats(string video_fname, string dest, int64_t starttime, int64_t endtime, int copied){
    json_t *audio_jobj = json_object();
    json_t *ib_jobj = json_object();
    string fname_no_extn = "", fname = "", folder = "";
    if(!get_folder_file_names(video_fname, folder, fname)){
        LOG_E(TAG, "Invalid path: %s", video_fname.c_str());
    }
    if(!remove_extension_from_session(fname, fname_no_extn)){
        LOG_I(TAG, "file name doesn't contain any . : %s",fname.c_str());
    }
    string dest_fname = dest + "/" + fname_no_extn + audio_file_extn;
    json_object_set_new( audio_jobj, "size", json_integer(file_size(dest_fname)) );
    json_object_set_new( ib_jobj, "copy_start", json_integer(starttime) );
    json_object_set_new( ib_jobj, "copy_end", json_integer(endtime) );
    json_object_set_new( ib_jobj, "copied", json_integer(copied) );
    json_object_set_new( audio_jobj, "sdcard", ib_jobj );
    send_audio_message_healthstats(nd_service_obj, fname_no_extn, audio_jobj);
}

static void send_obs_gen_message_healthstats(string meta_fname, map<string, int> sensor_data_size_map)
{
    json_t *root = json_object();
    json_t *observation = json_object();
    json_t *sensor = json_object();
    string folder = "", fname = "", session = "";
    char* req_params = NULL;
    if(!get_folder_file_names(meta_fname, folder, fname)){
        LOG_E(TAG, "Failed to get folder and file names from given path");
    }
    if(!get_session_name_from_string(fname, session)){
        LOG_E(TAG, "Invalid filename: %s",fname.c_str());
    }
    json_object_set_new( root, "session", json_string(session.c_str()) );
    json_object_set_new( observation, "modified_time", json_integer(get_system_time()) );
    json_object_set_new( observation, "stage", json_string("metadata") );
    json_object_set_new( observation, "location", json_string(folder.c_str()) );
    json_object_set_new( observation, "size", json_integer(file_size(meta_fname)) );
    json_object_set_new( sensor, HS_JSON_IMU.c_str(), json_integer(get_value_sensor_map(sensor_data_size_map, HS_JSON_IMU)) );
    json_object_set_new( sensor, HS_JSON_OBD.c_str(), json_integer(get_value_sensor_map(sensor_data_size_map, HS_JSON_OBD)) );
    json_object_set_new( sensor, HS_JSON_GPS.c_str(), json_integer(get_value_sensor_map(sensor_data_size_map, HS_JSON_GPS)) );
    json_object_set_new( sensor, "validGPSEntries", json_integer(valid_GPS_entries) );
    json_object_set_new( observation, "sensor", sensor );
    json_object_set_new( observation, HS_JSON_ALERT.c_str(), get_value_sensor_map(sensor_data_size_map, HS_JSON_ALERT) > 0 ? json_string("yes") : json_string("no") );
    json_object_set_new( root, "observation", observation );
    req_params = json_dumps(root, 0);
    if(req_params == NULL){
        LOG_E(TAG,"JSON creation failed for HS message");
        json_decref(root);
        return;
    }
    int length = strlen(req_params);
    nd_service_obj->send_msg_healthstats(req_params, length);
    json_decref(root);
    free(req_params);
}

static void copy_metadata_and_chm_for_scheduler (string folder_name, string file_prefix)
{
    string destination = ND_INPUT_PATH +"/" + file_prefix + video_metadata_extn;
    string source = folder_name + "/" + file_prefix + ".txt";

    LOG_I (TAG,"copy_metadata_and_chm_for_scheduler: source: %s, dest: %s", source.c_str(), destination.c_str());

    if (!file_copy(source, destination, true, 0644, false)){
        file_delete(source);
        LOG_E(TAG, "copy_metadata_and_chm_for_scheduler: file copy failed");
        return;
    }
    file_delete(source);

    string checksum;
    if(!calculate_md5sum(destination, checksum)) {
        LOG_E(TAG, "copy_metadata_and_chm_for_scheduler: Failed to get checksum for file %s",destination.c_str());
        return;
    }

    string dest_chm_file_path = ND_INPUT_PATH + "/" + file_prefix + ".chm." + checksum;

    LOG_I( TAG, "FINAL CS FILE for partial file %s", dest_chm_file_path.c_str() );
    ofstream CS_file;
    CS_file.open (dest_chm_file_path.c_str());
    CS_file.close();

    vector<string> vec;
    LOG_I(TAG, "Change permissions to 666 for files with prefix %s in folder %s\n", file_prefix.c_str(), ND_INPUT_PATH.c_str());

    if(!get_files(ND_INPUT_PATH, vec)) {
        LOG_E(TAG, "copy_metadata_and_chm_for_scheduler:Failed to get file details from given path %s", ND_INPUT_PATH.c_str());
        return;
    }
    for( vector<string>::iterator iter= vec.begin(), end = vec.end();
                         iter!=end; iter++ ) {
        if((*iter).find(file_prefix) != string::npos)
           if(chmod((ND_INPUT_PATH + "/" + (*iter)).c_str(), S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH) == -1) {
               LOG_E(TAG, "Failed to change file permissions :: %s" , (ND_INPUT_PATH + "/" + (*iter)).c_str());
               return;
           }
    }
    file_fd_sync(destination);
}

static void copy_metadata_for_obs_upload(string folder_name, string file_prefix, const string &json_contents)
{
    string obs_name = "summary_LD.json";
    string obs_zip_name = folder_name + "/" + file_prefix + METADATA_ZIP_EXT;
    string obs_zip_checksum;

    /* If ZIP already present, possibility of corruption, hence deleting. Check ticket: DT-376 */
    if(file_is_present(obs_zip_name)) {
        if(!file_delete(obs_zip_name)){
            LOG_E(TAG, "File delete failed on %s", obs_zip_name.c_str());
        }
        nd_service_obj->send_err_msg(SM_E_NDC_PARTIAL_ZIP_DELETE, 0, "partial zip deleted");
    }

    Zipper zipper(obs_zip_name);
    std::vector<char> charvect(json_contents.begin(), json_contents.end());
    boost::interprocess::basic_vectorstream<std::vector<char>> input_data(charvect);
    bool zip_add_status = zipper.add(input_data, obs_name);
    zipper.close();
    if(!zip_add_status){
        LOG_E(TAG, "Failed to zip metadata file :: %s", obs_name.c_str());
        file_delete(obs_zip_name);
        return;
    }
    if(!calculate_md5sum(obs_zip_name, obs_zip_checksum)) {
        LOG_E(TAG, "Failed to get checksum for metadata obs file %s", obs_zip_name.c_str());
        file_delete(obs_zip_name);
        return;
    }

    string dest_obs_name = METADATA_OBS_PATH + "/" + file_prefix + "_" + obs_zip_checksum + METADATA_ZIP_EXT;
    if(ND_AUTH_SUCCESS != nd_file_operate_to_file(obs_zip_name.c_str(), dest_obs_name.c_str())){
        LOG_E(TAG, "File operate failed on %s", dest_obs_name.c_str());
    }
    if(chmod(dest_obs_name.c_str(), S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH) == -1) {
        LOG_E(TAG, "Failed to change obs zip file permissions :: %s" , dest_obs_name.c_str());
        LOG_I(TAG, "Deleting obs zip as scheduler will be unable to delete due to chmod failure");
        file_delete(dest_obs_name);
    }
    if(!file_delete(obs_zip_name)){
        LOG_E(TAG, "File delete failed on %s", obs_zip_name.c_str());
    }
}

static bool generate_metadata_and_chm_files (string filename, const string &json_contents, string &chm_fname_suffix)
{
    ofstream meta_file;
    bool ret = true;
    string metadata_fname = filename + video_metadata_extn;

    meta_file.open (metadata_fname.c_str());
    meta_file << json_contents;
    meta_file.close();

    stringstream cs_command;
    string checksum;
    string folder_name, file_prefix;
    vector<string> vec;

    if(!calculate_md5sum(filename + video_metadata_extn, checksum)) {
        LOG_E(TAG, "Failed to get checksum for file %s", (filename + video_metadata_extn).c_str());
        return false;
    }

    chm_fname_suffix = checksum;

    cs_command << filename << ".chm." <<  chm_fname_suffix;
    LOG_I( TAG, "FINAL CS FILE %s", cs_command.str().c_str() );

    ofstream CS_file;
    CS_file.open (cs_command.str().c_str());
    CS_file.close();
    if(!get_folder_file_names(filename, folder_name, file_prefix)) {
        LOG_E(TAG, "Failed to get folder and file names from given path");
        return false;
    }
    LOG_I(TAG, "Change permissions to 666 for files with prefix %s in folder %s\n", file_prefix.c_str(), folder_name.c_str());

    if(!get_files(folder_name, vec)) {
        LOG_E(TAG, "Failed to get file details from given path %s", folder_name.c_str());
        return false;
    }
    for( vector<string>::iterator iter= vec.begin(), end = vec.end();
                         iter!=end; iter++ ) {
        if((*iter).find(file_prefix) != string::npos)
           if(chmod((folder_name + "/" + (*iter)).c_str(), S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH) == -1) {
               LOG_E(TAG, "Failed to change file permissions :: %s" , (folder_name + "/" + (*iter)).c_str());
               ret = false;
           }
    }
    file_fd_sync(metadata_fname);
    // Copy metadata file to inertial_obs
    if(ret){
        copy_metadata_for_obs_upload(folder_name, file_prefix, json_contents);
    }
    return ret;
}

bool audio_partial_encode_task(void *args) {
    string audio_file_pcm = (char *)args;
    string source_path = AUDIO_PARTIAL_PCM_FILE_BASE_PATH + "/" + audio_file_pcm ;
    string destination_path = source_path.substr(0,source_path.find(AUDIO_PARTIAL_FILE_SUFFIX)) + ".aac"; ;
    LOG_I(TAG, "audio_partial_encode_task() src: %s  ", source_path.c_str() );
    LOG_I(TAG, "audio_partial_encode_task() dest: %s  ", destination_path.c_str() );
    return Audio::audio_encode(source_path , destination_path);

}

bool audio_encode_task(void *args) {

    if (args == nullptr) {
        LOG_E(TAG, "audio_encode_task: args is null");
        return false;
    }

    char* fname = (char*)args;

    if(ctx.audio) {
        return ctx.audio->audio_encode(AUDIO_PCM_FILE_PATH, AUDIO_ENCODE_FILE_PATH + fname + audio_file_extn);
    }
    return false;

}

static bool audio_file_gen(int flipflop, string audio_file_name)
{
    if (audio_enable == true) {
        // Audio data encode
        bool audio_record_failed = false;
        bool audio_encode_failed = false;

        file_delete(AUDIO_PCM_FILE_PATH);

        if (!(ctx.meta_buff[flipflop].fill_audio(AUDIO_PCM_FILE_PATH))) {
            LOG_E(TAG, "Could not create audio pcm file. Skipping encoding");
            audio_record_failed = true;
        }

        if (audio_file_name == "") {
            LOG_E(TAG, "Audio file name %s is empty", audio_file_name.c_str());
            return false;
        }

        // Check if PCM file has 0 size
        if (GetFileSize(AUDIO_PCM_FILE_PATH) == 0) {
            LOG_E(TAG, "Audio pcm file is of 0 size for session %s", audio_file_name.c_str());
            nd_service_obj->send_err_msg(SM_E_NDC_AUDIO_RECORD_FAILED, NDService::UNUSED_ERR_AUX_CODE,
                std::string("Audio pcm file has 0 size for session ") + audio_file_name);
            audio_record_failed = true;
        }

        bool record_privacy;
        if (audio_record_failed == false) {
            record_privacy = false;
            check_audio_privacy(record_privacy);
            if (record_privacy == false) {
                task_result_t timed_task_result;
                const char* audio_fname = audio_file_name.c_str();
                timed_task_result = nd_timed_task(audio_encode_task, AUDIO_ENCODE_TIME_MAX, (void*)audio_fname, "audio_encode_task");
                if (timed_task_result != TASK_SUCCESS) {
                    LOG_E (TAG, "nd_timed_task for audio_encode failed");
                }
            } else {
                LOG_I(TAG, "Not encoding audio due to record privacy being enabled");
            }
        }

        // Check if AAC file has 0 size
        if (GetFileSize(AUDIO_ENCODE_FILE_PATH + audio_file_name + audio_file_extn) == 0) {
            LOG_E(TAG, "Audio aac file is of 0 size for session %s", (audio_file_name + audio_file_extn).c_str());
            nd_service_obj->send_err_msg(SM_E_NDC_AUDIO_ENCODE_FAILED, NDService::UNUSED_ERR_AUX_CODE,
                std::string("Audio aac file has 0 size for session ") + audio_file_name);
            audio_encode_failed = true;
        }

        stringstream ss;
        string filename_prefix = audio_file_name;
        ss << ctx.base_path_cam0 << "/" << filename_prefix;

        if (audio_record_failed == false && audio_encode_failed == false && record_privacy == false) {
            if (audio_encryption) {
                if (ND_AUTH_SUCCESS != nd_file_operate_to_file((AUDIO_ENCODE_FILE_PATH + filename_prefix + audio_file_extn).c_str(),
                                                (ss.str() + audio_file_extn).c_str())) {
                    LOG_E(TAG, "Audio File operate failed.");
                }
            } else if (!file_copy((AUDIO_ENCODE_FILE_PATH + filename_prefix + audio_file_extn),
                            (ss.str() + audio_file_extn), true, 0644, false)) {
                LOG_E(TAG, "Audio file_copy failed");
            }
        }

        if (file_is_present(ctx.audio->audio_pcm_partial_file_base_path) == true) {
            pthread_mutex_lock(&audio_partial_file_mutex);
            file_delete(ctx.audio->audio_pcm_partial_file_base_path);
            LOG_I(TAG, "old partial pcm file deleted: %s", ctx.audio->audio_pcm_partial_file_base_path.c_str());
            ctx.audio->audio_pcm_partial_file_base_path =  ctx.audio->next_audio_pcm_partial_file_base_path ;
            LOG_I(TAG, "next partial pcm file is: %s", ctx.audio->audio_pcm_partial_file_base_path.c_str());
            pthread_mutex_unlock(&audio_partial_file_mutex);
        } else {
            LOG_E(TAG, "partial pcm file Not Present: %s", ctx.audio->audio_pcm_partial_file_base_path.c_str());
        }
        file_delete(AUDIO_ENCODE_FILE_PATH + filename_prefix + audio_file_extn);
    }
}

void clear_disabled_camera_bits(int &copy_status_for_cams, int &edit_status_for_cams, int &remove_status_for_cams, bool is_partial_session = false)
{
    if (is_partial_session) {
        if (get_cams_enabled() != true) {
            LOG_E (TAG, "No configuration info regarding cameras enabled");
        }
    }

    for (int i = DEVICE_CAMERA_POSITION_BACK; i <= DEVICE_CAMERA_POSITION_RIGHT; i++) {
        if (!cams_enabled[i]) {
            clear_bit(copy_status_for_cams, i);
            clear_bit(edit_status_for_cams, i);
            healthstat_reason[i] = "";
        }
    }
    if (!cams_enabled[DEVICE_CAMERA_POSITION_DMS]) {
        clear_bit(copy_status_for_cams, DEVICE_CAMERA_POSITION_DMS);
        clear_bit(edit_status_for_cams, DEVICE_CAMERA_POSITION_DMS);
        healthstat_reason[DEVICE_CAMERA_POSITION_DMS] = "";
    }
}

bool get_copy_status(device_mode_t device_mode_global, bool &session_contains_usr_alert)
{
    copy_status_for_cams = default_status_value;
    session_contains_usr_alert = false;
    bool need_to_copy = false;
    edit_status_for_cams = 0x0;
    remove_status_for_cams = default_status_value;
    for (int i = DEVICE_CAMERA_POSITION_FRONT; i <= DEVICE_CAMERA_POSITION_RIGHT; i++) {
        healthstat_reason[i] = "";
    }
    healthstat_reason[DEVICE_CAMERA_POSITION_DMS] = "";

    // Engine idle case
    if (device_mode_global.engine_idle) {
        copy_status_for_cams = 0x0;
        remove_status_for_cams = 0x0;
        for (int i = DEVICE_CAMERA_POSITION_FRONT; i <= DEVICE_CAMERA_POSITION_RIGHT; i++) {
            healthstat_reason[i] += "idle";
        }
        healthstat_reason[DEVICE_CAMERA_POSITION_DMS] += "idle";
        LOG_I(TAG, "Not copying because of engine_idle");
    }

    // User alert case - user_alert_copy[] is already set correctly in USER_ALERT msg handler
    // based on whether geofence was active at the moment user pressed the button
    for (int i = DEVICE_CAMERA_POSITION_FRONT; i <= DEVICE_CAMERA_POSITION_RIGHT; i++) {
        if (user_alert_copy[i]) {
            session_contains_usr_alert = true;
            break;
        }
    }
    if (session_contains_usr_alert && ctx.privacy_params.save_user_alert_video) {
        need_to_copy = true;
    }


    //if user has generated an alert during engine idle, we have to ignore engine idle once
    if (need_to_copy && device_mode_global.privacy_status_geofence == PRIVACY_OFF) {
        for (int i = DEVICE_CAMERA_POSITION_FRONT; i <= DEVICE_CAMERA_POSITION_RIGHT; i++) {
            set_bit(copy_status_for_cams, i);
            set_bit(remove_status_for_cams, i);
            LOG_I(TAG, "Will copy CAM%d file because of user alert", i);
        }
        set_bit(copy_status_for_cams, DEVICE_CAMERA_POSITION_DMS);
        set_bit(remove_status_for_cams, DEVICE_CAMERA_POSITION_DMS);
        LOG_I(TAG, "Will copy CAM8 file because of user alert");
        clear_disabled_camera_bits(copy_status_for_cams, edit_status_for_cams, remove_status_for_cams);
        return true;
    }

    // Highg alert case
    for (int i = DEVICE_CAMERA_POSITION_FRONT; i <= DEVICE_CAMERA_POSITION_RIGHT; i++) {
        if (highg_alert_copy[i]) {
            set_bit(copy_status_for_cams, i);
            set_bit(remove_status_for_cams, i);
            LOG_I(TAG, "Will copy CAM%d file because of highg alert", i);
        }
    }
    if (highg_alert_copy[DEVICE_CAMERA_POSITION_DMS]) {
        set_bit(copy_status_for_cams, DEVICE_CAMERA_POSITION_DMS);
        set_bit(remove_status_for_cams, DEVICE_CAMERA_POSITION_DMS);
        LOG_I(TAG, "Will copy CAM8 file because of highg alert");
    }
    
    // LPW no record case
    if (file_is_present(lpw_no_record_persistent_file)) {
        copy_status_for_cams = 0x0;
        edit_status_for_cams = 0x0;
        remove_status_for_cams = default_status_value;
        for (int i = DEVICE_CAMERA_POSITION_BACK; i <= DEVICE_CAMERA_POSITION_RIGHT; i++){
            healthstat_reason[i] = "lpw_no_record";
        }
        healthstat_reason[DEVICE_CAMERA_POSITION_DMS] = "lpw_no_record";
        LOG_I(TAG, "Will delete all CAM files because of LPW NO RECORD");
        clear_disabled_camera_bits(copy_status_for_cams, edit_status_for_cams, remove_status_for_cams);
        return true;
    }

    // Handle enhanced privacy for all cameras
    if (ctx.privacy_params.enhanced_privacy) {

	    if(device_mode_global.privacy_status_geofence != PRIVACY_OFF) {
		    if (device_mode_global.privacy_status_geofence == PRIVACY_ON) {
			    copy_status_for_cams = 0x0;
			    edit_status_for_cams = 0x0;
			    remove_status_for_cams = default_status_value;
			    for (int i = DEVICE_CAMERA_POSITION_FRONT; i <= DEVICE_CAMERA_POSITION_RIGHT; i++) {
				    healthstat_reason[i] += "geofence_privacy";
			    }
			    healthstat_reason[DEVICE_CAMERA_POSITION_DMS] += "geofence_privacy";
			    LOG_I(TAG, "Will delete all CAM files because Geofence mode is enabled");
		    } else { //geofence mixed
			    /* Complete session is in offduty mode */
			    if (device_mode_global.privacy_status_offduty == PRIVACY_ON) {
				    copy_status_for_cams = 0x0;
				    edit_status_for_cams = 0x0;
				    remove_status_for_cams = default_status_value;
				    for (int i = DEVICE_CAMERA_POSITION_FRONT; i <= DEVICE_CAMERA_POSITION_RIGHT; i++) {
					    healthstat_reason[i] += "geofence_privacy+personal_privacy";
				    }
				    healthstat_reason[DEVICE_CAMERA_POSITION_DMS] += "geofence_privacy+personal_privacy";
				    LOG_I(TAG, "Will delete all CAM files because OFF-duty mode is enabled");
			    } else { //offduty mixed + + geofence mixed/off + other cases
				    // Handle inward and DMS camera
				    clear_bit(copy_status_for_cams, DEVICE_CAMERA_POSITION_BACK);
				    clear_bit(copy_status_for_cams, DEVICE_CAMERA_POSITION_DMS);
				    clear_bit(edit_status_for_cams, DEVICE_CAMERA_POSITION_BACK);
				    clear_bit(edit_status_for_cams, DEVICE_CAMERA_POSITION_DMS);
				    set_bit(remove_status_for_cams, DEVICE_CAMERA_POSITION_BACK);
				    set_bit(remove_status_for_cams, DEVICE_CAMERA_POSITION_DMS);

				    if(device_mode_global.privacy_status == PRIVACY_MIXED) {



					    for (int i = DEVICE_CAMERA_POSITION_FRONT; i <= DEVICE_CAMERA_POSITION_RIGHT; i++) {
						    if (i != DEVICE_CAMERA_POSITION_BACK) {
							    set_bit(edit_status_for_cams, i);
							    set_bit(copy_status_for_cams, i);
							    set_bit(remove_status_for_cams, i);
							    healthstat_reason[i] += "edit";
							    LOG_I(TAG, "Will edit CAM%d files because of Enhanced Privacy + Offduty Privacy Mode", i);
						    }
					    }
					    healthstat_reason[DEVICE_CAMERA_POSITION_BACK] += "enhanced_privacy+personal_privacy+geofence_privacy";
					    healthstat_reason[DEVICE_CAMERA_POSITION_DMS] += "enhanced_privacy+personal_privacy+geofence_privacy";
				    } else {
					    copy_status_for_cams = 0x0;
					    edit_status_for_cams = 0x0;
					    remove_status_for_cams = default_status_value;
					    for (int i = DEVICE_CAMERA_POSITION_FRONT; i <= DEVICE_CAMERA_POSITION_RIGHT; i++) {
						    healthstat_reason[i] += "geofence_privacy+personal_privacy";
					    }
					    healthstat_reason[DEVICE_CAMERA_POSITION_DMS] += "geofence_privacy+personal_privacy";
					    LOG_I(TAG, "Will delete all CAM files because OFF-duty mode is enabled");

				    
				    }
				}
		    }
	    } else { // geofence off
		    if (device_mode_global.privacy_status_offduty == PRIVACY_ON) {
			    copy_status_for_cams = 0x0;
			    edit_status_for_cams = 0x0;
			    remove_status_for_cams = default_status_value;
			    for (int i = DEVICE_CAMERA_POSITION_FRONT; i <= DEVICE_CAMERA_POSITION_RIGHT; i++) {
				    healthstat_reason[i] += "personal_privacy";
			    }
			    healthstat_reason[DEVICE_CAMERA_POSITION_DMS] += "personal_privacy";
			    LOG_I(TAG, "Will delete all CAM files because OFF-duty mode is enabled");
		    } else { //offduty mixed + + geofence mixed/off + other cases
			    // Handle inward and DMS camera
			    clear_bit(copy_status_for_cams, DEVICE_CAMERA_POSITION_BACK);
			    clear_bit(copy_status_for_cams, DEVICE_CAMERA_POSITION_DMS);
			    clear_bit(edit_status_for_cams, DEVICE_CAMERA_POSITION_BACK);
			    clear_bit(edit_status_for_cams, DEVICE_CAMERA_POSITION_DMS);
			    set_bit(remove_status_for_cams, DEVICE_CAMERA_POSITION_BACK);
			    set_bit(remove_status_for_cams, DEVICE_CAMERA_POSITION_DMS);

			    // Handle cameras other than inward and DMS
			    if (device_mode_global.privacy_status_offduty == PRIVACY_MIXED) {
				    /* session has both enhanced privacy and offduty privacy */
				    for (int i = DEVICE_CAMERA_POSITION_FRONT; i <= DEVICE_CAMERA_POSITION_RIGHT; i++) {
					    if (i != DEVICE_CAMERA_POSITION_BACK) {
						    set_bit(edit_status_for_cams, i);
						    set_bit(copy_status_for_cams, i);
						    set_bit(remove_status_for_cams, i);
						    healthstat_reason[i] += "enhanced_privacy+personal_privacy";
						    LOG_I(TAG, "Will edit CAM%d files because of Enhanced Privacy + Offduty Privacy Mode", i);
					    }
				    }
				    healthstat_reason[DEVICE_CAMERA_POSITION_BACK] += "enhanced_privacy+personal_privacy";
				    healthstat_reason[DEVICE_CAMERA_POSITION_DMS] += "enhanced_privacy+personal_privacy";
				    LOG_I(TAG, "Will delete Inward CAM files because of Enhanced Privacy + Offduty privacy");
			    } else {
				    /* complete session is in enhanced privacy mode */
				    for (int i = DEVICE_CAMERA_POSITION_FRONT; i <= DEVICE_CAMERA_POSITION_RIGHT; i++) {
					    if (i != DEVICE_CAMERA_POSITION_BACK) {
						    clear_bit(edit_status_for_cams, i);
						    set_bit(copy_status_for_cams, i);
						    set_bit(remove_status_for_cams, i);
						    healthstat_reason[i] += "enhanced_privacy";
						    LOG_I(TAG, "Will copy CAM%d files because of Enhanced Privacy", i);
					    }
				    }
				    healthstat_reason[DEVICE_CAMERA_POSITION_BACK] += "enhanced_privacy";
				    healthstat_reason[DEVICE_CAMERA_POSITION_DMS] += "enhanced_privacy";
				    LOG_I(TAG, "Will delete Inward CAM files because of Enhanced Privacy");
			    }
		    }
	    }
	    clear_disabled_camera_bits(copy_status_for_cams, edit_status_for_cams, remove_status_for_cams);
	    return true;
    }

    // Handle Geofence privacy for all cameras (non-enhanced privacy)
    if (device_mode_global.privacy_status_geofence != PRIVACY_OFF) {
        if (device_mode_global.privacy_status_geofence == PRIVACY_ON) {
            copy_status_for_cams = 0x0;
            edit_status_for_cams = 0x0;
            remove_status_for_cams = default_status_value;
            for (int i = DEVICE_CAMERA_POSITION_FRONT; i <= DEVICE_CAMERA_POSITION_RIGHT; i++) {
                healthstat_reason[i] += "geofence_privacy";
            }
            healthstat_reason[DEVICE_CAMERA_POSITION_DMS] += "geofence_privacy";
            LOG_I(TAG, "Will delete all CAM files because Geofence mode is enabled");
        } else {
            if (device_mode_global.privacy_status_offduty == PRIVACY_ON) {
                copy_status_for_cams = 0x0;
                edit_status_for_cams = 0x0;
                remove_status_for_cams = default_status_value;
                for (int i = DEVICE_CAMERA_POSITION_FRONT; i <= DEVICE_CAMERA_POSITION_RIGHT; i++) {
                    healthstat_reason[i] += "personal_privacy+geofence_privacy";
                }
                healthstat_reason[DEVICE_CAMERA_POSITION_DMS] += "personal_privacy+geofence_privacy";
                LOG_I(TAG, "Will delete all CAM files because Personal privacy is enabled");
            } else {
                // offduty off/mixed + geofence mixed + REGULAR PRIVACY MIXED/OFF
                if (device_mode_global.privacy_status != PRIVACY_ON) {
                    copy_status_for_cams = default_status_value;
                    edit_status_for_cams = default_status_value;
                    remove_status_for_cams = default_status_value;
                    for (int i = DEVICE_CAMERA_POSITION_FRONT; i <= DEVICE_CAMERA_POSITION_RIGHT; i++) {
                        healthstat_reason[i] += "edit";
                    }
                    healthstat_reason[DEVICE_CAMERA_POSITION_DMS] += "edit";
                    LOG_I(TAG, "Will edit all CAM files because of multiple privacy in geo-fence mode");
                } else {
                    // regular on + geofence mixed and offduty off
                    for (int i = DEVICE_CAMERA_POSITION_FRONT; i <= DEVICE_CAMERA_POSITION_RIGHT; i++) {
                        if (!ctx.privacy_params.cam_privacy[i]) {
                            set_bit(edit_status_for_cams, i);
                            set_bit(copy_status_for_cams, i);
                            set_bit(remove_status_for_cams, i);
                            if (i == DEVICE_CAMERA_POSITION_BACK) {
                                set_bit(edit_status_for_cams, DEVICE_CAMERA_POSITION_DMS);
                                set_bit(copy_status_for_cams, DEVICE_CAMERA_POSITION_DMS);
                                set_bit(remove_status_for_cams, DEVICE_CAMERA_POSITION_DMS);
                            }
                            healthstat_reason[i] += "edit";
                            LOG_I(TAG, "Will edit CAM%d file because of multiple privacy in geo-fence mode", i);
                        } else {
                            clear_bit(copy_status_for_cams, i);
                            clear_bit(edit_status_for_cams, i);
                            set_bit(remove_status_for_cams, i);
                            if (i == DEVICE_CAMERA_POSITION_BACK) {
                                clear_bit(copy_status_for_cams, DEVICE_CAMERA_POSITION_DMS);
                                clear_bit(edit_status_for_cams, DEVICE_CAMERA_POSITION_DMS);
                                set_bit(remove_status_for_cams, DEVICE_CAMERA_POSITION_DMS);
                            }
                            healthstat_reason[i] += "personal_privacy+geofence_privacy+regular_privacy";
                            LOG_I(TAG, "Will delete CAM%d file because of multiple privacy", i);
                        }
                    }
                }
            }
        }
        clear_disabled_camera_bits(copy_status_for_cams, edit_status_for_cams, remove_status_for_cams);
        return true;
    }

    // Handle OFF-duty privacy for all cameras
    if (device_mode_global.privacy_status_offduty != PRIVACY_OFF) {
        if (device_mode_global.privacy_status_offduty == PRIVACY_ON) {
            copy_status_for_cams = 0x0;
            edit_status_for_cams = 0x0;
            remove_status_for_cams = default_status_value;
            for (int i = DEVICE_CAMERA_POSITION_FRONT; i <= DEVICE_CAMERA_POSITION_RIGHT; i++) {
                healthstat_reason[i] += "personal_privacy";
            }
            healthstat_reason[DEVICE_CAMERA_POSITION_DMS] += "personal_privacy";
            LOG_I(TAG, "Will delete all CAM files because OFF-duty mode is enabled");
        } else {
            if (device_mode_global.privacy_status == PRIVACY_MIXED) {
                copy_status_for_cams = default_status_value;
                edit_status_for_cams = default_status_value;
                remove_status_for_cams = default_status_value;
                for (int i = DEVICE_CAMERA_POSITION_FRONT; i <= DEVICE_CAMERA_POSITION_RIGHT; i++) {
                    healthstat_reason[i] += "edit";
                }
                healthstat_reason[DEVICE_CAMERA_POSITION_DMS] += "edit";
                LOG_I(TAG, "Will edit all CAM files because of multiple privacy in off-duty mode");
            } else {
                for (int i = DEVICE_CAMERA_POSITION_FRONT; i <= DEVICE_CAMERA_POSITION_RIGHT; i++) {
                    if (!ctx.privacy_params.cam_privacy[i]) {
                        set_bit(edit_status_for_cams, i);
                        set_bit(copy_status_for_cams, i);
                        set_bit(remove_status_for_cams, i);
                        if (i == DEVICE_CAMERA_POSITION_BACK) {
                            set_bit(edit_status_for_cams, DEVICE_CAMERA_POSITION_DMS);
                            set_bit(copy_status_for_cams, DEVICE_CAMERA_POSITION_DMS);
                            set_bit(remove_status_for_cams, DEVICE_CAMERA_POSITION_DMS);
                        }
                        healthstat_reason[i] += "edit";
                        LOG_I(TAG, "Will edit CAM%d file because of multiple privacy in off-duty mode", i);
                    } else {
                        clear_bit(copy_status_for_cams, i);
                        clear_bit(edit_status_for_cams, i);
                        set_bit(remove_status_for_cams, i);
                        if (i == DEVICE_CAMERA_POSITION_BACK) {
                            clear_bit(copy_status_for_cams, DEVICE_CAMERA_POSITION_DMS);
                            clear_bit(edit_status_for_cams, DEVICE_CAMERA_POSITION_DMS);
                            set_bit(remove_status_for_cams, DEVICE_CAMERA_POSITION_DMS);
                        }
                        healthstat_reason[i] += "privacy";
                        LOG_I(TAG, "Will delete CAM%d file because of multiple privacy", i);
                    }
                }
            }
        }
        clear_disabled_camera_bits(copy_status_for_cams, edit_status_for_cams, remove_status_for_cams);
        return true;
    }

    // Handle regular privacy for all cameras
    if (device_mode_global.privacy_status == PRIVACY_OFF) {
        for (int i = DEVICE_CAMERA_POSITION_FRONT; i <= DEVICE_CAMERA_POSITION_RIGHT; i++) {
            clear_bit(edit_status_for_cams, i);
            set_bit(copy_status_for_cams, i);
            set_bit(remove_status_for_cams, i);
            if (i == DEVICE_CAMERA_POSITION_BACK) {
                clear_bit(edit_status_for_cams, DEVICE_CAMERA_POSITION_DMS);
                set_bit(copy_status_for_cams, DEVICE_CAMERA_POSITION_DMS);
                set_bit(remove_status_for_cams, DEVICE_CAMERA_POSITION_DMS);
            }
            healthstat_reason[i] += "no_privacy";
            LOG_I(TAG, "Will copy CAM%d file because of no privacy", i);
        }
    } else if (device_mode_global.privacy_status == PRIVACY_ON) {
        edit_status_for_cams = 0x0;
        remove_status_for_cams = default_status_value;
        for (int i = DEVICE_CAMERA_POSITION_FRONT; i <= DEVICE_CAMERA_POSITION_RIGHT; i++) {
            if (ctx.privacy_params.cam_privacy[i]) {
                clear_bit(copy_status_for_cams, i);
                if (i == DEVICE_CAMERA_POSITION_BACK) {
                    clear_bit(copy_status_for_cams, DEVICE_CAMERA_POSITION_DMS);
                }
                healthstat_reason[i] += "privacy";
                LOG_I(TAG, "Will delete CAM%d file because of regular privacy", i);
            } else {
                set_bit(copy_status_for_cams, i);
                if (i == DEVICE_CAMERA_POSITION_BACK) {
                    set_bit(copy_status_for_cams, DEVICE_CAMERA_POSITION_DMS);
                }
                healthstat_reason[i] += "no_privacy";
                LOG_I(TAG, "Will copy CAM%d file because of no privacy", i);
            }
        }
    } else {
        copy_status_for_cams = default_status_value;
        remove_status_for_cams = default_status_value;
        for (int i = DEVICE_CAMERA_POSITION_FRONT; i <= DEVICE_CAMERA_POSITION_RIGHT; i++) {
            if (ctx.privacy_params.cam_privacy[i]) {
                set_bit(edit_status_for_cams, i);
                if (i == DEVICE_CAMERA_POSITION_BACK) {
                    set_bit(edit_status_for_cams, DEVICE_CAMERA_POSITION_DMS);
                }
                healthstat_reason[i] += "edit";
                LOG_I(TAG, "Will edit CAM%d file because of regular privacy", i);
            } else {
                set_bit(copy_status_for_cams, i);
                if (i == DEVICE_CAMERA_POSITION_BACK) {
                    set_bit(copy_status_for_cams, DEVICE_CAMERA_POSITION_DMS);
                }
                healthstat_reason[i] += "no_privacy";
                LOG_I(TAG, "Will copy CAM%d file because of no privacy", i);
            }
        }
    }

    clear_disabled_camera_bits(copy_status_for_cams, edit_status_for_cams, remove_status_for_cams);
    return true;
}

void send_driver_initiated_privacy_healthstats(privacy_type_t privacy_type)
{
    json_t *driver_initiated_privacy_info = json_object();
    json_t *root = json_object();
    char* req_params = NULL;

    if (privacy_type == OFF_DUTY) {
        json_object_set_new(driver_initiated_privacy_info, "privacy_type", json_string("off-duty"));
    } else if (privacy_type == REGULAR) {
        json_object_set_new(driver_initiated_privacy_info, "privacy_type", json_string("button_privacy"));
    }
    json_object_set_new(driver_initiated_privacy_info, "timestamp", json_integer(get_system_time()));
    json_object_set_new(root, "isArray", json_string("true"));
    json_object_set_new(root, "health_info:driver_initiated_privacy", driver_initiated_privacy_info);

    req_params = json_dumps(root, 0);
    LOG_I(TAG, "sending msg to hs : %s", req_params);
    int length = strlen(req_params);
    nd_service_obj->send_msg_healthstats(req_params, length);
    json_decref(root);
    free(req_params);
}

#ifdef DMS_CAMERA_SUPPORTED
void send_dms_connection_healthstats(int dms_connection_status, int irled_status, char* dmscam_sn,
                                     int dms_sensor_temperature,
                                     dms_fault_registers_t fault_registers,
                                     dms_config_registers_t config_registers,
                                     const std::string& session_filename)
{
    json_t *dms_connection_info = json_object();
    json_t *root = json_object();
    char* req_params = NULL;

    json_object_set_new(dms_connection_info, "cs", json_integer(dms_connection_status));
    json_object_set_new(dms_connection_info, "irled_status", json_integer(irled_status));
    json_object_set_new(dms_connection_info, "sn", json_string(dmscam_sn));
    json_object_set_new(dms_connection_info, "ts", json_integer(get_system_time()));
    json_object_set_new(dms_connection_info, "dms_sensor_temperature", json_integer(dms_sensor_temperature));

    // Helper function to add register values to JSON object
    auto add_registers_to_json = [](json_t *json_obj, const std::vector<std::pair<std::string, uint8_t>> &regs)
    {
        char buf[HEX_BUFFER_SIZE];
        for (const auto &reg : regs)
        {
            snprintf(buf, sizeof(buf), "0x%02X", reg.second);
            json_object_set_new(json_obj, reg.first.c_str(), json_string(buf));
        }
    };

    // Fault register values
    json_t *fault_regs = json_object();
    add_registers_to_json(fault_regs, {{"0x0A", fault_registers.reg_0x0A},
                                       {"0x0B", fault_registers.reg_0x0B},
                                       {"0x0C", fault_registers.reg_0x0C},
                                       {"0x0D", fault_registers.reg_0x0D},
                                       {"0x0E", fault_registers.reg_0x0E}});
    json_object_set_new(dms_connection_info, "fault_register_values", fault_regs);

    // Config register values
    json_t *config_regs = json_object();
    add_registers_to_json(config_regs, {{"0x02", config_registers.reg_0x02},
                                        {"0x03", config_registers.reg_0x03},
                                        {"0x04", config_registers.reg_0x04},
                                        {"0x05", config_registers.reg_0x05}});

    json_object_set_new(dms_connection_info, "config_register_values", config_regs);

    json_object_set_new(root, "session", json_string(session_filename.c_str()));
    json_object_set_new(root, "dms_connection_info", dms_connection_info);

    // Serialize the JSON
    req_params = json_dumps(root, 0);
    if (req_params == NULL) {
        LOG_E(TAG, "JSON creation failed for HS message of irled info");
        json_decref(root);
        return;
    }
    LOG_I(TAG, "sending msg to hs : %s", req_params);
    int length = strlen(req_params);
    nd_service_obj->send_msg_healthstats(req_params, length);
    json_decref(root);
    free(req_params);
}

int get_dms_connection_status()
{
    int dms_connection_status;
    if (ctx.ignition_status == IGNITION_STATUS_OFF)
        dms_connection_status  = DMS_CONNECTION_STATUS_IGNITION_OFF + ctx.is_dms_connected; //when ignition is off setting connection status to 1000 + ( 1 / 0 )
    else if(ctx.ignition_status == IGNITION_STATUS_LPW)
        dms_connection_status = DMS_CONNECTION_STATUS_IGNITION_LPW + ctx.is_dms_connected;  //when in lower power off setting connection status to 2000 + ( 1 / 0 )
    else
        dms_connection_status = ctx.is_dms_connected;

    return dms_connection_status;
}
#endif

/**
 * @brief Parses JSON content to extract IR LED status and states.
 *
 * This function takes a JSON string containing IR LED observation data,
 * parses it, and extracts the "irled_status" (an integer) and "irled_states"
 * (an array or object, serialized to a string). The extracted values are
 * stored in the provided output parameters.
 *
 * @param json_contents   The JSON string containing IR LED observation data.
 * @param irled_status    Pointer to an integer where the IR LED status will be stored.
 * @param irled_states    Pointer to a string where the serialized IR LED states will be stored.
 * @return true if parsing and extraction are successful, false otherwise.
 */
bool get_obs_irled_data(string json_contents, int* irled_status, string* irled_states)
{
    json_t *root_json_data;
    json_error_t error;

    root_json_data = json_loads(json_contents.c_str(), 0, &error);

    if (root_json_data == NULL) {  // File not present or corrupted
        LOG_E(TAG, "Invalid json content: %s", json_contents.c_str());
        return false;
    }

    // Get irled_status (should be an integer)
    json_t *irled_status_t = json_object_get(root_json_data, "irled_status");
    if (irled_status_t == NULL) {
        LOG_E(TAG, "irled_status section not found in observation file");
        json_decref(root_json_data);
        return false;
    }
    *irled_status = json_integer_value(irled_status_t);

    // Get irled_states (should be an array or object, serialize to string)
    json_t *irled_states_t = json_object_get(root_json_data, "irled_states");
    if (irled_states_t == NULL) {
        LOG_E(TAG, "irled_states section not found in observation file");
        json_decref(root_json_data);
        return false;
    }
    char *states_str = json_dumps(irled_states_t, 0);
    if (states_str == NULL) {
        LOG_E(TAG, "Failed to serialize irled_states");
        json_decref(root_json_data);
        return false;
    }
    *irled_states = states_str;
    free(states_str);

    json_decref(root_json_data);
    return true;
}

static void update_session_status_in_device_mode(device_mode_t &device_mode) {
    device_mode.session_status.copy_status_for_cams = copy_status_for_cams;
    device_mode.session_status.edit_status_for_cams = edit_status_for_cams;
    device_mode.session_status.remove_status_for_cams = remove_status_for_cams;
    return;
}

static bool copy_or_move_files(char *fname, int cam_num, int flipflop, uint64_t epoch_time, uint64_t raw_time, uint64_t pts_time, bool is_ld)
{
    bool copied = false, edited = false, copied_audio = false;
    bool removed = false;
    bool end_of_session = false;
    bool is_partial_session = false;

    static int prev_session_count = -1;

    // making chm_fname_suffix as static inorder not to get overwritten.
    // chm_fname_suffix might be needed at the end_of_session if camera 0 files
    // were not copied due to engine idle and vehicle came out of engine idle
    // after that.
    static string chm_fname_suffix = "";

    stringstream ss;

    string filename_prefix = fname;

    LOG_I(TAG, "filename_prefix=%s", filename_prefix.c_str());

    if (filename_prefix == "") {
        LOG_E(TAG, "copy_or_move_files: filename_prefix is empty");
        return false;
    }

    int64_t hs_starttime = -1, hs_endtime = -1;

    // Block to define scope of sessionMapCountMutex
    {
        std::lock_guard<std::mutex> lock(sessionMapCountMutex);
        /* This check is added to make sure that copy_or_move_files flag gets false
           every time the first file of a session comes. Due to this get_copy_status
           will get called only once when the first file of a session is moved */
        if (prev_session_count != sessionMapCount[filename_prefix.substr(1)]) {
            copy_or_move_files_flag = false;
            prev_session_count = sessionMapCount[filename_prefix.substr(1)];

            if (imu_outage) {
                nd_service_obj->send_err_msg(SM_E_NDC_IMU_DATA_OUTAGE, NDService::UNUSED_ERR_AUX_CODE, ("IMU data outage for session " + filename_prefix));
            }
            if (gps_outage) {
                nd_service_obj->send_err_msg(SM_E_NDC_GPS_DATA_OUTAGE, NDService::UNUSED_ERR_AUX_CODE, ("GPS data outage for session " + filename_prefix));
            }
            imu_outage = true;
            gps_outage = true;
        }
    }

    // By end of current session, filenames in ctx.fname can be overwritten.
    // Hence taking copy of file names for using at end of session.
    cur_session_fnames[cam_num] = filename_prefix;

    ss << ctx.base_path_cam0 << "/" << filename_prefix;

    pthread_mutex_lock(&file_start_time_vec_mutex);
    while (file_start_time_vec_pair.size() > 20) {
        file_start_time_vec_pair.erase(file_start_time_vec_pair.begin());
    }

    int i = 0;
    for (i = 0; i < file_start_time_vec_pair.size(); i++)
        LOG_I(TAG, "session: %s, time %lld", file_start_time_vec_pair[i].first.c_str(), file_start_time_vec_pair[i].second);

    pthread_mutex_unlock(&file_start_time_vec_mutex);

    device_mode_t device_mode_global;

    memset((void *)&device_mode_global, 0, sizeof(device_mode_global));

    if (!get_device_mode_for_fname(filename_prefix, device_mode_global))
    {
        LOG_E(TAG, "Failed to get device_mode for %s", filename_prefix.c_str());
    }
    LOG_I(TAG, "cam_num:%d, device_mode_global.ref_count:%d", cam_num, device_mode_global.ref_count);
    if (device_mode_global.ref_count == 1)
    {
        end_of_session = true;
    }

    video_copied[cam_num] = false;
    video_removed[cam_num] = false;

    if (filename_prefix.find(partial_session_name) == std::string::npos) {
        device_mode_global_partial = device_mode_global;
        /* setting all false to avoid any garbage value */
        device_mode_global_partial.partial_privacy_params = {false};
        partial_video_len_sec = 60;
    } else {
        /* copy_or_move files called for other cameras partial files except outward, if
         * bagheera restarted in between for any reason. In that case we need to retrieve
         * device mode for partial file */
        device_mode_global = device_mode_global_partial;
        is_partial_session = true;
        LOG_I(TAG, "copy_or_move_files called for partial session");
    }

    LOG_I(TAG, "privacy_status %d individual_states_len %d ",
        device_mode_global.privacy_status, device_mode_global.individual_states_len);

    /* This flag check is to make sure, we call get_copy_status as soon as copy_or_move_files gets called for any camera */
    if (copy_or_move_files_flag == false && (!is_partial_session)) {
        // get_final_privacy_states_info will calculate final privacy states from regular and offduty privacy states
        get_final_privacy_states_info(device_mode_global);
	LOG_I(TAG, "Final device_mode_global privacy_status = %d, privacy_status_offduty = %d, privacy_status_geofence = %d", device_mode_global.privacy_status, device_mode_global.privacy_status_offduty, device_mode_global.privacy_status_geofence);

        get_copy_status(device_mode_global, session_contains_usr_alert);
        // Update the session status with copy/edit/remove bits in device_mode_global for cam_rec restart scenario.
        // When cam_rec restarts, copy_or_move_files is called for outward camera which calculates these status bits.
        // These same bits are needed when move_other_cam_partial_files is called to handle partial session files for other cameras
        update_session_status_in_device_mode(device_mode_global);
        // Update the device mode in the global map with the final device_mode after calculating final privacy states
        if (!update_device_mode_for_fname(filename_prefix, device_mode_global)) {
            LOG_E(TAG, "Failed to update device_mode in map for %s", filename_prefix.c_str());
        }
        string copy_status_in_binary = std::bitset<CAMERA_POSITION_MAXIMUM>(copy_status_for_cams).to_string();
        string edit_status_in_binary = std::bitset<CAMERA_POSITION_MAXIMUM>(edit_status_for_cams).to_string();
        string remove_status_in_binary = std::bitset<CAMERA_POSITION_MAXIMUM>(remove_status_for_cams).to_string();
        LOG_I(TAG,"Final binary string for copy_status_for_cams, edit_status_for_cams and remove_status_for_cams are %s, %s and %s",
                   copy_status_in_binary.c_str(), edit_status_in_binary.c_str(), remove_status_in_binary.c_str());
        copy_or_move_files_flag = true;
    }

    // DT-2167: For partial sessions created by NDC restart, outward camera uses move_partial_files() which correctly
    // sets session_contains_usr_alert based on user alerts, but other cameras use copy_or_move_files()
    // where session_contains_usr_alert was not getting set to true. We need to check for
    // user alerts in the partial session and set session_contains_usr_alert accordingly for other cameras.
    // IMPORTANT: Ignore user alerts if geofence privacy is present (even in partial sessions)
    if (is_partial_session) {
        if (device_mode_global.partial_privacy_params.has_user_alert && 
            device_mode_global.partial_privacy_params.save_user_alert_video) {
            // Check LAST geofence state from the PREVIOUS session (before reboot) saved in partial CSV
            // We use the last value written to CSV, not the calculated session-level status
            if (device_mode_global_partial.partial_privacy_params.geofence_privacy) {
                // Geofence privacy was ON at end of previous session - ignore user alert and log critical event
                LOG_C(TAG, "CRITICAL: User alert in PARTIAL session IGNORED - geofence was ON at end of previous session. "
                      "geofence_privacy=%d, off_duty_privacy=%d, enhanced_privacy=%d, save_user_alert_video=%d",
                      device_mode_global_partial.partial_privacy_params.geofence_privacy,
                      device_mode_global_partial.partial_privacy_params.off_duty_privacy,
                      device_mode_global_partial.partial_privacy_params.enhanced_privacy,
                      device_mode_global_partial.partial_privacy_params.save_user_alert_video);
                // Don't set session_contains_usr_alert
            } else {
                // Geofence was OFF at end of previous session - honor user alert in partial session
                LOG_I(TAG, "Partial session - geofence was OFF, honoring user alert");
                session_contains_usr_alert = true;
            }
        }
    }

    // For outward camera, meta data and chm need to be generated
    // Need not do it again for LD files
    if (cam_num == DEVICE_CAMERA_POSITION_FRONT && !is_ld) {
        ss.clear();
        ss.str("");
        ss << ctx.base_path_cam0 << "/" << filename_prefix;

        irled_mode_t irled_mode_current_session;
        memset((void *)&irled_mode_current_session, 0, sizeof(irled_mode_current_session));
        irled_mode_current_session.irled_status = -1;

        if (!get_irled_mode_for_fname(filename_prefix, irled_mode_current_session)) {
            LOG_E(TAG, "Failed to get irled_mode for %s", filename_prefix.c_str());
        }
        LOG_I(TAG, "irled_status: %d, irled_states_len: %d",
            irled_mode_current_session.irled_status, irled_mode_current_session.irled_states_len);

        // Get crank level from SYSFS file
        ctx.crank_level_RT_thread = nd_device_obj->get_crank_level();
        if (ctx.crank_level_RT_thread != CRANK_LOW){
            ctx.send_rt_frames_till_time = INT_64_MAX;
            ctx.send_rt_frames_till_time_outward = INT_64_MAX;
        }
        else{
            if (ctx.send_rt_frames_till_time == INT_64_MAX){
                ctx.send_rt_frames_till_time = get_system_monotonic_time() + ctx.post_ignition_analytics_secs * 1000;
            }
            if (ctx.send_rt_frames_till_time_outward == INT_64_MAX){
                ctx.send_rt_frames_till_time_outward = get_system_monotonic_time() + ctx.post_ignition_analytics_secs_outward * 1000;
            }
        }
        LOG_I(TAG, "crank_level_RT_thread %d ctx.send_rt_frames_till_time_outward %lld",
              ctx.crank_level_RT_thread, ctx.send_rt_frames_till_time_outward);

        string Json = "";

        stringstream ss_key, ss_value;

        ss_key.str("");
        ss_value.str("");
        string string_for_copy_status_cams = std::bitset<CAMERA_POSITION_MAXIMUM>(copy_status_for_cams).to_string();
        ss_value << string_for_copy_status_cams;
        ctx.genmeta->update_genmeta_header("copy_status_cam", ss_value.str(), flipflop);
        LOG_I("final string for copy status of camera %s", string_for_copy_status_cams.c_str());

        ss_value.str("");
        ss_value << ctx.camStartTime_ld[DEVICE_CAMERA_POSITION_FRONT][ctx.out_meta_count % 2];
        ctx.genmeta->update_genmeta_header("start_time_ld", ss_value.str(), flipflop);
        LOG_I(TAG, "StartTimeLd %lld", ctx.camStartTime_ld[DEVICE_CAMERA_POSITION_FRONT][ctx.out_meta_count % 2]);

        ss_value.str("");
        ss_value << ctx.camStartTime_dp[DEVICE_CAMERA_POSITION_FRONT][ctx.out_meta_count % 2];
        ctx.genmeta->update_genmeta_header("start_time_dp", ss_value.str(), flipflop);
        LOG_I(TAG, "StartTimeDP %lld", ctx.camStartTime_dp[DEVICE_CAMERA_POSITION_FRONT][ctx.out_meta_count % 2]);

        ss_value.str("");
        ss_value << ctx.camPtsStartTime[DEVICE_CAMERA_POSITION_FRONT][ctx.out_meta_count % 2];
        ctx.genmeta->update_genmeta_header("ptsStartTime", ss_value.str(), flipflop);
        LOG_I(TAG, "ptsStartTime %lld", pts_time);

        ss_value.str("");
        ss_value << ctx.camPtsStartTime_ld[DEVICE_CAMERA_POSITION_FRONT][ctx.out_meta_count % 2];
        ctx.genmeta->update_genmeta_header("ptsStartTimeLd", ss_value.str(), flipflop);
        LOG_I(TAG, "ptsStartTimeLd %lld", ctx.camPtsStartTime_ld[DEVICE_CAMERA_POSITION_FRONT][ctx.out_meta_count % 2]);

        ss_value.str("");
        ss_value << ctx.camPtsStartTime_dp[DEVICE_CAMERA_POSITION_FRONT][ctx.out_meta_count % 2];
        ctx.genmeta->update_genmeta_header("ptsStartTimeDP", ss_value.str(), flipflop);
        LOG_I(TAG, "ptsStartTimeDP %lld", ctx.camPtsStartTime_dp[DEVICE_CAMERA_POSITION_FRONT][ctx.out_meta_count % 2]);

        ss_value.str("");
        ss_value << ctx.camStartTime[DEVICE_CAMERA_POSITION_BACK][ctx.out_meta_count % 2];
        ctx.genmeta->update_genmeta_header("inwardStartTime", ss_value.str(), flipflop);
        LOG_I(TAG, "inwardStartTime %lld ctx.out_meta_count %d",
              ctx.camStartTime[DEVICE_CAMERA_POSITION_BACK][ctx.out_meta_count % 2], ctx.out_meta_count);

        ss_value.str("");
        ss_value << ctx.camStartTime_ld[DEVICE_CAMERA_POSITION_BACK][ctx.out_meta_count % 2];
        ctx.genmeta->update_genmeta_header("inwardStartTimeLd", ss_value.str(), flipflop);
        LOG_I(TAG, "inwardStartTimeLd %lld",
              ctx.camStartTime_ld[DEVICE_CAMERA_POSITION_BACK][ctx.out_meta_count % 2]);

        ss_value.str("");
        ss_value << ctx.camPtsStartTime[DEVICE_CAMERA_POSITION_BACK][ctx.out_meta_count % 2];
        ctx.genmeta->update_genmeta_header("inwardPtsStartTime", ss_value.str(), flipflop);
        LOG_I(TAG, "inwardPtsStartTime %lld", ctx.camPtsStartTime[DEVICE_CAMERA_POSITION_BACK][ctx.out_meta_count % 2]);

        ss_value.str("");
        ss_value << ctx.camPtsStartTime_ld[DEVICE_CAMERA_POSITION_BACK][ctx.out_meta_count % 2];
        ctx.genmeta->update_genmeta_header("inwardPtsStartTimeLd", ss_value.str(), flipflop);
        LOG_I(TAG, "inwardPtsStartTimeLd %lld", ctx.camPtsStartTime_ld[DEVICE_CAMERA_POSITION_BACK][ctx.out_meta_count % 2]);

        ss_value.str("");
        ss_value << ctx.camStartTime[DEVICE_CAMERA_POSITION_DMS][ctx.out_meta_count % 2];
        ctx.genmeta->update_genmeta_header("dmsStartTime", ss_value.str(), flipflop);
        LOG_I(TAG, "dmsStartTime %lld ctx.out_meta_count %d", ctx.camStartTime[DEVICE_CAMERA_POSITION_DMS][ctx.out_meta_count % 2], ctx.out_meta_count);

        ss_value.str("");
        ss_value << ctx.camStartTime_ld[DEVICE_CAMERA_POSITION_DMS][ctx.out_meta_count % 2];
        ctx.genmeta->update_genmeta_header("dmsStartTimeLd", ss_value.str(), flipflop);
        LOG_I(TAG, "dmsStartTimeLd %lld ctx.out_meta_count %d", ctx.camStartTime_ld[DEVICE_CAMERA_POSITION_DMS][ctx.out_meta_count % 2], ctx.out_meta_count);

        ss_value.str("");
        ss_value << ctx.camPtsStartTime[DEVICE_CAMERA_POSITION_DMS][ctx.out_meta_count % 2];
        ctx.genmeta->update_genmeta_header("dmsPtsStartTime", ss_value.str(), flipflop);
        LOG_I(TAG, "dmsPtsStartTime %lld", ctx.camPtsStartTime[DEVICE_CAMERA_POSITION_DMS][ctx.out_meta_count % 2]);

        ss_value.str("");
        ss_value << ctx.camPtsStartTime_ld[DEVICE_CAMERA_POSITION_DMS][ctx.out_meta_count % 2];
        ctx.genmeta->update_genmeta_header("dmsPtsStartTimeLd", ss_value.str(), flipflop);
        LOG_I(TAG, "dmsPtsStartTimeLd %lld", ctx.camPtsStartTime_ld[DEVICE_CAMERA_POSITION_DMS][ctx.out_meta_count % 2]);

        ss_value.str("");
        ss_value << ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_FRONT];
        ctx.genmeta->update_genmeta_header("outward_cam_privacy", ss_value.str(), flipflop);
        LOG_I(TAG, "outward_cam_privacy %d", ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_FRONT]);

        ss_value.str("");
        ss_value << ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_BACK];
        ctx.genmeta->update_genmeta_header("inward_cam_privacy", ss_value.str(), flipflop);
        LOG_I(TAG, "inward_cam_privacy %d", ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_BACK]);

        pthread_mutex_lock(&dis_mutex);
        ctx.genmeta->update_genmeta_header("driverInvariantSession", ctx.dis_uid_string, flipflop);
        LOG_I(TAG, "driverInvariantSession:: %s", ctx.dis_uid_string.c_str());

        ss_key.str(""); ss_value.str("");
        ss_value << ctx.dis_status;
        ctx.genmeta->update_genmeta_header("disMode", ss_value.str(), flipflop);
        LOG_I(TAG, "disMode:: %s", ss_value.str().c_str());
        pthread_mutex_unlock(&dis_mutex);

        map<string, int> sensor_data_size_map;
        fill_sensor_data_size_map_healthstats(sensor_data_size_map, flipflop);
        Json = get_metadata_json(device_mode_global.privacy_status, device_mode_global.engine_idle,
                                 ctx.crank_level_RT_thread, device_mode_global, flipflop, epoch_time, raw_time,
                                 ctx.hdmaps_mode_enabled, ctx.imu_data, irled_mode_current_session);

        bool metadata_success = generate_metadata_and_chm_files(ND_INPUT_PATH + "/" + filename_prefix, Json, chm_fname_suffix);
        if (metadata_success) {
            LOG_I(TAG, "sending recording data to HS");
            send_alert_info_recording_info_healthstats(filename_prefix, ctx.cam_session_start_time[cam_num][flipflop], epoch_time / ONE_MILLI_IN_MICRO);

            // Add IRLED info to healthstats
            int irled_status = -1;
            string irled_states;
            if (get_obs_irled_data(Json, &irled_status, &irled_states)) {
                LOG_I(TAG, "Sending IRLED info to healthstats: filename = %s, irled_status = %d, irled_states = %s", filename_prefix.c_str(), irled_status, irled_states.c_str());
                send_session_irled_info_healthstats(filename_prefix, irled_status, irled_states);
            } else {
                LOG_E(TAG, "Failed to get IRLED info from metadata for healthstats");
            }

            string metadata_fname = ND_INPUT_PATH + "/" + filename_prefix + video_metadata_extn;
            send_obs_gen_message_healthstats(metadata_fname, sensor_data_size_map);

            // Publish QR scan info to healthstats if driver login via QR was used
            if (ctx.is_driverlogin_qr_enabled) {
                send_qr_scan_info_healthstats(filename_prefix);
            }
        }
        // Calling scheduler even in failure case of metadata so that scheduler can perform other tasks such as calling OTA update Engine
        send_trigger_scheduler_manager();
        sensor_data_size_map.clear();

        // create and encrypt audio encoded file for this session
        audio_file_gen(flipflop, filename_prefix);
        ctx.out_meta_count++;
    }

    /*
    These alerts for corresponding cam_number are set to false here to ensure it only gets false only when
    the function to copy files is already invoked , precisely it is ensuring that whenever cam_rec crashes correct value of user_alert_copy[cam_num]
    is propagated in move_other_cam_partial_files
    */
    if (user_alert_copy[cam_num] == true)
        user_alert_copy[cam_num] = false;

    if (highg_alert_copy[cam_num] == true)
        highg_alert_copy[cam_num] = false;

    string filename_with_extn;
    if (is_ld)
        filename_with_extn = ss.str() + video_file_extn + ld_extn;
    else
        filename_with_extn = ss.str() + video_file_extn;

    // partial privacy file; get the privacy times and blackout privacy video
    if (is_bit_set(edit_status_for_cams, cam_num)) {
        if (device_mode_global.privacy_status_geofence == PRIVACY_MIXED ||
            device_mode_global_partial.privacy_status_geofence == PRIVACY_MIXED) {
            LOG_C(TAG, "CAM%d video will be EDITED (geofence MIXED + other privacy) - session=%s",
                cam_num, filename_prefix.c_str());
        }
        hs_starttime = get_system_time();
        edited = apply_partial_privacy_and_save_video(filename_with_extn,
                                                      CIRCULAR_BUFFER_PATH, device_mode_global, cam_num, is_ld, partial_video_len_sec); // mixed privacy
        if ((cam_num == DEVICE_CAMERA_POSITION_FRONT) && (dp_enabled == true)) {
            /* Since the framerate of DP file is configurable and for blacking out
             * of the frames in case of mixed privacy, we need a blackoutvideo with
             * predefined framerate which is not possible in case of DP video,
             * we are going to delete the DP file in case of mixed privacy.
             */
            file_delete(ss.str() + video_file_extn + dp_extn); // Delete DP file in case of mixed privacy
        }
        if ((cam_num == DEVICE_CAMERA_POSITION_FRONT) && (ea_outward_enabled == 1) && file_is_present(ss.str() + ea_extn)) {
            if (!is_ld) {
                /* Deleting EA image file in case of mixed privacy */
                file_delete(ss.str() + ea_extn);
                string ea_image_fname = filename_prefix + ea_extn;
                LOG_I(TAG, "Deleting ea image file %s because of mixed privacy", ea_image_fname.c_str());
                post_uploader_add_ea_file_db(ea_image_fname, cam_num, PARTIAL_PRIVACY);
            }
        }
        hs_endtime = get_system_time();
        if (edited) {
            video_copied[cam_num] = true;
        }
    } else if (is_bit_set(copy_status_for_cams, cam_num)) {
        if (device_mode_global.privacy_status_geofence != PRIVACY_OFF ||
            device_mode_global_partial.privacy_status_geofence != PRIVACY_OFF) {
            LOG_I(TAG, "CAM%d video COPIED despite geofence (user alert override or partial copy) - session=%s",
                cam_num, filename_prefix.c_str());
        }
        pthread_mutex_lock(&file_mutex);
        hs_starttime = get_system_time();

        copied = move_files(filename_with_extn, CIRCULAR_BUFFER_PATH); // copy mp4

        if ((cam_num == DEVICE_CAMERA_POSITION_FRONT) && (dp_enabled == true)) {
            if (!is_ld)
                copied |= move_files(filename_with_extn + dp_extn, CIRCULAR_BUFFER_PATH); // copy DP mp4
        }

        if ((cam_num == DEVICE_CAMERA_POSITION_FRONT) && (ea_outward_enabled == 1) && file_is_present(ss.str() + ea_extn)) {
            if (!is_ld) {
                string ea_image_fname = filename_prefix + ea_extn;
                bool upload_vid_flag = ctx.privacy_params.upload_video[cam_num];
                bool need_to_copy = session_contains_usr_alert && ctx.privacy_params.save_user_alert_video; //TODO: CHECK_WITH_SHRAVAN - FOR EA to add geofence check

                if ((device_mode_global.privacy_status == PRIVACY_ON) && !upload_vid_flag && !need_to_copy) {
                    LOG_I(TAG, "Deleting ea image file %s because of upload privacy", ea_image_fname.c_str());
                    file_delete(ss.str() + ea_extn);
                    post_uploader_add_ea_file_db(ea_image_fname, cam_num, UPLOAD_PRIVACY);
                } else {
                    // copy EA image
                    LOG_I(TAG, "Copying ea image file %s to sdcard", ea_image_fname.c_str());
                    move_files(ss.str() + ea_extn, CIRCULAR_BUFFER_PATH_EA);
                    post_uploader_add_ea_file_db(ea_image_fname, cam_num, NO_PRIVACY);
                }
            }
        }

        hs_endtime = get_system_time();
        pthread_mutex_unlock(&file_mutex);

        if (copied) {
            video_copied[cam_num] = true;
        }
    }

    if ((cam_num == DEVICE_CAMERA_POSITION_FRONT) && (audio_enable == true)) {
        if (!is_ld) {
            bool record_privacy = false;
            check_audio_privacy(record_privacy);
            if (record_privacy == false) {
                if (device_mode_global.privacy_status_geofence != PRIVACY_OFF ||
                        device_mode_global_partial.privacy_status_geofence != PRIVACY_OFF) {
                    LOG_I(TAG, "Audio COPIED despite geofence (user alert override) - session=%s",
                            filename_prefix.c_str());
                }
                int64_t hs_audio_starttime = -1, hs_audio_endtime = -1;
                hs_audio_starttime = get_system_time();
                copied_audio = file_copy_wrapper(ss.str() + audio_file_extn, CIRCULAR_BUFFER_PATH + filename_prefix + audio_file_extn); // copy only .aac
                hs_audio_endtime = get_system_time();
                fill_map_add_file_audio_healthstats(ss.str(), CIRCULAR_BUFFER_PATH, hs_audio_starttime, hs_audio_endtime, copied_audio);
            }
        }
    }

    if ((cam_num == DEVICE_CAMERA_POSITION_FRONT) && (ea_outward_enabled == 1) && file_is_present(ss.str() + ea_extn)) {
        if (!is_ld) {
            if ((is_bit_set(edit_status_for_cams, DEVICE_CAMERA_POSITION_FRONT) == 0) &&
                (is_bit_set(copy_status_for_cams, DEVICE_CAMERA_POSITION_FRONT) == 0) &&
                (is_bit_set(remove_status_for_cams, DEVICE_CAMERA_POSITION_FRONT) == 1)) {
                string ea_image_fname = filename_prefix + ea_extn;
                if (file_is_present(lpw_no_record_persistent_file)) {
                    LOG_I(TAG, "Deleting ea image file %s because of lpw_no_record", ea_image_fname.c_str());
                    post_uploader_add_ea_file_db(ea_image_fname, cam_num, LPW_NO_CAPTURE);
                } else {
                    LOG_I(TAG, "Deleting ea image file %s because of full privacy", ea_image_fname.c_str());
                    post_uploader_add_ea_file_db(ea_image_fname, cam_num, RECORD_PRIVACY);
                }
            }
        }
    }

    // adding entries to circular buffer DB regardless file is copied/deleted
    add_filenames_to_db(filename_prefix, chm_fname_suffix, cam_num, is_ld);

    if (is_bit_set(remove_status_for_cams, cam_num)) {
        pthread_mutex_lock(&file_mutex);
        removed = file_delete(filename_with_extn);
        pthread_mutex_unlock(&file_mutex);
        if (removed) {
            video_removed[cam_num] = true;
        }
    }

    if ((is_bit_set(copy_status_for_cams, cam_num) || is_bit_set(edit_status_for_cams, cam_num) ||
            is_bit_set(remove_status_for_cams, cam_num))) {
        if (!is_ld)
            fill_map_add_file_healthstats(ss.str(), CIRCULAR_BUFFER_PATH, cam_num, hs_starttime, hs_endtime, copied || edited, healthstat_reason[cam_num], flipflop);
    }

    // During the last invokation of copy_or_move function,
    // we check if there were any files which didn't get copied
    // because of engine idle being enabled when corresponding
    // call to copy_or_move was made
    if (end_of_session) {
        LOG_I(TAG, "END OF SESSION");
        for (int i = 0; i < NUM_CAMERAS; i++) {
            if (!cams_enabled[i] || video_copied[i]) {
                continue;
            }
            LOG_I(TAG, "file %s was not copied during this session", cur_session_fnames[i].c_str());
            if (video_removed[i]) {
                LOG_I(TAG, "No need to copy %s", cur_session_fnames[i].c_str());
                continue;
            }
            LOG_I(TAG, "Decision to remove %s was deferred", cur_session_fnames[i].c_str());

            ss.clear();
            ss.str("");
            ss << ctx.base_path_cam0 << "/" << cur_session_fnames[i];
            if (!device_mode_global.engine_idle) {
                LOG_I(TAG, "Moving %s", ss.str().c_str());
                pthread_mutex_lock(&file_mutex);
                hs_starttime = get_system_time();

                copied = move_files(ss.str() + video_file_extn, CIRCULAR_BUFFER_PATH); // copy HD mp4
                if ((i == DEVICE_CAMERA_POSITION_FRONT && outward_ld) ||
                        (i == DEVICE_CAMERA_POSITION_BACK && inward_ld) ||
                        (i == DEVICE_CAMERA_POSITION_DMS && dms_ld))
                    copied |= move_files(ss.str() + video_file_extn + ld_extn, CIRCULAR_BUFFER_PATH); // copy LD mp4
                if ((i == DEVICE_CAMERA_POSITION_FRONT) && (dp_enabled == true))
                    copied |= move_files(ss.str() + video_file_extn + dp_extn, CIRCULAR_BUFFER_PATH); // copy DP mp4
                if ((i == DEVICE_CAMERA_POSITION_FRONT) && (ea_outward_enabled == 1) && file_is_present(ss.str() + ea_extn)) {
                    move_files(ss.str() + ea_extn, CIRCULAR_BUFFER_PATH_EA); // copy EA image
                    string ea_image_fname = filename_prefix + ea_extn;
                    LOG_I(TAG, "engine_idle_state_exited: Copying ea image file %s to sdcard", ea_image_fname.c_str());
                    post_uploader_add_ea_file_db(ea_image_fname, DEVICE_CAMERA_POSITION_FRONT, NO_PRIVACY);
                }

                hs_endtime = get_system_time();

                if ((cam_num == DEVICE_CAMERA_POSITION_FRONT) && (audio_enable == true)) {
                    bool record_privacy = false;
                    check_audio_privacy(record_privacy);
                    if (record_privacy == false)
                        copied_audio = move_files(ss.str() + audio_file_extn, CIRCULAR_BUFFER_PATH); // copy only .aac
                }
                pthread_mutex_unlock(&file_mutex);
                if (copied) {
                    video_copied[i] = true;
                }
            } else {
                if ((i == DEVICE_CAMERA_POSITION_FRONT) && (ea_outward_enabled == 1) && file_is_present(ss.str() + ea_extn)) {
                    string ea_image_fname = filename_prefix + ea_extn;
                    LOG_I(TAG, "Deleting ea image file %s because of engine idle", ea_image_fname.c_str());
                    post_uploader_add_ea_file_db(ea_image_fname, DEVICE_CAMERA_POSITION_FRONT, RECORD_PRIVACY);
                }
            }
            pthread_mutex_lock(&file_mutex);
            removed = remove_files(ss.str());
            pthread_mutex_unlock(&file_mutex);
            if (removed) {
                video_removed[i] = true;
            }

            fill_map_add_file_healthstats(ss.str(), CIRCULAR_BUFFER_PATH, i, hs_starttime, hs_endtime, copied, healthstat_reason[i], flipflop);
        }
    }
    return true;
}

static bool stop_meta(char *fname, int cam_num, int flipflop, uint64_t epoch_time, uint64_t raw_time, uint64_t pts_time, bool is_ld)
{
#ifdef KRAIT
	/* Once we get the ADC value from the OBD callback, no need to call read_adc_status_and_channel_two_data again */
	/* As inside this function it will unnecessarily wait for 2 sec to get the OBD callback */
    if (!is_adc_updated)
    {
        float adc_val=0.0;
        int adc_state=0;
        // read voltage
        if (true == read_adc_status_and_channel_two_data(&adc_val,&adc_state)) {
            LOG_D(TAG,"adc_val :%f,adc_state:%d",adc_val,adc_state);
            if (0 == adc_state) {
                LOG_E(TAG,"unable to read obd data, make sure obd can is connected !!!");
            }
            if (adc_val < 0) {
                LOG_E(TAG, "failed in read_adc_status_and_channel_two_data");
                adc_val = 0;
            }
            LOG_I(TAG, "****ADC voltage In STOP_META:%f****", adc_val);
        }
        stringstream ss_value;
        ss_value << adc_val;
        ctx.genmeta->update_genmeta_header("Voltage in Volts", ss_value.str(), flipflop);
    }
#endif

    ndc_copy_or_move_file_msg_t cmf_msg;

    cmf_msg.type = COPY_OR_MOVE_FILE;
    cmf_msg.len = sizeof(ndc_copy_or_move_file_msg_t);
    if (fname)
        cmf_msg.fname = strdup(fname);
    cmf_msg.cam_num = cam_num;
    cmf_msg.flipflop = flipflop;
    cmf_msg.epoch_time = epoch_time;
    cmf_msg.raw_time = raw_time;
    cmf_msg.pts_time = pts_time;
    cmf_msg.is_ld = is_ld;

    nd_msgq_t::nd_msg_t msg((char *)&cmf_msg, sizeof(cmf_msg), false);
    if (ctx.cmf_msg_q->send(msg, nd_msgq_t::ND_MSG_MED) == false) {
        LOG_E( TAG, "Cannot send message to: %s ", QNAME_CMF.c_str());
    }

    return true;
}

static void send_led_blinking_resp_msg_to_installer_app() {
    // Send message to installer app service
    led_blink_resp_msg_t inst_msg;
    memset(&inst_msg, 0, sizeof(inst_msg));
    inst_msg.blink_status_resp = LED_IS_NOT_BLINKING;

    const std::string src_q = get_msgq_name();
    if ( false == send_msg((generic_msg_t*)&inst_msg,
                  (msg_type_t)RESP_INSTALLER_LED_BLINKING,
                  sizeof(inst_msg),
                  src_q.c_str(),
                  installer_queue_name,
                  0)) {
        LOG_E(TAG, "Sending LED_IS_NOT_BLINKING msg to installer app failed");
        return;
    }

    LOG_I(TAG, "Sent LED_IS_NOT_BLINKING message to installer app service");
}

void led_blinking_timer(int time_in_second)
{
    LOG_I (TAG," led_blinking_timer(): time_in_second::  %d ", time_in_second);
    std::this_thread::sleep_for(std::chrono::seconds(time_in_second)); //sleep(time_in_second);
    if(ctx.is_led_blinking) {
        ctx.is_led_blinking = false;
        if (!ctx.off_duty_privacy && !ctx.geofence_privacy) {
            nd_device_obj->nd_clear_led(BLINKING_LED);
            nd_device_obj->nd_clear_led(INST_BLINKING_LED);
            nd_device_obj->nd_set_led_on(GREEN, POWER_LED, true );
        } else {
            nd_device_obj->nd_clear_led(BLINKING_LED_OFF_DUTY);
            nd_device_obj->nd_clear_led(INST_BLINKING_LED);
            nd_device_obj->nd_set_led_on(RED, POWER_LED, true );
        }

        LOG_I (TAG," led_blinking_timer(): led_blinking stopped... ");
    } else {
        LOG_I (TAG,"Error:  Not BLINKING ");
    }
    return ;
}

static uint16_t get_installer_scan_blink_timeout_sec(int requested_timeout_sec)
{
    if (requested_timeout_sec <= 0) {
        return installer_scan_max_blink_timeout_sec;
    }

    if (requested_timeout_sec > installer_scan_max_blink_timeout_sec) {
        return installer_scan_max_blink_timeout_sec;
    }

    return static_cast<uint16_t>(requested_timeout_sec);
}

static void reset_installer_scan_blink_deadline(uint16_t timeout_sec)
{
    int64_t now_sec = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    inst_scan_blink_deadline_sec = now_sec + timeout_sec;
}

static bool start_installer_scan_led_blinking(uint16_t timeout_sec)
{
    reset_installer_scan_blink_deadline(timeout_sec);

    bool expected = false;
    if (!inst_scan_led_blinking.compare_exchange_strong(expected, true)) {
        LOG_I(TAG, "Installer scan blink already running, timeout reset to %u seconds from now.", (unsigned)timeout_sec);
        return false;
    }

    inst_scan_cancel = false;
    if (!pthread_create(&inst_scan_thread_id, NULL, inst_scan_led_worker,
                        (void*)(uintptr_t)timeout_sec)) {
        pthread_detach(inst_scan_thread_id);
        LOG_I(TAG, "Installer scan blink thread started with timeout=%u.", (unsigned)timeout_sec);
        return true;
    }

    // Failed to start thread, reset blinking state and return failure
    inst_scan_led_blinking = false;
    LOG_E(TAG, "Failed to create installer scan led blinking thread.");
    return false;
}

static void recover_installer_scan_led_blinking_on_startup()
{
    if (!file_is_present(installer_scan_active_file)) {
        LOG_I(TAG, "Installer scan ramfile not present at startup, no need to recover installer blink.");
        return;
    }

    LOG_I(TAG, "Installer scan ramfile present at startup, recovering installer blink for max timeout: %u seconds.",
         installer_scan_max_blink_timeout_sec);
    pthread_mutex_lock(&inst_scan_led_mutex);
    start_installer_scan_led_blinking(installer_scan_max_blink_timeout_sec);
    pthread_mutex_unlock(&inst_scan_led_mutex);
}

static void* inst_scan_led_worker(void* arg) {

    //Installer Scan blinking LED start
    ctx.is_led_blinking = true;
    inst_scan_led_blinking = true;
    auto blink_start_tp = std::chrono::steady_clock::now();
    int64_t last_blink_log_sec = 0;

    uint16_t timeout_sec = static_cast<uint16_t>(reinterpret_cast<uintptr_t>(arg));
    LOG_I(TAG, "Installer scan worker started (timeout=%u)", (unsigned)timeout_sec);

    nd_device_obj->nd_clear_led(BLINKING_LED);
    nd_device_obj->nd_clear_led(INST_BLINKING_LED);
    nd_device_obj->nd_blink_led(INST_BLINKING_LED, DUTYCYCLE_50);

    // Wait for Installer scan blinking LED stop signal, ramfile absence, or timeout deadline.
    while (true) {
        if (inst_scan_cancel) {
            LOG_I(TAG, "inst_scan_led_worker: Received stop message from INSTALLER_SCAN_INDICATE.");
            break;
        }
        if (!file_is_present(installer_scan_active_file)) {
            LOG_I(TAG, "inst_scan_led_worker: installer scan ramfile missing, stopping installer blink.");
            break;
        }

        int64_t now_sec = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        if (now_sec >= inst_scan_blink_deadline_sec.load()) {
            LOG_I(TAG, "inst_scan_led_worker: timeout reached, stopping installer blink.");
            break;
        }

        int64_t elapsed_sec = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - blink_start_tp).count();
        if ((elapsed_sec > 0) && (elapsed_sec % 10 == 0) && (elapsed_sec != last_blink_log_sec)) {
            last_blink_log_sec = elapsed_sec;
            LOG_I(TAG, "inst_scan_led_worker: LED is blinking... elapsed=%lld seconds", elapsed_sec);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    
    // Stop blinking if stop signal arrives or timeout is reached.
    if (!ctx.off_duty_privacy && !ctx.geofence_privacy) {
        nd_device_obj->nd_clear_led(BLINKING_LED);
        nd_device_obj->nd_clear_led(INST_BLINKING_LED);
        nd_device_obj->nd_set_led_on(GREEN, POWER_LED, true);
    } else {
        nd_device_obj->nd_clear_led(BLINKING_LED_OFF_DUTY);
        nd_device_obj->nd_clear_led(INST_BLINKING_LED);
        nd_device_obj->nd_set_led_on(RED, POWER_LED, true);
    }
    int64_t total_blink_sec = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - blink_start_tp).count();
    LOG_I(TAG, "inst_scan_led_worker: Total LED blinking duration=%lld seconds", total_blink_sec);

    LOG_I(TAG, "inst_scan_led_worker: Completed.");
    inst_scan_cancel = false;
    inst_scan_led_blinking = false;
    inst_scan_blink_deadline_sec = 0;
    ctx.is_led_blinking = false;

    send_led_blinking_resp_msg_to_installer_app();
    
    return nullptr;
}

static bool handle_user_alert(int64_t timestamp, int button, const char *source) {
    LOG_I(TAG, "handle_user_alert called for button: %d, at %lld", button, timestamp);

    genmeta_usralert_t alert;
    alert.time = timestamp;
    alert.btn = button;

    if ((nullptr == source) || (0 == strlen(source))) {
        nd_strncpy(alert.source, "unknown", sizeof(alert.source));
    } else {
        nd_strncpy(alert.source, source, sizeof(alert.source));
    }

    if (ctx.genmeta->push_data(alert, ctx.session_flipflop) == false) {
        LOG_E(TAG, "Error adding user alert to metadata");
        return false;
    }
    ctx.meta_buff[ctx.session_flipflop].push(alert);
    if (ctx.is_led_blinking == false) {
        if (ctx.off_duty_privacy)
            nd_device_obj->nd_blink_led(BLINKING_LED_OFF_DUTY, DUTYCYCLE_50);   // netradyne api
        else if (ctx.geofence_privacy)
            nd_device_obj->nd_blink_led(BLINKING_LED_OFF_DUTY, DUTYCYCLE_50);   // netradyne api - using same LED for geofence
        else
            nd_device_obj->nd_blink_led(BLINKING_LED, DUTYCYCLE_50);   // netradyne api
        ctx.is_led_blinking = true;
        LOG_I(TAG, "Led blink started by nd_blink_led():: ctx.led_blinking_durarion: %d ",  ctx.led_blinking_durarion);
        user_alert_result = std::async(std::launch::async, led_blinking_timer, ctx.led_blinking_durarion);
        LOG_I(TAG, "led_blinking_timer() is called with async");

        if (ctx.user_alert_cfg.no_of_buttons_ > button) {
            if (ctx.user_alert_cfg.enable_status_.at(button)) {
                if (send_alert_audio_play(button)) {
                    LOG_I(TAG, "User audio alert sent");
                } else {
                    LOG_E(TAG, "failed to send user audio alert to analytics");
                }
            } else {
                LOG_E(TAG, "Audio not enabled for this alert button");
            }
        } else {
            LOG_E(TAG, "Not a valid button number");
        }
    } else {
       LOG_I(TAG, "Led is already blinking hence ignore this event for led blink");
    }
    return true;
}

static bool handle_highg_alert(int64_t timestamp) {
    genmeta_highgalert_t alert;
    alert.time = timestamp;

    if( ctx.genmeta->push_data(alert, ctx.session_flipflop) == false ) {
        LOG_E(TAG, "Error adding highg alert to metadata");
        return false;
    }
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

static string get_outcam_mirror_status ()
{
	return "4";
    FILE *fp = NULL;
    char buffer[10] = {'\0'};
    string mirror_status = "";

    fp = popen (outcam_mirror_status_cmd.c_str(), "r");
    if (!fp) {
        LOG_E (TAG,"Failed to execute %s", outcam_mirror_status_cmd.c_str());
        return "";
    }
    if (fgets (buffer, sizeof (buffer), fp)) {
        mirror_status = string (buffer);
        LOG_D (TAG, "mirror_status: %s", mirror_status.c_str());
        //Remove any surrounding whitespaces
        remove_leading_trailing_spaces(mirror_status);
    }
    pclose (fp);
    return mirror_status;
}

void check_and_set_ext_cam_fnames(int64_t time)
{
    for(int i=0;i<NUM_EXT_CAMERAS;i++) {
        if (ctx.ext_cam_enabled[i] == false) {
            continue;
        }

        stringstream ss;
        // external camera filenames start at offset 4
        ss << i + DEVICE_CAMERA_POSITION_MAX;
        string fname_prefix = ctx.fname[DEVICE_CAMERA_POSITION_FRONT];
        string fname = fname_prefix.substr(1, fname_prefix.length()-1);
        ss << fname;
        ctx.fname[i + DEVICE_CAMERA_POSITION_MAX] = ss.str();
        pthread_mutex_lock(&file_start_time_vec_mutex);
        file_start_time_vec_pair.push_back(make_pair(ctx.fname[i], time));
        pthread_mutex_unlock(&file_start_time_vec_mutex);
    }
}

void send_msg_mdvr_time_set() {
    generic_msg_t time_set_msg;
    if(send_msg(&time_set_msg, REQ_MDVR_TIME_SET,
                     sizeof( generic_msg_t ), get_msgq_name(), get_ext_cam_msgq_name(), 0)) {
        LOG_I(TAG, "sent message to ext_cam service to set mdvr time");
        return;
    }

    LOG_E(TAG, "failed to send msg to ext_cam service to set mdvr time");
}

bool check_fuel_report_configuration(){

   string temp;
   bool get_override_val = true;
   bool is_val_overridden = false;

    Config_parser obd_config_parser (BAGHEERACONFIG_INI);
    if (!obd_config_parser.getParseStatus()){
        LOG_E(TAG, "Can not parse bagheera config");
            return false;
    }
    temp = obd_config_parser.getConfig("driveri_one","fuel_report","0", get_override_val, is_val_overridden);
    if(!string_to_integer(temp, fuel_report)) {
        LOG_E(TAG, "failed to convert fuel_report to int");
    }
    if(fuel_report == 1){
        return true;
    }
    return false;
}

bool check_idling_report_configuration(){

   string temp;
   bool get_override_val = true;
   bool is_val_overridden = false;

    Config_parser obd_config_parser (BAGHEERACONFIG_INI);
    if (!obd_config_parser.getParseStatus()){
        LOG_E(TAG, "Can not parse bagheera config");
            return false;
    }
    temp = obd_config_parser.getConfig("driveri_one","idling_report","0", get_override_val, is_val_overridden);
    if(!string_to_integer(temp, idling_report)) {
        LOG_E(TAG, "failed to convert idling_report to int");
    }
    if(idling_report == 1){
        return true;
    }
    return false;
}

bool check_obd_service_configuration(){
   string temp,temp_retry_count,temp_retry_time;
   bool get_override_val = true;
   bool is_val_overridden = false;

   Config_parser obd_config_parser (BAGHEERACONFIG_INI);
    if (!obd_config_parser.getParseStatus()){
        LOG_E(TAG, "Can not parse bagheera config");
            return false;
    }
    temp = obd_config_parser.getConfig("vehicle_data","enabled","false", get_override_val, is_val_overridden);

    if (temp != "true"){
        LOG_I(TAG, "obd_service service is disabled");
        vdm_enabled = false;
        return false;
    }
    LOG_I(TAG, "obd_service service is enabled");
    vdm_enabled = true;

    temp_retry_count = obd_config_parser.getConfig("vehicle_data","retry_count", to_string(default_obd_data_retry_count), get_override_val, is_val_overridden);
    if (!string_to_integer(temp_retry_count, obd_data_retry_count)){
        LOG_E(TAG, "vehicle_data retry_count parse failed");
        obd_data_retry_count = default_obd_data_retry_count;
    }
    LOG_I(TAG, "vehicle_data retry_count set to %d: ",obd_data_retry_count);

    temp_retry_time = obd_config_parser.getConfig("vehicle_data","retry_time", to_string(default_obd_data_retry_time), get_override_val, is_val_overridden);
    if (!string_to_integer(temp_retry_time, obd_data_retry_time)){
        LOG_E(TAG, "vehicle_data retry_count parse failed");
        obd_data_retry_time = default_obd_data_retry_time;
    }
    LOG_I(TAG, "vehicle_data retry_time set to %d: ",obd_data_retry_time);

    return true;
}

bool vin_validation(int length){

      //check if junk char is in last or in between
     //if last char is junk we can take the vin
     //there is possibilty that last char may be *

     int i =0;
     int count = 0;

     unsigned char temp_vin[MAX_SIZE_OF_VIN] = {0};

     LOG_I(TAG, "initial VIN %s", obd_vin);

     for (i =0; i < length; i++){

         if( isalnum(obd_vin[i]) ) {
             temp_vin[count++]= obd_vin[i];
         }
         else if (obd_vin[i] == ' ' || obd_vin[i] == '*' ) {
              LOG_E(TAG, "vin has space/star so discardig them");
         }
         else{
              // discard junk char is in between
              LOG_E(TAG, "vehicle vin has junk char: %d in between so discarding", obd_vin[i]);
              memset(obd_vin, 0, sizeof(obd_vin));
              return false;
         }
     }

     memcpy(obd_vin, temp_vin, sizeof(temp_vin));
     return true;
}

void clone_message_received( req_livestreaming_data_t *req, req_livestreaming_data_t *new_msg, bool dual_streaming=false)
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
    strncpy(new_msg->stream_name, req->stream_name, sizeof(new_msg->stream_name));
    strncpy(new_msg->endpoint, req->endpoint, sizeof(new_msg->endpoint));
    strncpy(new_msg->resolution, req->resolution, sizeof(new_msg->resolution));
}

void convert_dual_to_two_single_streaming_requests(req_dual_livestreaming_data_t *dual_cam, req_livestreaming_data_t *outward, req_livestreaming_data_t *inward){
    outward->msg_type = dual_cam->msg_type;
    inward->msg_type = dual_cam->msg_type;
    outward->length = dual_cam->length;
    inward->length = dual_cam->length;
    strncpy(outward->client_id,dual_cam->client_id, sizeof(outward->client_id));
    strncpy(inward->client_id,dual_cam->client_id, sizeof(inward->client_id));
    outward->msg_idx = dual_cam->msg_idx;
    inward->msg_idx = dual_cam->msg_idx;
    outward->res_reqd = dual_cam->res_reqd;
    inward->res_reqd = dual_cam->res_reqd;
    outward->req_id = dual_cam->req_id;
    inward->req_id = dual_cam->req_id;
    outward->id = dual_cam->id;
    inward->id = dual_cam->id;

    outward->duration = dual_cam->duration;
    inward->duration = dual_cam->duration;
    strncpy(outward->endpoint, dual_cam->endpoint, sizeof(dual_cam->endpoint));
    strncpy(inward->endpoint, dual_cam->endpoint, sizeof(dual_cam->endpoint));
    outward->dual_streaming = true;
    inward->dual_streaming = true;
    outward->dual_streaming_active = 0;
    inward->dual_streaming_active = 0;
    if (dual_cam->streaming_cameras[0].camera == DEVICE_CAMERA_POSITION_FRONT){
        outward->camera = dual_cam->streaming_cameras[0].camera;
        inward->camera = dual_cam->streaming_cameras[1].camera;
        outward->fps = dual_cam->streaming_cameras[0].fps;
        inward->fps = dual_cam->streaming_cameras[1].fps;
        outward->bitrate = dual_cam->streaming_cameras[0].bitrate;
        inward->bitrate = dual_cam->streaming_cameras[1].bitrate;
        strncpy(outward->resolution, dual_cam->streaming_cameras[0].resolution, sizeof(outward->resolution));
        strncpy(inward->resolution, dual_cam->streaming_cameras[1].resolution, sizeof(inward->resolution));
        strncpy(outward->stream_name, dual_cam->streaming_cameras[0].stream_name, sizeof(outward->stream_name));
        strncpy(inward->stream_name, dual_cam->streaming_cameras[1].stream_name, sizeof(inward->stream_name));
    }
    else{
        outward->camera = dual_cam->streaming_cameras[1].camera;
        inward->camera = dual_cam->streaming_cameras[0].camera;
        outward->fps = dual_cam->streaming_cameras[1].fps;
        inward->fps = dual_cam->streaming_cameras[0].fps;
        outward->bitrate = dual_cam->streaming_cameras[1].bitrate;
        inward->bitrate = dual_cam->streaming_cameras[0].bitrate;
        strncpy(outward->resolution, dual_cam->streaming_cameras[1].resolution, sizeof(outward->resolution));
        strncpy(inward->resolution, dual_cam->streaming_cameras[0].resolution, sizeof(inward->resolution));
        strncpy(outward->stream_name, dual_cam->streaming_cameras[1].stream_name, sizeof(outward->stream_name));
        strncpy(inward->stream_name, dual_cam->streaming_cameras[0].stream_name, sizeof(inward->stream_name));
    }
}

string readFileToString(const string& filename) {
    std::lock_guard<std::mutex> lock(dualStreamingFileMutex);
    ifstream file(filename);
    if (!file_is_present(filename)) {
        LOG_I(TAG, "File not present: %s", filename.c_str());
        return "";
    }

    string content((istreambuf_iterator<char>(file)),
                        istreambuf_iterator<char>());

    file.close();
    // Remove newlines from the end of the content
    while (!content.empty() && content.back() == '\n') {
        content.pop_back();
    }
    return content;
}

live_stream_event_t check_audio_streaming_mode_start(bool dual_streaming, int camera){
    // If value is 1 for both outward and inward, then play dual audio start message
    // If value is 1 for outward and 0 for inward, then play outward audio start message
    // If value is 1 for inward and 0 for outward, then play inward audio start message
    if(dual_streaming && ctx.live_stream_audio_notification) {
        //lock mutex and release mutex after checking the state
        bool is_start_audio_played = dual_streaming_audio_start_played.load(std::memory_order_relaxed);
        if(is_start_audio_played) {
            LOG_I(TAG, "Dual streaming mode, audio start message already played");
            return INVALID;
        }
        string contentOutward = readFileToString(TMP_KINESIS_STREAMING_OUTWARD);
        string contentInward = readFileToString(TMP_KINESIS_STREAMING_INWARD);
        if (contentOutward == "1" && contentInward == "1") {
            LOG_I(TAG, "Dual streaming mode, play dual audio start message");
            send_live_streaming_status_audio_play(camera, DUAL_START);
            dual_streaming_audio_start_played.store(true, std::memory_order_relaxed);
            return DUAL_START;
        } else if (contentOutward == "1" && file_is_present(TMP_KINESIS_STREAMING_INWARD)) {
            LOG_I(TAG, "Dual streaming mode, play outward audio start message");
            send_live_streaming_status_audio_play(0, START);
            dual_streaming_audio_start_played.store(true, std::memory_order_relaxed);
            return START;
        } else if (file_is_present(TMP_KINESIS_STREAMING_OUTWARD) && contentInward == "1") {
            LOG_I(TAG, "Dual streaming mode, play inward audio start message");
            send_live_streaming_status_audio_play(1, START);
            dual_streaming_audio_start_played.store(true, std::memory_order_relaxed);
            return START;
        } else {
            LOG_I(TAG, "Dual streaming mode not ready, wait for 1 second");
            return INVALID;
        }
    }
}

void check_audio_streaming_mode_stop(bool dual_streaming, int camera, live_stream_event_t curr_event){
    if(dual_streaming && ctx.live_stream_audio_notification) {
        bool is_end_audio_played = dual_streaming_audio_end_played.load(std::memory_order_relaxed);
        if(is_end_audio_played) {
            LOG_I(TAG, "Dual streaming mode, audio end message already played");
            return;
        }
        if(curr_event == DUAL_END){
            LOG_I(TAG, "Dual streaming mode, play dual audio end message");
            send_live_streaming_status_audio_play(camera, DUAL_END);
            dual_streaming_audio_end_played.store(true, std::memory_order_relaxed);
        }
        else if(camera == 0 && curr_event == END){
            LOG_I(TAG, "Dual streaming mode, play outward audio end message");
            send_live_streaming_status_audio_play(0, END);
            dual_streaming_audio_end_played.store(true, std::memory_order_relaxed);
        }
        else if(camera == 1 && curr_event == END){
            LOG_I(TAG, "Dual streaming mode, play inward audio end message");
            send_live_streaming_status_audio_play(1, END);
            dual_streaming_audio_end_played.store(true, std::memory_order_relaxed);
        }
        else {
            LOG_I(TAG, "Dual streaming mode not ready, wait for 1 second");
            return;
        }
    }
}

void *livestreaming_timer( void *arg)
{
    req_livestreaming_data_t *kinesis_req_msg = (req_livestreaming_data_t *)arg;
    req_livestreaming_data_t new_msg;
    clone_message_received( kinesis_req_msg, &new_msg);
    new_msg.force_stop = false; // force_stop is false by default for every new request
    int duration = new_msg.duration;
    unsigned int connection_check_cnt = 4;
    int waiting_counter = 0;
    if(new_msg.dual_streaming){
        // This is to increase the duration of dual live streaming by 3 seconds
        // to overcome latency during start for average case scenario
        duration = duration + 3;
    }
    live_stream_event_t curr_event = INVALID;
    LOG_I(TAG, "In live streaming timer");
    bool passed_once = false;
    bool passed_once_audio = false;
    bool audio_played = false;
    while (duration > 0 && waiting_counter < 60) {
        sleep(1);
        LOG_I(TAG, "livestreaming time left %d", duration);
        audio_played = dual_streaming_audio_start_played.load(std::memory_order_relaxed);
        if(new_msg.dual_streaming && !audio_played && !passed_once_audio){
            curr_event = check_audio_streaming_mode_start(new_msg.dual_streaming, new_msg.camera);
            if(curr_event != INVALID){
                passed_once_audio = true;
            }
            else{
                waiting_counter++;
                if(waiting_counter >= 60){
                    LOG_I(TAG, "closing live streaming request, duration exceeded for both cameras to start");
                }
            }
        }
        if ((duration % connection_check_cnt) == 0) {
            // check for connection
            if (!check_internet_exist()) {
                LOG_I(TAG, "No internet, unable to continue live streaming");
                duration = duration + 10;
                connection_check_cnt = 10;	// reduce overhead
            }
        }

        /* check for framedrops, i.e live_stream_force_stop and connection lost */
        if ((new_msg.camera == DEVICE_CAMERA_POSITION_FRONT) && live_stream_force_stop_outward && (connection_check_cnt == 10)) {
            LOG_I(TAG, "break live stream timer outward");
            // enable flag to forcefully stop present request, force_stop is false by default for every new request
            new_msg.force_stop = true;
            pthread_mutex_lock(&live_stream_flag);
            live_stream_force_stop_outward = false;
            pthread_mutex_unlock(&live_stream_flag);
            break;
        }
        else if((new_msg.camera == DEVICE_CAMERA_POSITION_BACK) && live_stream_force_stop_inward && (connection_check_cnt == 10)){
            LOG_I(TAG, "break live stream timer inward");
            // enable flag to forcefully stop present request, force_stop is false by default for every new request
            new_msg.force_stop = true;
            pthread_mutex_lock(&live_stream_flag);
            live_stream_force_stop_inward = false;
            pthread_mutex_unlock(&live_stream_flag);
            break;
        }
        if(!passed_once && new_msg.dual_streaming && !(file_is_present(TMP_KINESIS_STREAMING_OUTWARD) && file_is_present(TMP_KINESIS_STREAMING_INWARD))){
            continue;
        }
        passed_once = true;
        duration--;
    }

    LOG_I(TAG, "live streaming time completed");
    if(curr_event != INVALID){
        check_audio_streaming_mode_stop(new_msg.dual_streaming, new_msg.camera, audioPlayedMap.at(curr_event));
    }
    else{
        LOG_I(TAG, "live streaming time completed -- no audio played");
    }
    if (!send_msg((generic_msg_t *)&new_msg, (msg_type_t)STOP_LIVE_STREAMING,
                sizeof(new_msg), get_msgq_name(), "q_nd_central", 0)) {
        LOG_E(TAG, "Cannot send message to nd-central to stop kinesis streaming");
    }
    return NULL;
}

void livestream_framedrop_cb(int cam_num)
{
	LOG_I(TAG, "LIVE_STREAM stopping due to continous frame dropping");
	pthread_mutex_lock(&live_stream_flag);
    if(cam_num == DEVICE_CAMERA_POSITION_FRONT){
        live_stream_force_stop_outward = true;
    }
    else if(cam_num == DEVICE_CAMERA_POSITION_BACK){
        live_stream_force_stop_inward = true;
    }
	pthread_mutex_unlock(&live_stream_flag);
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
    LOG_I(TAG,"Live streaming error codes: %d %d", msg_res.error_cam_one, msg_res.error_cam_two);
    msg_status = send_msg((generic_msg_t*)&msg_res, RES_LIVE_STREAM, sizeof(msg_res), get_msgq_name(), "AWSIOT", 0);
    if (msg_status == false) {
        LOG_E(TAG, "LIVE_STREAMING_DONE for outward camera not sent to client, client_id: AWSIOT");
    } else {
        LOG_I(TAG, "LIVE_STREAMING_DONE for outward camera sent to client, client_id: AWSIOT");
    }
}

bool trigger_live_streaming(req_livestreaming_data_t *kinesis_req_msg, int& streaming){
    if (!start_kinesis(kinesis_req_msg)) {
        // TODO: Catch the LS error returned by gst_recorder
        LOG_I (TAG,"start kinesis failed for requested camera");
        return false;
    }
    LOG_I(TAG,"live streaming duration: %d", kinesis_req_msg->duration);
    // live_stream_duration = kinesis_req_msg->duration;
    pthread_t live_streaming;
    if (!pthread_create (&live_streaming, NULL, &livestreaming_timer, kinesis_req_msg)) {
        LOG_I (TAG, "Live streaming timer thread started for requested camera");
    }
    streaming = 1;
    return true;
}

void check_start_outward_livestreaming(req_livestreaming_data_t *kinesis_req_msg, int& streaming_outward){
    if (cams_enabled[DEVICE_CAMERA_POSITION_FRONT] == false) {
        LOG_E(TAG, "Camera %d is disabled. Not streaming", kinesis_req_msg->camera);
        if(!kinesis_req_msg->dual_streaming){
            send_livestreaming_completion_msg( kinesis_req_msg, LIVE_STREAMING_ERR, LS_ERR_CAM_DISABLED);
        }
        else{
            //required so that dual streaming can continue
            file_touch(TMP_KINESIS_STREAMING_OUTWARD);
            // send partial error state to cloud
            send_livestreaming_completion_msg( kinesis_req_msg, LIVE_STREAMING_ERR, LS_ERR_CAM_DISABLED, LS_ERR_DEFAULT);
        }
        return;
    }
    if ((kinesis_req_msg->camera == DEVICE_CAMERA_POSITION_FRONT) &&
         ((ctx.fused_privacy && ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_FRONT]
          && !ctx.privacy_params.enhanced_privacy) || ctx.off_duty_privacy || ctx.geofence_privacy)) {
        LOG_E(TAG, "privacy enabled. Cannot stream outward");
        if(!kinesis_req_msg->dual_streaming){
            send_livestreaming_completion_msg( kinesis_req_msg, LIVE_STREAMING_ERR, LS_ERR_PRIVACY_ENABLED);
        }
        else{
            //required so that dual streaming can continue
            file_touch(TMP_KINESIS_STREAMING_OUTWARD);
            // send partial error state to cloud
            send_livestreaming_completion_msg( kinesis_req_msg, LIVE_STREAMING_ERR, LS_ERR_PRIVACY_ENABLED, LS_ERR_DEFAULT);
        }
        return;
    }
    if(!streaming_outward){
        bool trigger_live_streaming_status = trigger_live_streaming(kinesis_req_msg, streaming_outward);
        if(!trigger_live_streaming_status){
            if(!kinesis_req_msg->dual_streaming){
                send_livestreaming_completion_msg( kinesis_req_msg, LIVE_STREAMING_ERR, kinesis_req_msg->error);
            }
            else{
                send_livestreaming_completion_msg( kinesis_req_msg, LIVE_STREAMING_ERR, kinesis_req_msg->error, LS_ERR_DEFAULT);
            }
        }
    }
    else{
        LOG_E(TAG,"streaming is already requested for this camera, cannot honor the current request");
    }
}

void check_start_inward_livestreaming(req_livestreaming_data_t *kinesis_req_msg, int& streaming_inward){
    if (cams_enabled[DEVICE_CAMERA_POSITION_BACK] == false) {
        LOG_E(TAG, "Camera %d is disabled. Not streaming", kinesis_req_msg->camera);
        if(!kinesis_req_msg->dual_streaming){
            send_livestreaming_completion_msg( kinesis_req_msg, LIVE_STREAMING_ERR, LS_ERR_CAM_DISABLED);
        }
        else{
            //required so that dual streaming can continue
            file_touch(TMP_KINESIS_STREAMING_INWARD);
            // send partial error state to cloud
            send_livestreaming_completion_msg( kinesis_req_msg, LIVE_STREAMING_ERR, LS_ERR_DEFAULT, LS_ERR_CAM_DISABLED);
        }
        return;
    }
    // Streaming ongoing check is done in cam_recorder service
    if (ctx.inward_privacy_state) {
        LOG_E(TAG, "privacy enabled. Cannot stream inward");
        if(!kinesis_req_msg->dual_streaming){
            send_livestreaming_completion_msg( kinesis_req_msg, LIVE_STREAMING_ERR, LS_ERR_PRIVACY_ENABLED);
        }
        else{
            //required so that dual streaming can continue
            file_touch(TMP_KINESIS_STREAMING_INWARD);
            // send partial error state to cloud
            send_livestreaming_completion_msg( kinesis_req_msg, LIVE_STREAMING_ERR, LS_ERR_DEFAULT, LS_ERR_PRIVACY_ENABLED);
        }
        return;
    }
#ifdef BAGHEERA2
        LOG_I(TAG, "send_start_livestream_msg_to_cam_rec_service");
        send_start_livestream_msg_to_cam_rec_service(Q_NAME, kinesis_req_msg);
#else
        if(!streaming_inward){
            bool trigger_live_streaming_status = trigger_live_streaming(kinesis_req_msg, streaming_inward);
            if(!trigger_live_streaming_status){
                if(!kinesis_req_msg->dual_streaming){
                    send_livestreaming_completion_msg( kinesis_req_msg, LIVE_STREAMING_ERR, kinesis_req_msg->error);
                }
                else{
                    send_livestreaming_completion_msg( kinesis_req_msg, LIVE_STREAMING_ERR, LS_ERR_DEFAULT, kinesis_req_msg->error);
                }
            }
        }
        else{
            LOG_E(TAG,"streaming is already requested for this camera, cannot honor the current request");
        }
#endif
}

bool qr_scan_callback(unordered_map<string, vector<string>> qr_scan_out, int num_qr_codes_detected, uint64_t qr_frame_ts, qr_scan_status qrscan_status)
{
    LOG_I(TAG, "QR scan callback called with %d QR codes detected, status: %d", num_qr_codes_detected, qrscan_status);

    ctx.qr_codes_detected[ctx.session_flipflop] += num_qr_codes_detected;

    if (qrscan_status == QR_SCAN_NO_QR) {
        ctx.qr_code_scan_status = 0; //00b
    } else if (qrscan_status == QR_SCAN_QR_DETECTED) {
        ctx.qr_code_scan_status = 1; //01b
        if (!qr_scan_out.empty()) {
            LOG_E(TAG, "It looks like a Non-ND QR code is scanned at timestamp %llu", qr_frame_ts);
            nd_service_obj->send_err_msg(SM_E_NDC_QRSCAN_INVALID_QR_CODE, NDService::UNUSED_ERR_AUX_CODE, "Non-ND QR code scanned");
            ctx.qr_codes_mismatched[ctx.session_flipflop] += num_qr_codes_detected;
        }
    } else if (qrscan_status == QR_SCAN_QR_DECODED) {
        ctx.qr_code_scan_status = 3; //11b

        int num_ids = 0;

        // Create JSON object in the specified format
        json_t *root = json_object();
        json_t *data_array = json_array();

        // Loop through all saved QR scan tags
        for (int tag_index = 0; tag_index < kQRScanMaxTagPatterns; tag_index++) {
            if (strlen(saved_qr_scan_tags[tag_index]) == 0) {
                continue; // Skip empty tags
            }

            string tag_name = string(saved_qr_scan_tags[tag_index]);
            LOG_D(TAG, "Checking for tag: %s", tag_name.c_str());

            // Check if this tag exists in the QR scan output
            auto tag_it = qr_scan_out.find(tag_name);
            if (tag_it != qr_scan_out.end()) {
                vector<string> tag_values_vec = tag_it->second;

                // Loop through the vector and extract values from each string
                for (const auto& tag_value_str : tag_values_vec) {
                    // Parse the JSON string if it's in the format {"TagName": "value"}
                    json_error_t error;
                    json_t *parsed_json = json_loads(tag_value_str.c_str(), 0, &error);
                    if (parsed_json != nullptr && json_is_object(parsed_json)) {
                        // Extract the tag value from the parsed JSON
                        json_t *tag_value_json = json_object_get(parsed_json, tag_name.c_str());
                        if (tag_value_json != nullptr && json_is_string(tag_value_json)) {
                            const char *tag_value = json_string_value(tag_value_json);
                            // Create individual tag object and add to array
                            json_t *tag_obj = json_object();
                            json_object_set_new(tag_obj, tag_name.c_str(), json_string(tag_value));
                            json_array_append_new(data_array, tag_obj);

                            num_ids++;
                            LOG_I(TAG, "Found valid tag '%s' with value '%s'", tag_name.c_str(), tag_value);
                        } else {
                            LOG_E(TAG, "%s value is not a string in parsed JSON", tag_name.c_str());
                        }
                        // Clean up parsed JSON object
                        json_decref(parsed_json);
                    } else {
                        LOG_W(TAG, "Failed to parse JSON string for tag '%s': %s", tag_name.c_str(), tag_value_str.c_str());
                    }
                }
            }
        }

        LOG_I(TAG, "Extracted %d tag values from QR scan", num_ids);

        ctx.qr_codes_decoded[ctx.session_flipflop] += num_ids;

        if (num_ids > 0) {
            // Add data array and login_time to root JSON object
            json_object_set_new(root, "data", data_array);
            json_object_set_new(root, "login_time", json_integer(qr_frame_ts));

            // Convert JSON to string for publishing
            char *json_string_data = json_dumps(root, JSON_COMPACT);
            if (json_string_data) {
                LOG_I(TAG, "QR scan JSON data: %s", json_string_data);
                // Publish the JSON data
                qr_scan_data_publisher.setMessage(json_string_data).publish();
                // Free the JSON string
                free(json_string_data);
            } else {
                LOG_E(TAG, "Failed to convert QR scan data to JSON string");
            }
        }
        // Clean up JSON objects
        json_decref(root);
    } else if (qrscan_status == QR_SCAN_TIMEDOUT) {
        LOG_I(TAG, "QR scan has timed out, stopping the scan and restoring IRLEDs to saved state");
#ifdef BAGHEERA2
        file_delete(qr_scan_started_file);
#endif
        pthread_mutex_lock(&qr_scan_start_mutex);

        qr_scan_started = false;
        ctx.qr_code_scan_status = 0;

        // Stop the IR LED blinking thread
        if (qr_scan_irled_thread_running) {
            pthread_join(qr_scan_irled_thread, NULL);
        }

        check_set_irled(saved_irled_data.reason, saved_irled_data.value);

        pthread_mutex_unlock(&qr_scan_start_mutex);

        // Add QR scan stop time to healthstats
        qr_scan_stop_timestamp = get_system_time();
        char *req_params;
        json_t *root = json_object();
        json_t *qr_scan_info = json_object();
        json_object_set_new(qr_scan_info, "qr_scan_stop_time", json_integer(qr_scan_stop_timestamp));
        json_object_set_new(root, "qr_scan_info", qr_scan_info);
        json_object_set_new(root, "isArray", json_string("true"));

        req_params = json_dumps(root, 0);
        if (req_params == NULL) {
            LOG_E(TAG, "JSON creation failed for QR scan stop healthstats message");
            json_decref(root);
        } else {
            LOG_I(TAG, "Sending QR scan stop time to healthstats: %s", req_params);
            int length = strlen(req_params);
            nd_service_obj->send_msg_healthstats(req_params, length);
            json_decref(root);
            free(req_params);
        }
        resp_qr_login_scan_status_msg_t qr_scan_stop_status_msg;
        // Initialize response message
        memset(&qr_scan_stop_status_msg, 0, sizeof(qr_scan_stop_status_msg));
        qr_scan_stop_status_msg.msg_type = STOP_QR_SCAN_RES;
        qr_scan_stop_status_msg.length = sizeof(resp_qr_login_scan_status_msg_t);
        qr_scan_stop_status_msg.status = QR_SCAN_STATUS_TIMEOUT;
        nd_strncpy(qr_scan_stop_status_msg.reason, "QR scan has timedout", sizeof(qr_scan_stop_status_msg.reason));

        send_msg((generic_msg_t*)&qr_scan_stop_status_msg, STOP_QR_SCAN_RES,
                 sizeof(qr_scan_stop_status_msg), get_msgq_name(), Q_NAME_BTFV, 0);
    }

    return true;
}

bool ndmb_engine_status_cb(ndmb_generic_msg_t *msg){
    ndmbmsg_engine_status_t *ptr1;
    if (msg->topic == TOPIC_ENGINE_STATUS){
        ptr1 = reinterpret_cast<ndmbmsg_engine_status_t *>( msg );
        LOG_I(TAG, "IOSiX Engine status %d", ptr1->engine_status);
        engine_status= ptr1->engine_status;
        LOG_I(TAG, "Engine status %d", engine_status);
    }
}

bool ndmb_gps_cb(ndmb_generic_msg_t *msg)
{
    if(msg == nullptr)
    {
        LOG_E(TAG, "ndmb_gps_cb Invalid msg");
        return false;
    }   
    gps_msg_t *ptr1;
    string topic = msg->topic;
    if(topic == TOPIC_GPS_DATA)
    {
        ptr1 = reinterpret_cast<gps_msg_t *>( msg );
        LOG_D(TAG, "GPS Debug: msg pointer = %p, aligned = %s", msg, ((uintptr_t)msg % sizeof(double) == 0) ? "YES" : "NO");
        LOG_D(TAG, "GPS Debug: msg_length = %d, expected = %zu", msg->msg_length, sizeof(gps_msg_t));
        gps_msg_t gps_msg;
        memset(&gps_msg, 0, sizeof(gps_msg_t));
        memcpy(&gps_msg, msg, sizeof(gps_msg_t));
       

        Gps::gps_data_t data;
        memset(&data, 0, sizeof(data));
        data.valid      = gps_msg.valid;
        data.latitude   = gps_msg.latitude;
        data.longitude  = gps_msg.longitude;
        data.altitude   = gps_msg.altitude;
        data.speed      = gps_msg.speed;
        data.bearing    = gps_msg.bearing;
        data.accuracy   = gps_msg.accuracy;
        data.timestamp  = gps_msg.timestamp;
        data.system_timestamp = gps_msg.system_timestamp;
        
        Gps::gps_sensor_data_t extended_gps_data;
        extended_gps_data.gps_data = data;
        extended_gps_data.gps_index = gps_msg.gps_index;
        extended_gps_data.raw_time_micro = gps_msg.raw_time_ns;
        extended_gps_data.good_satellites = gps_msg.good_sattelites;
        extended_gps_data.fix_quality = gps_msg.fix_quality;

     

        LOG_D(TAG, "GPS CB Valid %d, Lat %f, Lon %f, Alt %f, Accuracy %f, Timestamp %lld, system_timestamp %lld, raw_time_stamp %lld",
              extended_gps_data.gps_data.valid,
              extended_gps_data.gps_data.latitude,
              extended_gps_data.gps_data.longitude,
              extended_gps_data.gps_data.altitude,
              extended_gps_data.gps_data.accuracy,
              extended_gps_data.gps_data.timestamp,
              extended_gps_data.gps_data.system_timestamp,
              extended_gps_data.raw_time_micro);
        // Call the GPS callback with the extended GPS data
        gps_callback( extended_gps_data);
    }
    else
    {
	    LOG_E(TAG, "ndmb_gps_cb Invalid topic %s", topic.c_str());
        return false;
    }
    return true;
}
bool ndmb_gps_pps_cb(ndmb_generic_msg_t *msg)
{
    ndmb_gps_pps_data_t *ptr1;
    LOG_I(TAG, "GPS PPS Data =========================== Topic %s", msg->topic);
    string topic = msg->topic;
    if (topic == TOPIC_GPS_PPS_DATA)
    {
        ptr1 = reinterpret_cast<ndmb_gps_pps_data_t *>( msg );
        LOG_I(TAG, "PPS Index      = [%d]", ptr1->pps_index);
        LOG_I(TAG, "PPS clock time = [%ld]", ptr1->pps_clock_time);
        LOG_I(TAG, "PPS raw time   = [%ld]", ptr1->pps_raw_time);
        Gps::gps_pps_data_t data;
        data.pps_index      = ptr1->pps_index;
        data.pps_raw_time   = ptr1->pps_raw_time;
        data.pps_clock_time = ptr1->pps_clock_time;
        gps_pps_callback(data);
    }
    else
    {
        LOG_E(TAG, "ndmb_gps_pps_cb Invalid topic %s", topic.c_str());
        return false;
    }
    return true;
 
}

static bool send_privacy_status_audio_play(privacy_type_t privacy_mode, bool enabled) {

    bool status = false;

    if ((privacy_mode == REGULAR || privacy_mode == ENHANCED) && !cams_enabled[DEVICE_CAMERA_POSITION_BACK]) {
        LOG_I(TAG,"Not playing privacy audio because inward camera is disabled");
        return status;
    }
    string session_name = cur_session_fnames[DEVICE_CAMERA_POSITION_FRONT];
    string alert_type = "PrivacySt";
    string event_code = "eventCode";
    string uuid = get_new_uuid();
    uint64_t curr_time = (uint64_t)get_system_time();

    if (((ctx.privacy_params.privacy_deactivate_params.transition_audio_feedback == false) && (enabled == false)) ||
        ((ctx.privacy_params.privacy_activate_params.transition_audio_feedback == false) && (enabled == true))) {
        LOG_I(TAG, "privacy transition audio alert is disabled");
        return status;
    }

    if (privacy_mode == REGULAR && (ctx.off_duty_privacy || ctx.geofence_privacy || ctx.privacy_params.enhanced_privacy)) {
        LOG_I(TAG, "Regular Privacy audio came, when vehicle is in off-duty mode or geofence mode or Enhanced_privacy mode. Ignoring it");
        return status;
    }

    do {
        string file_name = "";

        if (privacy_mode == REGULAR) {
            if (enabled) {
                file_name = ctx.privacy_params.privacy_activate_params.transition_audio_alert_file_regular;
                if (!file_is_present(file_name)) {
                    LOG_E(TAG, "Audio file %s is not present, will play English audio", file_name.c_str());
                    file_name = default_privacy_activated_audio;
                }
            }
            else {
                file_name = ctx.privacy_params.privacy_deactivate_params.transition_audio_alert_file_regular;
                if (!file_is_present(file_name)) {
                    LOG_E(TAG, "Audio file %s is not present, will play English audio", file_name.c_str());
                    file_name = default_privacy_deactivated_audio;
                }
            }
        } else if (privacy_mode == ENHANCED) {
            if (enabled) {
                file_name = ctx.privacy_params.privacy_activate_params.transition_audio_alert_file_enhanced;
                if (!file_is_present(file_name)) {
                    LOG_E(TAG, "Audio file %s is not present, will play English audio", file_name.c_str());
                    file_name = default_enhanced_privacy_activated_audio;
                }
            }
        } else if (privacy_mode == OFF_DUTY) {
            if (enabled) {
                file_name = ctx.privacy_params.privacy_activate_params.transition_audio_alert_file_offduty;
                if (!file_is_present(file_name)) {
                    LOG_E(TAG, "Audio file %s is not present, will play English audio", file_name.c_str());
                    file_name = default_offduty_activated_audio;
                }
            }
            else {
                file_name = ctx.privacy_params.privacy_deactivate_params.transition_audio_alert_file_offduty;
                if (!file_is_present(file_name)) {
                    LOG_E(TAG, "Audio file %s is not present, will play English audio", file_name.c_str());
                    file_name = default_offduty_deactivated_audio;
                }
            }
        }

        if (!file_name.size()) {
            LOG_E(TAG, "Audio file name string for privacy status is NULL");
            break;
        }

        if (!file_is_present(file_name)) {
            LOG_E(TAG, "Audio file %s is not present", file_name.c_str());
            break;
        }

        if (uuid.empty()) {
            LOG_E(TAG, "%s uuid is empty, using dummy", __func__);
            uuid = "uuid";
        }

        std::cout << "session_name: " << session_name << " alert_type: " << alert_type << " event_code: " << event_code << " uuid: " << uuid << " file_name: " << file_name << " curr_time: " << curr_time << std::endl;

        LOG_I(TAG, "send audio play with uuid: %s", uuid.c_str());

        nd_audio::AudioData audio_data{};

        audio_data.set_session_name(session_name);
        audio_data.set_alert_type(alert_type);
        audio_data.set_event_code(event_code);
        audio_data.set_uuid(uuid);
        audio_data.set_frame_gen_time(curr_time);
        audio_data.set_issue_time(curr_time);
        audio_data.set_acceptable_latency(5000);
        audio_data.set_volume(100);
        audio_data.set_play_audio(true);
        audio_data.set_audio_file_name(file_name);

        // Add Audio Request
        add_audio_request(file_name, AudioEventType::PrivacySt);

        if (!send_audio_play(audio_data)) {
            LOG_E(TAG, "send_audio_play failed");
            break;
        }

        status = true;

    } while (false);

    return status;
}

static bool register_with_speed_for_privacy () {

    req_idle_reg_msg_t req;

    LOG_I (TAG,"privacy idle registration");
    req.speed = ctx.privacy_params.privacy_activate_params.threshold_speed;
    req.idle_secs = ctx.privacy_params.privacy_activate_params.threshold_time;
    req.reg_type = IDLE_REG_PRIVACY;

    if (false == send_msg((generic_msg_t *)&req, REQ_IDLE_REG, sizeof(req), get_msgq_name(), "SPEED", msg_idx++)) {
        return false;
    }
    return true;
}

static bool register_with_speed_for_speed () {
    req_speed_reg_msg_t req;

    req.speed = ctx.privacy_params.privacy_deactivate_params.threshold_speed;
    req.contig_secs = ctx.privacy_params.privacy_deactivate_params.threshold_time;

    req.reg_type = SPEED_REG_PRIVACY;

    if (false == send_msg((generic_msg_t *)&req, REQ_SPEED_REG, sizeof(req), get_msgq_name(), "SPEED", msg_idx++)) {
        LOG_E(TAG, "Not able to register for speed");
        return false;
    }
    return true;
}

bool send_privacy_update(int cam_num, bool old_privacy, bool new_privacy)
{
    if (cam_num == DEVICE_CAMERA_POSITION_BACK) {
        if ((old_privacy && ctx.privacy_params.cam_privacy[cam_num])!= (new_privacy && ctx.privacy_params.cam_privacy[cam_num])) {
            LOG_I(TAG, "Privacy mode changed for camera %d from %d to %d", cam_num, old_privacy, new_privacy);
            bool inward_privacy_state = new_privacy && ctx.privacy_params.cam_privacy[cam_num];
#ifdef BAGHEERA2
            LOG_I(TAG, "send_privacy_update_msg to_cam_rec_service due to privacy %d",inward_privacy_state);
            send_privacy_update_msg_to_cam_rec_service(Q_NAME, inward_privacy_state);
#elif KRAIT
            ctx.media_recorder[DEVICE_CAMERA_POSITION_BACK]->set_privacy(DEVICE_CAMERA_POSITION_BACK, inward_privacy_state);
#endif
        }
    } else if (cam_num == DEVICE_CAMERA_POSITION_FRONT) {
        if ((old_privacy && ctx.privacy_params.cam_privacy[cam_num]) != (new_privacy && ctx.privacy_params.cam_privacy[cam_num])) {
            bool outward_privacy_status = new_privacy && ctx.privacy_params.cam_privacy[cam_num];
            ctx.media_recorder[cam_num]->set_privacy(cam_num, outward_privacy_status);
        }
    }
}

//engine_idle is there in the argument just to maintain the legacy code. This feature will be always disabled
//and the engine_idle will always be false.
void activate_geofence_privacy (string cur_session_fname, bool engine_idle)
{
    if (ctx.geofence_privacy) {
        LOG_C(TAG, "ignoring this event since geofence mode is already enabled");
        return;
    }

    ctx.geofence_privacy = true;
    ctx.inward_privacy_state = ctx.geofence_privacy;
    bool old_fused_privacy = ctx.fused_privacy;
    ctx.fused_privacy = ctx.geofence_privacy;
    LOG_I(TAG, "Geofence privacy mode is Activated");
#ifdef BAGHEERA2
    LOG_I(TAG, "send_privacy_update_msg to_cam_rec_service due to geofence privacy turned on");
    send_privacy_update_msg_to_cam_rec_service(Q_NAME, true);
#elif KRAIT
    ctx.media_recorder[DEVICE_CAMERA_POSITION_BACK]->set_privacy(DEVICE_CAMERA_POSITION_BACK, true);
#endif
    ctx.media_recorder[DEVICE_CAMERA_POSITION_FRONT]->set_privacy(DEVICE_CAMERA_POSITION_FRONT, true);

    stringstream ss_p_meta("");
    ss_p_meta.str("");
    ss_p_meta << "geofence_privacy" << ", " << ctx.geofence_privacy;
    ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;
    LOG_I(TAG, "Wrote geofence_privacy=%d to partial CSV", ctx.geofence_privacy);

    if (cur_session_fname != "") {
        int num_files_recording = get_num_digital_cam_files_recording();

        set_device_mode_for_fname(cur_session_fname, ctx.geofence_privacy, engine_idle, num_files_recording, REASON_GEOFENCE);
        LOG_I(TAG, "Set device mode for session %s with REASON_GEOFENCE", cur_session_fname.c_str());
        set_privacy_mode_led(ctx.inward_privacy_state);
        write_privacy_state_file(ctx.geofence_privacy);
        check_set_irled(REASON_PRIVACY);
    }
    return;
}

void deactivate_geofence_privacy (string cur_session_fname, bool engine_idle)
{
    if (!ctx.geofence_privacy) {
        LOG_C(TAG, "geofence mode is not enabled, nothing to deactivate");
        return;
    }

    bool old_privacy = ctx.geofence_privacy;
    ctx.geofence_privacy = false;
    LOG_I(TAG, "Geofence privacy mode is Deactivated");

    // Always record geofence deactivation event, regardless of enhanced privacy
    if (cur_session_fname != "") {
        int num_files_recording = get_num_digital_cam_files_recording();
        set_device_mode_for_fname(cur_session_fname, false, engine_idle, num_files_recording, REASON_GEOFENCE);
        LOG_I(TAG, "Set device mode for session %s with REASON_GEOFENCE (deactivated)", cur_session_fname.c_str());
        
        // Also record offduty state if it's currently active
        if (ctx.off_duty_privacy) {
            set_device_mode_for_fname(cur_session_fname, ctx.off_duty_privacy, engine_idle, num_files_recording, REASON_OFFDUTY);
            LOG_I(TAG, "Set device mode for session %s with REASON_OFFDUTY (state=%d)", cur_session_fname.c_str(), ctx.off_duty_privacy);

        } else if (ctx.privacy_params.enhanced_privacy) {
            // Enhanced privacy mode remains active after geofence exit
            activate_enhanced_privacy_mode(cur_session_fname);
        } else {
            bool old_fused = ctx.fused_privacy;
            bool ret = read_privacy_value_and_reason_from_file(ctx.fused_privacy, privacy_reason, privacy_state_file_global);
            if (old_fused != ctx.fused_privacy) {
                LOG_C(TAG, "ctx.fused_privacy changed: %d → %d (geofence deactivated, read from file, enhanced=%d, offduty=%d)",
                    old_fused, ctx.fused_privacy, ctx.privacy_params.enhanced_privacy, ctx.off_duty_privacy);
            }
            write_privacy_state_file(ctx.fused_privacy);
            if (cur_session_fname != "") {
	    	int num_files_recording = get_num_digital_cam_files_recording();
                // Set the regular privacy state after geofence deactivation
                set_device_mode_for_fname(cur_session_fname, ctx.fused_privacy, engine_idle, num_files_recording, privacy_reason);
                set_privacy_mode_led(ctx.fused_privacy && ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_BACK]);
                check_set_irled(REASON_PRIVACY);
            }
            if (ctx.fused_privacy == true) {
                LOG_I(TAG, "Privacy Mode is Activated");
                send_privacy_status_audio_play(REGULAR, true);
            }
        }
        
        // Update inward_privacy_state BEFORE calling send_privacy_update
        if (!ctx.privacy_params.enhanced_privacy && !ctx.off_duty_privacy && !ctx.geofence_privacy)
            ctx.inward_privacy_state = ctx.fused_privacy && ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_BACK];
        else
            ctx.inward_privacy_state = true;
        
        send_privacy_update(DEVICE_CAMERA_POSITION_BACK, old_privacy, ctx.fused_privacy);
        send_privacy_update(DEVICE_CAMERA_POSITION_FRONT, old_privacy, ctx.fused_privacy);

        stringstream ss_p_meta("");
        ss_p_meta.str("");
        ss_p_meta << "geofence_privacy" << ", " << ctx.geofence_privacy;
        ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;
        LOG_I(TAG, "Wrote geofence_privacy=%d to partial CSV after deactivation", ctx.geofence_privacy);
    }
    return;
}

void activate_off_duty_privacy (string cur_session_fname, bool engine_idle, bool play_audio = true)
{
    if (file_is_present(offduty_mode_activated_file)) {
        LOG_C(TAG, "ignoring this event since off duty mode is already enabled");
        return;
    }
    file_touch(offduty_mode_activated_file);

    ctx.off_duty_privacy = true;
    ctx.inward_privacy_state = ctx.off_duty_privacy;
    ctx.fused_privacy = ctx.off_duty_privacy;
    LOG_I(TAG, "Off duty mode is Activated");
    if (play_audio) {
        send_privacy_status_audio_play(OFF_DUTY, true);
    }
#ifdef BAGHEERA2
    LOG_I(TAG, "send_privacy_update_msg to_cam_rec_service due to privacy turned on");
    send_privacy_update_msg_to_cam_rec_service(Q_NAME, true);
#elif KRAIT
    ctx.media_recorder[DEVICE_CAMERA_POSITION_BACK]->set_privacy(DEVICE_CAMERA_POSITION_BACK, true);
#endif
    ctx.media_recorder[DEVICE_CAMERA_POSITION_FRONT]->set_privacy(DEVICE_CAMERA_POSITION_FRONT, true);

    stringstream ss_p_meta("");
    ss_p_meta.str("");
    ss_p_meta << "personal_privacy" << ", " << ctx.off_duty_privacy;
    ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;

    if (cur_session_fname != "") {
        int num_files_recording = get_num_digital_cam_files_recording();

        set_device_mode_for_fname(cur_session_fname, ctx.off_duty_privacy, engine_idle, num_files_recording, REASON_OFFDUTY);
        set_privacy_mode_led(ctx.inward_privacy_state);
        write_privacy_state_file(ctx.off_duty_privacy);
        check_set_irled(REASON_PRIVACY);
    }

    send_driver_initiated_privacy_healthstats(OFF_DUTY);
    return;
}

//engine_idle is there in the argument just to maintain the legacy code. This feature will be always disabled
//and the engine_idle will always be false.
void activate_button_based_privacy (string cur_session_fname, bool engine_idle)
{
    if (file_is_present(offduty_mode_activated_file)) {
        LOG_I(TAG, "off-duty mode is already enabled, no need to update the privacy");
        return;
    }

    if (ctx.geofence_privacy) {
        LOG_I(TAG, "geofence mode is already enabled, no need to update the privacy");
        return;
    }

    if (ctx.privacy_params.enhanced_privacy) {
        LOG_I(TAG, "Enhanced privacy is already enabled, no need to update the privacy");
        return;
    }

    //To apply button based privacy, we will check, if during the long press duration, all the speed samples were zero.
    // Check speed_samples array for speed and timestamp
    bool all_speed_zero = true;
    int64_t now = get_system_time();
    std::queue<tuple<float, int64_t, float>> speed_samples_copy = ctx.speed_samples;
    while (!speed_samples_copy.empty()) {
        const auto& s = speed_samples_copy.front();
        float speed = std::get<0>(s);
        int64_t timestamp = std::get<1>(s);
        float accuracy = std::get<2>(s);
        LOG_I(TAG, "Speed: %f, timestamp: %lld, accuracy: %f", speed, timestamp, accuracy);
        if (now - timestamp <= ctx.privacy_params.privacy_activate_params.long_press_duration_ms) {
            // This condition is added to avoid invalid speed coming from GPS modules, even if vehicle is stationary
            // For more details on the data point please refer https://netradyne.atlassian.net/browse/DD-3000
            if ((accuracy < invalid_accuracy_threshold) && (speed >= invalid_speed_threshold)) {
                all_speed_zero = false;
                break;
            }
        }
        speed_samples_copy.pop();
    }
    if (!all_speed_zero) {
        LOG_I(TAG, "Speed samples check failed: not all speeds are zero for timestamps within the long press duration");
        return;
    }

    bool old_privacy = ctx.fused_privacy;
    ctx.fused_privacy = fuse_privacy(SOURCE_BUTTON, ctx.fused_privacy, true);
    privacy_reason = REASON_BUTTON_BASED;
    write_privacy_to_file_with_reason(ctx.fused_privacy, privacy_reason, privacy_state_file_global);
    if (ctx.fused_privacy == old_privacy) {
        LOG_I(TAG, "Privacy is already activated, no need to update the privacy");
        return;
    }
    if (cur_session_fname != "") {
        int num_files_recording = get_num_digital_cam_files_recording();
        set_device_mode_for_fname(cur_session_fname, ctx.fused_privacy, engine_idle, num_files_recording, privacy_reason);
        write_privacy_state_file(ctx.fused_privacy);
    }
    if (ctx.inward_privacy_state != (ctx.fused_privacy && ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_BACK])) {
        LOG_I(TAG, "ctx.fused_privacy %d", ctx.fused_privacy);
        set_privacy_mode_led(ctx.fused_privacy && ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_BACK]);
        check_set_irled(REASON_PRIVACY);

        if (ctx.fused_privacy && ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_BACK]) {
            LOG_I(TAG, "Privacy Mode is Activated");
            send_privacy_status_audio_play(REGULAR, true);
        } else {
            LOG_I(TAG, "Privacy Mode is Deactivated");
            send_privacy_status_audio_play(REGULAR, false);
        }
    }
    if (!ctx.privacy_params.enhanced_privacy && !ctx.off_duty_privacy && !ctx.geofence_privacy)
        ctx.inward_privacy_state = ctx.fused_privacy && ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_BACK];
    else
        ctx.inward_privacy_state = true;

    send_privacy_update(DEVICE_CAMERA_POSITION_BACK, old_privacy, ctx.fused_privacy);
    send_privacy_update(DEVICE_CAMERA_POSITION_FRONT, old_privacy, ctx.fused_privacy);

    // Reset the privacy flags to avoid retriggering
    ignition_on_received = false;
    ignition_off_received = false;
    privacy_activate_update_received = false;
    privacy_deactivate_update_received = false;

    send_driver_initiated_privacy_healthstats(REGULAR);

    return;
}

void activate_enhanced_privacy_mode(string cur_session_fname) {
    LOG_I(TAG, "Enhanced Privacy is activated");
    send_privacy_status_audio_play(ENHANCED, true);
    /* In Enhanced privacy only inward will be in privacy always and other
        * cameras will be out of privacy and also alerts detection should be
        * possible. For that we need to make the the device privacy state as FALSE,
        * to tell analytics that device is not in privacy state and it should process
        * the alerts */
    ctx.inward_privacy_state = ctx.privacy_params.enhanced_privacy;
    ctx.fused_privacy = false;
    if (cur_session_fname != "") {
        int num_files_recording = get_num_digital_cam_files_recording();
        set_device_mode_for_fname(cur_session_fname, ctx.off_duty_privacy, ctx.idle_mode_RT_thread, num_files_recording, REASON_OFFDUTY);
        set_privacy_mode_led(ctx.inward_privacy_state);
        check_set_irled(REASON_PRIVACY);
    }
}

#ifdef AUTOMATION
void DriveSimulation::start_dts_imu_msg_loop()
{
    while (1)
    {
        nd_msgq_t::nd_msg_t *msg;
        if ((msg = ctx.Q_DTS_IMU_MSG->receive()) == NULL)
        {
            LOG_C(TAG, "Receive message failed");
            continue;
        }

        generic_msg_t *g_msg = (generic_msg_t *)msg->get_buffer();
        if (NULL == g_msg)
        {
            LOG_E(TAG, "msg->get_buffer() returned NULL");
            continue;
        }
        switch (g_msg->msg_type)
        {
        case START_DTS_MSG:
        {
            LOG_I(TAG, "Received START_DTS_MSG");
            nd_service_obj->send_err_msg(SM_E_DTS_START, 0, "Drive simulation IMU started");
            if (DriveSimulation::isImuQueueEmpty == true)
            {
                DriveSimulation::isImuQueueEmpty = false;
            }
        }
        break;
        default:
            LOG_I(TAG, "Unknown message %d received", g_msg->msg_type);
            break;
        }
    }
}

void DriveSimulation::handle_client(int clientSocket)
{
    nd_server sock;
    string buffer(4000, 0);
    ssize_t recvBytes;

    string receivedData;
    //  sample data coming from automation
    // {"imu_data":[{"accelerometer":["10.14","-0.2","-0.82","1735309850697","1735309850697"],"gyro":["0.4","-0.87","-0.02","1735309850697","1735309850697"]}]}|||
    while (true)
    {
        recvBytes = sock.tcp_receive(clientSocket, &buffer[0], buffer.size(), 0);

        if (recvBytes > 0)
        {
            buffer.resize(recvBytes);
            receivedData += buffer;
            // Check for the delimiter
            size_t delimiterPos = receivedData.find("|||");
            while (delimiterPos != string::npos)
            {
                string dataLine = receivedData.substr(0, delimiterPos);
                receivedData.erase(0, delimiterPos + 3);
                // Parse JSON data for each line
                nlohmann::json json_data = nlohmann::json::parse(dataLine);
                if (json_data.contains("imu_data"))
                {
                    if (json_data["imu_data"].is_null())
                    {
                        LOG_E(TAG, "Received null IMU data");
                        continue;
                    }
                    std::vector<json> imu_data = json_data["imu_data"];
                    for (auto &data : imu_data)
                    {
                        Imu::val_t imu_acc;
                        Imu::val_t imu_gyro;
                        Imu::val_t imu_magneto;
                        imu_gyro.x = std::stof(data["gyro"][0].get<std::string>());
                        imu_gyro.y = std::stof(data["gyro"][1].get<std::string>());
                        imu_gyro.z = std::stof(data["gyro"][2].get<std::string>());
                        imu_gyro.clock_time = 0; // while pop setting this to system_time
                        imu_gyro.raw_time = 0;

                        imu_acc.x = std::stof(data["accelerometer"][0].get<std::string>());
                        imu_acc.y = std::stof(data["accelerometer"][1].get<std::string>());
                        imu_acc.z = std::stof(data["accelerometer"][2].get<std::string>());
                        imu_acc.raw_time = 0;
                        imu_acc.clock_time = 0;

                        imu_magneto.x = 0;
                        imu_magneto.y = 0;
                        imu_magneto.z = 0;
                        imu_magneto.raw_time = 0;
                        imu_magneto.clock_time = 0;
                        DriveSimulation::imu_data_queue.push_back(std::make_tuple(imu_acc, imu_gyro, imu_magneto));
                    }
                }

                delimiterPos = receivedData.find("|||");
            }
        }
        else
        {
            break;
        }
    }
}

int DriveSimulation::receiveData_IMU()
{
    nd_server sock;
    int Port = 12349;

    int Socket = sock.createTcpSocket();
    if (Socket < 0)
    {
        LOG_E(TAG, "Failed to create TCP socket");
        nd_service_obj->send_err_msg(SM_E_DTS_CONN_ERROR, 0, "DTS socket creation error");
        return 1;
    }
    if (!sock.bindAndListenTcpSocket(Socket, Port))
    {
        close(Socket);
        nd_service_obj->send_err_msg(SM_E_DTS_CONN_ERROR, 0, "DTS bind/listen error");
        return 1;
    }
    LOG_I(TAG, "TCP server to receive IMU is now listening for incoming connections on port %d", Port);

    // Accept and handle IMU clients
    while (true)
    {

        int ClientSocket = sock.acceptTcpConnection(Socket);
        if (ClientSocket >= 0)
        {
            DriveSimulation::imu_data_queue.clear();
            std::thread ClientThread(DriveSimulation::handle_client, ClientSocket);
            ClientThread.detach();
        }
        else{
            nd_service_obj->send_err_msg(SM_E_DTS_CONN_ERROR, 0, "DTS connection error");
        }
    }
}
#endif

void* qr_scan_irled_blink_thread(void* args)
{
    while (qr_scan_started) {
        // Sleep for 5 seconds total, checking qr_scan_started every 100ms
        for (int i = 0; i < 50 && qr_scan_started; i++) {
            usleep(100000); // 100ms
        }

        // Check if QR scan is still running
        if (!qr_scan_started) {
            break;
        }

        // Check photodiode sensor status
        light_mode pt_reading = DAY_MODE;
        bool photodiode_status = ctx.photodiode->get_phototransistor_status(&pt_reading);

        if (!photodiode_status) {
            LOG_E(TAG, "Failed to get photodiode status during QR scan");
            continue;
        }

        LOG_I(TAG, "QR scan photodiode reading: %s", (pt_reading == NIGHT_MODE) ? "NIGHT_MODE" : "DAY_MODE");

        // Only toggle the IR LED if photodiode indicates NIGHT_MODE
        if (pt_reading == NIGHT_MODE) {
            qr_scan_irled_toggle_state = !qr_scan_irled_toggle_state;
            LOG_I(TAG, "QR scan IR LED toggle: %s", qr_scan_irled_toggle_state ? "ON" : "OFF");
            check_set_irled(REASON_QR_SCAN, qr_scan_irled_toggle_state);
        } else {
            // Ensure IR LED is OFF when not in NIGHT_MODE
            if (qr_scan_irled_toggle_state) {
                qr_scan_irled_toggle_state = false;
                LOG_I(TAG, "QR scan IR LED turned OFF - not in NIGHT_MODE");
                check_set_irled(REASON_QR_SCAN, false);
            } else {
                LOG_I(TAG, "QR scan IR LED already OFF - not in NIGHT_MODE");
            }
        }
    }

    LOG_I(TAG, "QR scan IR LED blink thread exiting");
    qr_scan_irled_thread_running = false;
    return NULL;
}

void msg_loop()
{
#ifdef PLAY_BEEP
    int msg_counter = 0;
#endif

    int streaming_outward = 0;
    int streaming_inward = 0;
    nd_msgq_t::nd_msg_t *msg;

    ndc_start_meta_msg_t *meta_msg;
    ndc_stop_meta_msg_t *meta_stop_msg;
    int postpone_count = 0;
    ndc_cam_restart_msg_t *restart_cam_msg;

    ndc_cam_msg_t *cam_start_msg;
    ndc_cam_msg_t *cam_stop_msg;
    ndc_photodiode_cb_msg_t *photodiode_cb_msg;
    drv_id_update_msg_t *drvid_update_msg;
    eng_idle_update_msg_t *eng_idle_update_msg;
    privacy_mode_update_msg_t *privacy_mode_update_msg;
    driver_login_fr_update_msg_t *driver_login_update_msg;
    user_alert_msg_t *user_alert_msg;
    highg_alert_msg_t *highg_alert_msg;
    conn_mgr_sig_info_msg_t *sig_info_msg;
    network_time_update_msg_t *network_time_msg;
    res_idle_reg_msg_t *res_idle_msg;
    res_idle_update_msg_t *res_idle_update_msg;
    res_speed_reg_msg_t *res_speed_msg;

    obd_wakeup_msg_t *obd_wakeup_msg;
    obd_subscribe_msg_t obd_subscribe_msg;
    ndmb_generic_msg_t ndmb_msg;
    //Obd::obd_data_t obd_val;

    resp_drv_login_query *resp_drvlogin;

    ndc_get_circular_buffer_qname_msg_t *circular_buffer_qname;
    std::string s_qname;

    string cur_session_fname = "";

    constexpr int64_t AUDIO_PLAY_THRESHOLD = 60;

    speed_privacy_activate_target_time = get_system_monotonic_time() + ctx.privacy_params.privacy_activate_params.threshold_time * 1000;
    speed_privacy_deactivate_target_time = get_system_monotonic_time() + ctx.privacy_params.privacy_deactivate_params.threshold_time * 1000;

    bool temp_initial_privacy = false;
    privacy_reason_t temp_privacy_reason = REASON_NO_PRIVACY;
    // reading default privacy
    temp_initial_privacy = get_default_privacy_speed();
    if (temp_initial_privacy == PRIVACY_ON) {
        temp_privacy_reason = REASON_SPEED;
    }
    // updating initial privacy based on ignition state
    if (ctx.privacy_params.privacy_activate_params.ignition_based_privacy && ctx.crank_level_RT_thread == CRANK_LOW) {
        temp_initial_privacy = true;
        temp_privacy_reason = REASON_IGNITION;
    } else if (ctx.privacy_params.privacy_deactivate_params.ignition_based_privacy && ctx.crank_level_RT_thread == CRANK_HIGH) {
        temp_initial_privacy = false;
        temp_privacy_reason = REASON_NO_PRIVACY;
    }

    int64_t prev_privacy_time = 0;
    bool temp_privacy;
    if (first_after_boot == true) {
        // Check and handle geofence privacy state on boot using database
        bool geofence_status_from_db = false;
        if (nd_service_obj->get_geofence_status(&geofence_status_from_db)) {
            if (geofence_status_from_db) {
                LOG_I(TAG, "Geofence privacy mode was active before reboot (from DB)");
                ctx.geofence_privacy = true;
                activate_geofence_privacy(currvid_fname, /*engine idle */ false);
                LOG_I(TAG, "Re-activated geofence privacy after boot");
                bool ret = read_privacy_value_and_reason_from_file(temp_privacy, privacy_reason, privacy_state_file_global);
            } else {
                LOG_I(TAG, "Geofence was OFF before reboot (from DB)");
                ctx.geofence_privacy = false;
            }
        } else {
            LOG_W(TAG, "Failed to read geofence status from DB, defaulting to OFF");
            ctx.geofence_privacy = false;
        }
        
        if (file_is_present(offduty_mode_activated_file)) {
            bool reason_sw_reboot = false;
            if (pow_on_off_reason & (1 << PowerOnTriggerT::REBOOT)) {
                // Device rebooted due to software reason
                reason_sw_reboot = true;
            }
            LOG_I(TAG, "pow_on_off_reason = 0x%X, reason_sw_reboot = %d", pow_on_off_reason, reason_sw_reboot);
            if (reason_sw_reboot && (nd_device_obj->get_crank_level() == CRANK_HIGH)) {
                // Retaining Off-duty mode when device rebooted due to any SW reason and updating fused privacy accordingly
                LOG_I(TAG, "Off duty driving mode is activated");
                // Delete the existing off-duty file first, then reactivate off-duty mode to ensure all context variables are properly updated.
                // Without this, the activate function would return early seeing the file already exists, leaving variables uninitialized.
                file_delete(offduty_mode_activated_file);
                activate_off_duty_privacy(currvid_fname, false, false);
                bool ret = read_privacy_value_and_reason_from_file(temp_privacy, privacy_reason, privacy_state_file_global);
            } else {
                LOG_I(TAG, "Off duty driving mode is deactivated");
                send_privacy_status_audio_play(OFF_DUTY, false);
                file_delete(offduty_mode_activated_file);
                ctx.fused_privacy = temp_initial_privacy;
                privacy_reason = temp_privacy_reason;
                write_privacy_to_file_with_reason(ctx.fused_privacy, privacy_reason, privacy_state_file_global);
            }
        } else {
            ctx.fused_privacy = temp_initial_privacy;
            privacy_reason = temp_privacy_reason;
            write_privacy_to_file_with_reason(ctx.fused_privacy, privacy_reason, privacy_state_file_global);
        }
    } else if (first_after_boot == false) {
        // if NDC crash -> go to previous privacy state by reading dev/shm/privacy_state.bin and update fused privacy
        if (read_privacy_state_file(ctx.fused_privacy, prev_privacy_time) == true) {
            LOG_I(TAG, "prev_privacy_time = %lld", prev_privacy_time);
            if (ctx.privacy_params.privacy_activate_params.speed_based_privacy)
                speed_privacy_activate_target_time = prev_privacy_time + ctx.privacy_params.privacy_activate_params.threshold_time * 1000;
            if (ctx.privacy_params.privacy_deactivate_params.speed_based_privacy)
                speed_privacy_deactivate_target_time = prev_privacy_time + ctx.privacy_params.privacy_deactivate_params.threshold_time * 1000;
            // updating initial privacy reason in case of NDC crash
            bool ret = read_privacy_value_and_reason_from_file(temp_privacy, privacy_reason, privacy_state_file_global);
        }
        LOG_I(TAG, "speed_privacy_activate_target_time:%lld, speed_privacy_deactivate_target_time:%lld", speed_privacy_activate_target_time, speed_privacy_deactivate_target_time);

        // Check geofence status from database on service restart
        bool geofence_status_restart = false;
        if (nd_service_obj->get_geofence_status(&geofence_status_restart)) {
            if (geofence_status_restart) {
                LOG_I(TAG, "Bagheera service restarted while in geofence mode (from DB), lets apply geofence mode");
                activate_geofence_privacy(currvid_fname, false);
                bool ret = read_privacy_value_and_reason_from_file(temp_privacy, privacy_reason, privacy_state_file_global);
            } else {
                LOG_I(TAG, "Geofence was OFF during service restart (from DB)");
            }
        } else {
            LOG_W(TAG, "Failed to read geofence status from DB on restart, defaulting to OFF");
        }

        if (file_is_present(offduty_mode_activated_file)) {
            LOG_I(TAG, "Bagheera service restarted while in off-duty mode, lets apply off-duty mode");
            // Delete the existing off-duty file first, then reactivate off-duty mode to ensure all context variables are properly updated.
            // Without this, the activate function would return early seeing the file already exists, leaving variables uninitialized.
            file_delete(offduty_mode_activated_file);
            activate_off_duty_privacy(currvid_fname, false, false);
            bool ret = read_privacy_value_and_reason_from_file(temp_privacy, privacy_reason, privacy_state_file_global);
        }
    }

    LOG_I(TAG, "Initial privacy: %d", ctx.fused_privacy);
    write_privacy_state_file(ctx.fused_privacy);

    int prev_ignition_state;
    int64_t prev_ignition_time;
    bool ret = read_ignition_state_file(prev_ignition_state, prev_ignition_time);
    /* Below condition is added for, suppose if there is a change in Ignition after the last privacy transition
     * and there was duration added in config file as post_ignition_duration and before that duration
     * bagheera service crashed for any reason, so once the bagheera service restarts, it should apply the last
     * privacy state and after post_ignition duration, it should apply the ignition privacy also */
    if (prev_privacy_time < prev_ignition_time) {
        if (ret == true) {
            if (prev_ignition_state == 0 && ctx.privacy_params.privacy_activate_params.ignition_based_privacy) { //IGN_OFF
                ignition_off_received = true;
                ignition_on_received = false;
                post_ignition_off_target_time = prev_ignition_time + (ctx.privacy_params.privacy_activate_params.post_ignition_off_duration * 1000);
            } else if (prev_ignition_state && ctx.privacy_params.privacy_deactivate_params.ignition_based_privacy) { //IGN_ON
                ignition_off_received = false;
                ignition_on_received = true;
                post_ignition_on_target_time = prev_ignition_time + (ctx.privacy_params.privacy_deactivate_params.post_ignition_on_duration * 1000);
            }
        }
    }
    ctx.inward_privacy_state = ctx.fused_privacy && ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_BACK];

    if (!file_is_present(offduty_mode_activated_file) && !ctx.geofence_privacy && ctx.privacy_params.enhanced_privacy) {
        activate_enhanced_privacy_mode(currvid_fname);
    }

    light_mode pt_reading = DAY_MODE;
    if (ctx.photodiode->get_phototransistor_status(&pt_reading) == true) {
        photodiode_callback(pt_reading);
    }

    bool engine_idle = false;
    ctx.idle_mode_RT_thread = engine_idle;
    bool res;
    int res_fname_reg =-1;
    std::string s = "NDNB_OBD_SERVICE";
    NDMBClient msg_client(s);
    bool obd_res = check_obd_service_configuration();
    if (obd_res) {
        msg_client.subscribe(TOPIC_OBD_DATA, ndmb_obddata_cb, obd_data_retry_count, obd_data_retry_time);
    }
    std::string s1 = "NDMB_OBD_FR_SERVICE";
    NDMBClient msg_client_fr(s1);
    obd_res = check_fuel_report_configuration();
    if (obd_res){
       LOG_I(TAG, "subscribe for fuel report failed");
       msg_client_fr.subscribe(TOPIC_FUEL_REPORT_DATA, ndmb_fuelreport_cb, obd_data_retry_count, obd_data_retry_time);
    }

    std::string s2 = "NDMB_OBD_IR_SERVICE";
    NDMBClient msg_client_ir(s2);
    obd_res = check_idling_report_configuration();
    if (obd_res){
        LOG_I(TAG, "subscribe for idling report failed");
       msg_client_ir.subscribe(TOPIC_IDLING_REPORT_DATA, ndmb_idlingreport_cb, obd_data_retry_count, obd_data_retry_time);
    }
    std::string s3 = "NDMB_OBD_ENG_SERVICE";
    NDMBClient msg_client_es(s3);
    LOG_I(TAG, "subscribe for engine status");
    msg_client_es.subscribe(TOPIC_ENGINE_STATUS, ndmb_engine_status_cb, obd_data_retry_count, obd_data_retry_time);

    std::string s4 = "NDMB_GPS_SERVICE";
    NDMBClient msg_client_gps(s4);
    LOG_I(TAG, "subscribe for GPS data");
    msg_client_gps.subscribe(TOPIC_GPS_DATA, ndmb_gps_cb, default_gps_data_retry_count, default_gps_data_retry_time);

    std::string s5 = "NDMB_GPS_PPS_SERVICE";
    NDMBClient msg_client_gps_pps(s5);
    LOG_I(TAG, "subscribe for PPS data");
    msg_client_gps_pps.subscribe(TOPIC_GPS_PPS_DATA, ndmb_gps_pps_cb, default_gps_data_retry_count, default_gps_data_retry_time);


    while(1) {
        if( (msg = ctx.msg_q->receive( )) == NULL ) {
            LOG_C(TAG, "Receive message failed");
            continue;
        }

        ndc_generic_msg_t *g_msg = (ndc_generic_msg_t *)msg->get_buffer();
        if( NULL == g_msg ) {
            LOG_E(TAG, "msg->get_buffer() returned NULL");
            continue;
        }
        switch( g_msg->type ) {

            case REQ_CIRCULAR_BUFFER_Q_NAME:
                circular_buffer_qname = (ndc_get_circular_buffer_qname_msg_t *)g_msg;
                LOG_I(TAG, "REQ_CIRCULAR_BUFFER_Q_NAME received");
                if( ctx.circular_buffer_msg_q == NULL ) {
                    if(circular_buffer_qname->length != sizeof(ndc_get_circular_buffer_qname_msg_t))
                    {
                        LOG_E(TAG, "circular_buffer_qname->len != sizeof(ndc_get_circular_buffer_qname_msg_t)");
                        break;
                    }
                    LOG_I(TAG, "circular_buffer_qname->len %d",circular_buffer_qname->length);
                    s_qname.assign(circular_buffer_qname->q_name);
                    LOG_I(TAG, "String S %s", s_qname.c_str());
                    ctx.circular_buffer_msg_q = nd_msgq_t::get_msgq( s_qname, nd_msgq_t::ND_MSGQ_CLIENT);
                    if( ctx.circular_buffer_msg_q == NULL ) {
                        LOG_E(TAG, "Failed to create ctx.circular_buffer_msg_q");
                        break;
                    }
                    LOG_I(TAG, "successfully created ctx.circular_buffer_msg_q");
                }
                else {
                    LOG_I(TAG, "ctx.circular_buffer_msg_q != NULL");
                }

                break;

            case START_CAMERA:
                {
                    if (cams_enabled[DEVICE_CAMERA_POSITION_BACK]) {
                        turn_irled_on_or_off (ctx.saved_gps.timestamp/1000, ctx.saved_gps.longitude);
                    }

                    cam_start_msg = (ndc_cam_msg_t *)g_msg;
                    LOG_I(TAG, "START_CAMERA received");

                    if(cam_start_msg->len != sizeof(ndc_cam_msg_t)) {
                        LOG_E(TAG, "cam_start_msg->len != sizeof(ndc_cam_msg_t)");
                        break;
                    }

                    LOG_I(TAG, "cam_start_msg->cam_num %x", cam_start_msg->cam_num);
                    //For debugging camera crash
                    //execute_cmd (dump_meminfo_cmd, "MEMINFO_BEFORE_CAM_START");

#ifdef BAGHEERA2
                    if( start_camera(0) == false ) {
                        LOG_C(TAG,"Cannot start outward camera");
                    }
#elif KRAIT
                    for (int i = 0; i < NUM_CAMERAS; i++) {
                        if (cams_enabled[i]) {
                            if (start_camera(i) == false) {
                                LOG_C(TAG,"Cannot start camera");
                            }
                        }
                    }
#endif
                    if((audio_enable == true) && (audio_running == false)) {
                        LOG_I(TAG, "start_audio");
                        if(ctx.audio->start_audio()) {
                            audio_running = true;
                        }
                    }
                    //For debugging camera crash
                    //execute_cmd (dump_meminfo_cmd, "MEMINFO_AFTER_CAM_START");
                    //execute_cmd (dump_camera_daemon_logs_cmd.c_str(), "DAEMON_LOGS");
#ifndef KRAIT
                    string mirror_status = get_outcam_mirror_status();
                    LOG_I(TAG, "Outcam mirror status: %s", mirror_status.c_str());
                    // If mirror is disabled, report it as critical info
                    if(mirror_status != OUTCAM_MIRROR_ENABLED) {
                        LOG_E(TAG, "Outcam mirror status is not 4. Reporting to service mon");
                        nd_service_obj->send_err_msg(SM_E_NDC_OUTCAM_FLIP, 0, "Camera mirror disabled");
                    }
#endif
                }
                break;

            case RESTART_CAMERA: // triggered by timer
                {
                    restart_cam_msg = (ndc_cam_restart_msg_t *)g_msg;
                    LOG_I(TAG, "###RESTART_CAMERA###");

                    if (cams_enabled[DEVICE_CAMERA_POSITION_BACK]) {
                        turn_irled_on_or_off (ctx.saved_gps.timestamp/1000, ctx.saved_gps.longitude);
                    }
#ifdef ROUTE_LOGS
                    startmeta_count++; // move to timer based event instead of this logic
                    if(startmeta_count%(LOG_FILE_DURATION) == 0) {
                           route_logs( log_dir.c_str() );
                    }
#endif
                    int num_files_recording = get_num_digital_cam_files_recording();
                    cur_session_fname = currvid_fname;
                    if(cur_session_fname != "") {
                        LOG_I(TAG, "ctx.fused_privacy %d", ctx.fused_privacy);

                        bool ret = read_privacy_value_and_reason_from_file(temp_privacy, privacy_reason, privacy_state_file_global);
                        set_device_mode_for_fname(cur_session_fname, temp_privacy, engine_idle, num_files_recording, privacy_reason);
                        set_device_mode_for_fname(cur_session_fname, ctx.off_duty_privacy, engine_idle, num_files_recording, REASON_OFFDUTY);
                        set_device_mode_for_fname(cur_session_fname, ctx.geofence_privacy, engine_idle, num_files_recording, REASON_GEOFENCE);
                        set_privacy_mode_led(ctx.fused_privacy && ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_BACK]);

                        pthread_mutex_lock(&irled_mutex);
                        set_irled_mode_for_fname(currvid_fname, ctx.ir_led_status);
                        pthread_mutex_unlock(&irled_mutex);
                    }
                    //Send a keep alive
                    svc_util_send_keepalive();

                    // Monitor Audio Play Requests
                    monitor_audio_requests(AUDIO_PLAY_THRESHOLD);

                }
                break;

            case STOP_CAMERA:
                cam_stop_msg = (ndc_cam_msg_t *)g_msg;
                LOG_I(TAG, "STOP_CAMERA received");

                if( cam_stop_msg->len != sizeof(ndc_cam_msg_t) ) {
                    LOG_E(TAG, "cam_stop_msg->len != sizeof(ndc_cam_msg_t)");
                    break;
                }

                if( cams_enabled[cam_stop_msg->cam_num] && stop_camera(cam_stop_msg->cam_num) == false ) {
                    LOG_C(TAG,"Cannot stop Camera");
                }
                break;

            case START_META:
                {
                    meta_msg = (ndc_start_meta_msg_t *)g_msg;
                    LOG_I(TAG, "START_META received for cam_num %d, prevVidName:%s, curVidName:%s",
                               meta_msg->cam_pos, ctx.fname[meta_msg->cam_pos].c_str(), meta_msg->f_name);

                    if (meta_msg->len != sizeof(ndc_start_meta_msg_t)) {
                        LOG_E(TAG, "meta_msg->len != sizeof(ndc_start_meta_msg_t)");
                        break;
                    }
                    if (meta_msg->cam_pos == DEVICE_CAMERA_POSITION_FRONT) {
                        ctx.prevVideoName = ctx.fname[meta_msg->cam_pos];
                        if (ctx.prevVideoName.find("_trip") == string::npos) {
                            ctx.prevVideoName = meta_msg->f_name;
                            LOG_I(TAG, "Current videoName and ctx.prevVideoName %s", ctx.prevVideoName.c_str());

                        }
                    }
                    pthread_mutex_lock(&file_mutex);
                    ctx.fname[meta_msg->cam_pos] = meta_msg->f_name;
                    pthread_mutex_unlock(&file_mutex);
                    pthread_mutex_lock(&file_start_time_vec_mutex);
                    int i = 0;
                    for(i = 0; i < file_start_time_vec_pair.size(); i++)
                    {
                        if(file_start_time_vec_pair[i].first == ctx.fname[meta_msg->cam_pos] ) {
                            file_start_time_vec_pair.erase(file_start_time_vec_pair.begin() + i);
                            break;
                        }
                    }
                    pthread_mutex_unlock(&file_start_time_vec_mutex);

                    // If ext camera feature is enabled and cam 0 start meta is received,
                    // create file names and start times for external camera
                    // recording and set globals accordingly
                    if (meta_msg->cam_pos == DEVICE_CAMERA_POSITION_FRONT) {
                        ctx.presVideoName = ctx.fname[meta_msg->cam_pos];
                        //for front camera, meta_msg->epoch_time is in micros
                        pthread_mutex_lock(&file_start_time_vec_mutex);
                        file_start_time_vec_pair.push_back(make_pair(ctx.fname[meta_msg->cam_pos], meta_msg->epoch_time / ONE_MILLI_IN_MICRO));
                        pthread_mutex_unlock(&file_start_time_vec_mutex);

                        // adding session starttime needed for healthstats
                        ctx.cam_session_start_time[meta_msg->cam_pos][ctx.session_flipflop] = meta_msg->epoch_time / ONE_MILLI_IN_MICRO;

                        start_meta( meta_msg->f_name, meta_msg->epoch_time, meta_msg->raw_time);
                        if (ctx.ext_cam_feature_enabled) {
                            check_and_set_ext_cam_fnames(meta_msg->epoch_time/ONE_MILLI_IN_MICRO);
                        }
#ifdef PLAY_BEEP
                        if (msg_counter == 0)
                        {
                            LOG_I (TAG, "Beeping at the start of session");
                            play_beep(msg_counter++);
                        }
#endif
                    } else {
                        //for other cameras, meta_msg->epoch_time is in millis
                        pthread_mutex_lock(&file_start_time_vec_mutex);
                        file_start_time_vec_pair.push_back(make_pair(ctx.fname[meta_msg->cam_pos], meta_msg->epoch_time));
                        pthread_mutex_unlock(&file_start_time_vec_mutex);
                        // adding session starttime needed for healthstats
                        ctx.cam_session_start_time[meta_msg->cam_pos][ctx.session_flipflop] = meta_msg->epoch_time;
                    }
                }
                break;

            case STOP_DUMP_META:
                {
                    meta_stop_msg = (ndc_stop_meta_msg_t *)g_msg;
                    if (meta_stop_msg->len != sizeof(ndc_stop_meta_msg_t)) {
                        LOG_E(TAG, "meta_stop_msg->len != sizeof(ndc_stop_meta_msg_t)");
                        break;
                    }
                    LOG_I(TAG, "STOP_DUMP_META received for cam_num %d", meta_stop_msg->cam_num);
                    // adding session endtime needed for healthstats
                    if (meta_stop_msg->cam_num == DEVICE_CAMERA_POSITION_FRONT) {
                        //For front camera, meta_stop_msg->epoch_time is in micros
                        ctx.cam_session_stop_time[meta_stop_msg->cam_num][meta_stop_msg->flipflop] = meta_stop_msg->epoch_time / ONE_MILLI_IN_MICRO;
                    } else {
                        //For other cameras, meta_stop_msg->epoch_time is in millis
                        ctx.cam_session_stop_time[meta_stop_msg->cam_num][meta_stop_msg->flipflop] = meta_stop_msg->epoch_time;
                    }

		    bool need_to_copy = (user_alert_copy[DEVICE_CAMERA_POSITION_FRONT] && ctx.privacy_params.save_user_alert_video);

                    stop_meta( meta_stop_msg->f_name, meta_stop_msg->cam_num, meta_stop_msg->flipflop, meta_stop_msg->epoch_time, meta_stop_msg->raw_time, meta_stop_msg->pts_time, false);

                    // flip the buffer to append in next session
                    if (meta_stop_msg->cam_num == DEVICE_CAMERA_POSITION_FRONT) {
                        // Check if external cameras are enabled and send file record messages
                        if ((ctx.ext_cam_feature_enabled == true) && (!file_is_present(lpw_no_record_persistent_file))) {
                            send_msg_ext_cam_thread(meta_stop_msg->epoch_time/ONE_MILLI_IN_MICRO, need_to_copy);
                        }
                        ctx.session_number++;
                    }

#ifdef PLAY_BEEP
                    if (meta_stop_msg->cam_num == 0 && msg_counter == 1)
                    {
                        LOG_I (TAG, "Beeping at the end of session");
                        play_beep(msg_counter++);
                    }
#endif
                }
                break;

            case PHOTODIODE_CALL_BACK:
                photodiode_cb_msg = (ndc_photodiode_cb_msg_t*)g_msg;
                LOG_I (TAG,"Photodiode call back msg received PT status:%d, ctx.inward_privacy_state = %d",
                            photodiode_cb_msg->pt_status, ctx.inward_privacy_state);

                if(cams_enabled[DEVICE_CAMERA_POSITION_BACK] == true) {
                    if(photodiode_cb_msg->pt_status == DAY_MODE) {
                        check_set_irled(REASON_AMBIENT_LIGHT, false);
                    }
                    else if(photodiode_cb_msg->pt_status == NIGHT_MODE) {
                        check_set_irled(REASON_AMBIENT_LIGHT, true);
                    }
                }
                break;

            case USER_ALERT: {
                user_alert_msg = (user_alert_msg_t *)g_msg;
#ifdef KRAIT
                int64_t time = (int64_t)extract_time_64(user_alert_msg->timestamp1, user_alert_msg->timestamp2);
#else
                int64_t time = user_alert_msg->timestamp;
#endif
                LOG_I (TAG, "User alert msg received, button: %d src: %s @ %lld", user_alert_msg->button,
                       user_alert_msg->source, time);
                
                // Block user alerts when geofence privacy is enabled
                if (ctx.geofence_privacy) {
                    LOG_C(TAG, "CRITICAL: User alert button %d pressed during ACTIVE geofence privacy - IGNORED (no LED/audio/copy). "
                          "geofence_privacy=%d, off_duty_privacy=%d, enhanced_privacy=%d",
                          user_alert_msg->button, ctx.geofence_privacy, ctx.off_duty_privacy, 
                          ctx.privacy_params.enhanced_privacy);
		    break;
                }
                
                handle_user_alert(time, user_alert_msg->button, user_alert_msg->source);
                for (int i = 0; i < DEVICE_CAMERA_POSITION_MAX; i++)
                {
                    if (cams_enabled[i]) {
                        user_alert_copy[i] = true;
                    }
                    else {
                        user_alert_copy[i] = false;
                    }
                }
                if (cams_enabled[DEVICE_CAMERA_POSITION_DMS])
                    user_alert_copy[DEVICE_CAMERA_POSITION_DMS] = true;
                else
                    user_alert_copy[DEVICE_CAMERA_POSITION_DMS] = false;
                break;
            }

            case START_LIVE_STREAMING:
                {
                    req_livestreaming_data_t *kinesis_req_msg;
                    kinesis_req_msg = (req_livestreaming_data_t *)g_msg;
                    kinesis_req_msg->dual_streaming = false;
                    if (file_is_present(lpw_no_record_persistent_file)) {
                        LOG_I(TAG, "LPW no record is enabled, not streaming");
                        send_livestreaming_completion_msg( kinesis_req_msg, LIVE_STREAMING_ERR, LS_ERR_LPW_ENABLED);
                        break;
                    }
                    if (live_streaming_enabled) {
                        if (kinesis_req_msg->camera == DEVICE_CAMERA_POSITION_BACK) {
                            check_start_inward_livestreaming(kinesis_req_msg, streaming_inward);
                        }
                        else if (kinesis_req_msg->camera == DEVICE_CAMERA_POSITION_FRONT) {
                            check_start_outward_livestreaming(kinesis_req_msg, streaming_outward);
                        }
                    } else {
                        LOG_E(TAG, "Live streaming feature disabled. Not streaming");
                        send_livestreaming_completion_msg( kinesis_req_msg, LIVE_STREAMING_ERR, LS_ERR_FEATURE_DISABLED);
                    }
                    break;
                }
            case START_DUAL_LIVE_STREAMING:
                {
                    req_dual_livestreaming_data_t *kinesis_req_msg;
                    kinesis_req_msg = (req_dual_livestreaming_data_t *)g_msg;
                    req_livestreaming_data_t outward, inward;
                    convert_dual_to_two_single_streaming_requests(kinesis_req_msg, &outward, &inward);
                    if (file_is_present(lpw_no_record_persistent_file)) {
                        LOG_I(TAG, "LPW no record is enabled, not streaming");
                        send_livestreaming_completion_msg( &outward, LIVE_STREAMING_ERR, LS_ERR_LPW_ENABLED, LS_ERR_LPW_ENABLED);
                        break;
                    }
                    if (live_streaming_enabled) {
                        //if streaming is for both, and both cameras are disabled break; else continue
                        if (cams_enabled[DEVICE_CAMERA_POSITION_FRONT] == false && cams_enabled[DEVICE_CAMERA_POSITION_BACK] == false) {
                            LOG_E(TAG, "Both cameras are disabled. Not streaming");
                            // Sending outward camera completion message for dual as it just uses request id field to update shadow
                            send_livestreaming_completion_msg( &outward, LIVE_STREAMING_ERR, LS_ERR_CAM_DISABLED, LS_ERR_CAM_DISABLED);
                            break;
                        }
                        //If privacy is enabled for both cameras, break
                        if (((ctx.fused_privacy && ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_BACK]) != PRIVACY_OFF)
                             && ((ctx.fused_privacy && ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_FRONT]) != PRIVACY_OFF)) {
                            LOG_E(TAG, "Both cameras are in privacy mode. Not streaming");
                            send_livestreaming_completion_msg( &outward, LIVE_STREAMING_ERR, LS_ERR_PRIVACY_ENABLED, LS_ERR_PRIVACY_ENABLED);
                            break;
                        }
                        //Start both kinesis streams after checking for camera enabled & privacy
                        check_start_outward_livestreaming(&outward, streaming_outward);
                        inward.dual_streaming_active = streaming_outward;
                        check_start_inward_livestreaming(&inward, streaming_inward);
                    } else {
                        LOG_E(TAG, "Live streaming feature disabled. Not streaming");
                        send_livestreaming_completion_msg( &outward, LIVE_STREAMING_ERR, LS_ERR_FEATURE_DISABLED, LS_ERR_FEATURE_DISABLED);
                    }
                    break;
                }
            case STOP_LIVE_STREAMING:
            {
                req_livestreaming_data_t *kinesis_msg;
                kinesis_msg = (req_livestreaming_data_t *)g_msg;
                // log streaming boolean state
                LOG_I(TAG, "stop kinesis message received, streaming_outward= %d, streaming_inward= %d", streaming_outward, streaming_inward);
                if (streaming_outward || streaming_inward) {
                    livestreaming_status_t status;
                    status = LIVE_STREAMING_DONE;
                    LOG_I(TAG, "stop kinesis message received for camera %d", kinesis_msg->camera);
                    send_livestreaming_completion_msg( kinesis_msg, status);

                    if (!stop_kinesis(kinesis_msg)) {
                        LOG_E (TAG, "stop kinesis failed for camera %d", kinesis_msg->camera);
                        status = LIVE_STREAMING_DONE;
                    }
                    if(kinesis_msg->camera == DEVICE_CAMERA_POSITION_FRONT){
                        streaming_outward = 0;
                    }
                    else if(kinesis_msg->camera == DEVICE_CAMERA_POSITION_BACK){
                        streaming_inward = 0;
                    }
                } else {
                    LOG_I(TAG,"Not a valid call since no live streaming is started");
                }
                break;
            }

            case INSTALLER_SCAN_INDICATE:
            {
                LOG_I(TAG,"INSTALLER_SCAN_INDICATE msg received.");
                led_blink_req_msg_t *blink_msg = (led_blink_req_msg_t*)g_msg;
                pthread_mutex_lock(&inst_scan_led_mutex);

                bool is_installer_ramfile_present = file_is_present(installer_scan_active_file);
                bool explicit_stop = (blink_msg->blink_status == LED_STOP_BLINKING);
                bool need_start = !explicit_stop && ((blink_msg->blink_status == LED_START_BLINKING) || is_installer_ramfile_present);
                bool need_stop = explicit_stop || !is_installer_ramfile_present;

                if (need_stop && ctx.is_led_blinking && inst_scan_led_blinking)
                {
                    LOG_I(TAG,"INSTALLER_SCAN_INDICATE: Requesting installer scan blink thread to stop.");
                    inst_scan_cancel = true;
                }
                else if (need_start)
                {
                    uint16_t timeout_sec = get_installer_scan_blink_timeout_sec(blink_msg->blink_timeout_sec);
                    if (ctx.is_led_blinking && inst_scan_led_blinking) {
                        reset_installer_scan_blink_deadline(timeout_sec);
                        LOG_I(TAG, "INSTALLER_SCAN_INDICATE: Blink already active, timeout reset to %u seconds.", (unsigned)timeout_sec);
                    } else {
                        start_installer_scan_led_blinking(timeout_sec);
                    }
                }
                else
                {
                    LOG_I(TAG, "INSTALLER_SCAN_INDICATE: No action. blink_status: %d, is_led_blinking: %d, installer_scan_ramfile_present: %d",
                          (int)blink_msg->blink_status, (int)ctx.is_led_blinking, (int)is_installer_ramfile_present);
                }
                pthread_mutex_unlock(&inst_scan_led_mutex);

                break;
            }

            case HIGHG_ALERT:
                highg_alert_msg = (highg_alert_msg_t *)g_msg;
                LOG_I (TAG,"High G alert msg received, ");
                handle_highg_alert( highg_alert_msg->timestamp);
                for (int i = 0; i < DEVICE_CAMERA_POSITION_MAX; i++)
                {
                    if (cams_enabled [i])
                    {
                        highg_alert_copy [i] = true;
                    }
                    else
                    {
                        highg_alert_copy [i] = false;
                    }
                }
                break;

            case QUIT:
                LOG_I(TAG, "Quiting the message loop");
                delete msg;
                return;

            case DRVID_UPDATE:
                LOG_I (TAG,"DRVID_UPDATE received");
                drvid_update_msg = (drv_id_update_msg_t*)g_msg;
                set_driver_id (drvid_update_msg->drv_ids, drvid_update_msg->login_time, drvid_update_msg->num_drvids);
                break;

            case ENG_IDLE_UPDATE:
                eng_idle_update_msg = (eng_idle_update_msg_t*)g_msg;
                engine_idle = eng_idle_update_msg->idle_on;
                ctx.idle_mode_RT_thread = engine_idle;
                // 1 means car is idle
                // 0 means car is moving
                LOG_I (TAG,"ENG_IDLE_UPDATE idle_mode_RT_thread received: %d", engine_idle);
                if (!engine_idle)
                {
                    int num_files_recording = get_num_digital_cam_files_recording();
                    //Vehicle came out of idle state.
                    //In this case, we don't wait fot next session to begin for disabling engine idle
                    //because if vehicle come out of engine idle during a session,
                    //outward files in that session should be copied inorder not to
                    //miss any alerts.
                    if (cur_session_fname != "")
                    {
                        if (!update_engine_idle_for_fname (cur_session_fname, engine_idle, num_files_recording))
                        {
                            LOG_E (TAG, "Failed to update engine idle status");
                        }
                    }
                    if (cur_session_fname != "")
                    {
                        if (!update_engine_idle_for_fname (cur_session_fname, engine_idle, num_files_recording))
                        {
                            LOG_E (TAG, "Failed to update engine idle status");
                        }
                    }
                }
                break;

            case DRIVER_LOGIN_UPDATE:
                driver_login_update_msg = (driver_login_fr_update_msg_t*)g_msg;
                pthread_mutex_lock(&dis_mutex);

                ctx.dis_status = driver_login_update_msg->status;
#ifdef BAGHEERA2
                ctx.dis_update_ts = driver_login_update_msg->timestamp;
#elif KRAIT
                ctx.dis_update_ts = (int64_t)extract_time_64(driver_login_update_msg->timestamp1, driver_login_update_msg->timestamp2);
#endif
                LOG_I (TAG,"DRIVER_LOGIN_UPDATE received: %d, ts: %lld", ctx.dis_status, ctx.dis_update_ts);
                pthread_mutex_unlock(&dis_mutex);
                break;

            case PRIVACY_MODE_UPDATE:
            {
                if (ctx.privacy_params.enhanced_privacy) {
                    LOG_I(TAG, "Enhanced privacy is already enabled, no need to update the privacy");
                    break;
                }

                privacy_mode_update_msg = (privacy_mode_update_msg_t*)g_msg;
                if (privacy_mode_update_msg->privacy_on) {
                    privacy_activate_update_received = true;
                    privacy_deactivate_update_received = false;
                } else {
                    privacy_deactivate_update_received = true;
                    privacy_activate_update_received = false;
                }
                // Do not honor speed based privacy if the target time is not reached
                if (privacy_mode_update_msg->privacy_on && (get_system_monotonic_time() < speed_privacy_activate_target_time)) {
                    LOG_I(TAG, "Did not reach time to enable the speed based privacy, ignoring request. current time:%lld", get_system_monotonic_time());
                    //Reset the ignition events as we have to consider the latest events only
                    ignition_on_received = false;
                    ignition_off_received = false;
                    break;
                }
                if (!privacy_mode_update_msg->privacy_on && (get_system_monotonic_time() < speed_privacy_deactivate_target_time)) {
                    LOG_I(TAG, "Did not reach time to disable the speed based privacy, ignoring request. current time:%lld", get_system_monotonic_time());
                    //Reset the ignition events as we have to consider the latest events only
                    ignition_on_received = false;
                    ignition_off_received = false;
                    break;
                }
                bool old_privacy = -1; privacy_reason_t old_privacy_reason = REASON_NO_PRIVACY;
                bool ret = read_privacy_value_and_reason_from_file(old_privacy, old_privacy_reason, privacy_state_file_global);
                bool new_privacy = fuse_privacy(SOURCE_SPEED, ctx.fused_privacy, privacy_mode_update_msg->privacy_on);
                privacy_reason = new_privacy ? REASON_SPEED : REASON_NO_PRIVACY;
                write_privacy_to_file_with_reason(new_privacy, privacy_reason, privacy_state_file_global);
                bool old_fused = ctx.fused_privacy;
                if (!ctx.off_duty_privacy && !ctx.geofence_privacy) {
                    ctx.fused_privacy = new_privacy;
                    if (old_fused != ctx.fused_privacy) {
                        LOG_C(TAG, "ctx.fused_privacy changed: %d → %d (PRIVACY_MODE_UPDATE, geofence=%d, offduty=%d)",
                            old_fused, ctx.fused_privacy, ctx.geofence_privacy, ctx.off_duty_privacy);
                    }
                }
                if (old_privacy == new_privacy && old_privacy_reason == privacy_reason) {
                    LOG_I(TAG, "Privacy state is not changed, ignoring request");
                    break;
                }
                if (cur_session_fname != "") {
                    int num_files_recording = get_num_digital_cam_files_recording();
                    set_device_mode_for_fname(cur_session_fname, new_privacy, engine_idle, num_files_recording, privacy_reason);
                    // LOG_I(TAG, "ctx.fused_privacy %d inward privacy %d", ctx.fused_privacy, ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_BACK]);
                    write_privacy_state_file(ctx.fused_privacy);
                }
                if (!ctx.off_duty_privacy && !ctx.geofence_privacy && (ctx.inward_privacy_state != (ctx.fused_privacy && ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_BACK]))) {
                    LOG_I(TAG, "ctx.fused_privacy %d privacy_mode_update_msg->privacy_on %d", ctx.fused_privacy, privacy_mode_update_msg->privacy_on);
                    set_privacy_mode_led(ctx.fused_privacy && ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_BACK]);
                    check_set_irled(REASON_PRIVACY);
                    if (ctx.fused_privacy && ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_BACK]) {
                        LOG_I(TAG, "Privacy Mode is Activated");
                        send_privacy_status_audio_play(REGULAR, true);
                    } else {
                        LOG_I(TAG, "Privacy Mode is Deactivated");
                        send_privacy_status_audio_play(REGULAR, false);
                    }
                } 
                if (!ctx.privacy_params.enhanced_privacy && !ctx.off_duty_privacy && !ctx.geofence_privacy)
                    ctx.inward_privacy_state = ctx.fused_privacy && ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_BACK];
                else
                    ctx.inward_privacy_state = true;

                send_privacy_update(DEVICE_CAMERA_POSITION_BACK, old_privacy, ctx.fused_privacy);
                send_privacy_update(DEVICE_CAMERA_POSITION_FRONT, old_privacy, ctx.fused_privacy);

                //Need to consider the events for privacy which has come latest and discard old events
                ignition_on_received = false;
                ignition_off_received = false;
            }
            break;

            case SPEED_SERVICE_STARTED:
                LOG_I(TAG, "SPEED_SERVICE_STARTED msg received from speed");
                if (!ctx.privacy_params.enhanced_privacy && !ctx.off_duty_privacy && !ctx.geofence_privacy &&
                       ctx.privacy_params.privacy_activate_params.speed_based_privacy)
                    register_with_speed_for_privacy();
                if (!ctx.privacy_params.enhanced_privacy && !ctx.off_duty_privacy && !ctx.geofence_privacy &&
                       ctx.privacy_params.privacy_deactivate_params.speed_based_privacy)
                    register_with_speed_for_speed();
                break;

            case RES_IDLE_REG:
                res_idle_msg = (res_idle_reg_msg_t *)g_msg;
                if(res_idle_msg->reg_type == IDLE_REG_PRIVACY)
                {
                    LOG_I(TAG, "Registered with speed for idle privacy events");
                }
                break;
            case RES_IDLE_UPDATE:
                LOG_I(TAG, "Idle update received from speed");
                break;
            case RES_SPEED_REG:
                res_speed_msg = (res_speed_reg_msg_t *)g_msg;
                if(res_speed_msg->reg_type == SPEED_REG_PRIVACY)
                {
                    LOG_I(TAG, "Registered with speed for speed privacy events");
                }
                break;
            case RES_SPEED_UPDATE:
                LOG_I(TAG, "Speed update received from speed");
                break;
            case CONN_MGR_SIG_INFO:
            {
                sig_info_msg = (conn_mgr_sig_info_msg_t*)g_msg;
                LOG_I (TAG,"SIGINFO received");
                set_network_info (sig_info_msg);
            }
                break;
            case OBD_WAKEUP_SERVICE:
                obd_wakeup_msg = (obd_wakeup_msg_t *)g_msg;
                LOG_I(TAG, "Received OBD_WAKEUP_SERVICE");
                if(obd_wakeup_msg->length != sizeof(obd_wakeup_msg_t)) {
                    LOG_E(TAG, "obd_wakeup_msg->length != sizeof(obd_wakeup_msg_t)");
                    break;
                }

                if(ctx.obd_pid == obd_wakeup_msg->Pid) {
                    LOG_I(TAG, "received OBD_WAKEUP_SERVICE  for same PID %d; ignoring", obd_wakeup_msg->Pid);
                }
                LOG_I(TAG, "PID %d", (int)obd_wakeup_msg->Pid);

                ctx.obd_pid = obd_wakeup_msg->Pid;

                obd_subscribe_msg.msg_type = OBD_SUBSCRIBE_SERVICE;
                obd_subscribe_msg.length = sizeof(obd_subscribe_msg);
                obd_subscribe_msg.Pid = obd_wakeup_msg->Pid;
                obd_subscribe_msg.key[OBD_EVENT_SPEED] = true;

                send_msg((generic_msg_t*)&obd_subscribe_msg, OBD_SUBSCRIBE_SERVICE, sizeof(obd_subscribe_msg),
                            get_msgq_name(), ctx.obd_message_q_name, 0);

                break;

            case OBD_PROTOCOL_TYPE:
            {
                obd_proto_msg_t *obd_proto_msg = (obd_proto_msg_t*)g_msg;

                obd_protocol = obd_proto_msg->protocol;
                LOG_I(TAG, "received obd protocol msg %d", obd_proto_msg->protocol);
                break;
            }
#ifdef KRAIT
            case OBD_CONFIGURED_VIN_DATA:
            {
                 obd_vin_msg_t *obd_vin_msg = (obd_vin_msg_t*)g_msg;
                 memcpy (configured_obd_vin, obd_vin_msg->vin_value, MAX_SIZE_OF_VIN);
                 configured_obd_vin[MAX_SIZE_OF_VIN -1] = '\0';
                 int vin_len = strlen(configured_obd_vin);
                 bool response = vin_validation(vin_len);
                 if (response){
                     LOG_I(TAG, "configured vin data is %s", configured_obd_vin);
                 }
                 break;
            }
#endif
            case OBD_VIN_DATA:
            {
                 obd_vin_msg_t *obd_vin_msg = (obd_vin_msg_t*)g_msg;
                 memcpy (obd_vin, obd_vin_msg->vin_value, MAX_SIZE_OF_VIN);
                 obd_vin[MAX_SIZE_OF_VIN -1] = '\0';
                 int vin_len = strlen(obd_vin);
                 bool response = vin_validation(vin_len);
                 if (response){
                     LOG_I(TAG, "vin data is %s", obd_vin);
                 }
                 break;
            }

            case CAN_FW_VER:
            {
                can_fw_ver_msg_t *can_fw_ver_msg = (can_fw_ver_msg_t*)g_msg;
                memcpy(can_firmware_ver, can_fw_ver_msg->can_fw_ver, CAN_FW_VER_LEN);
                LOG_I(TAG, "received CAN FW version msg %s", can_firmware_ver);
                break;
            }
            case CAN_DETAILS:
            {   LOG_I(TAG, "Received CAN details msg");
                can_details_msg_t *can_details_msg = (can_details_msg_t*)g_msg;
                memcpy(can_sn, can_details_msg->can_sn, CAN_SN_LEN);
                memcpy(can_model, can_details_msg->can_model, CAN_MODEL_LEN);
                LOG_I(TAG, "received CAN model %s", can_model);
                LOG_I(TAG, "received CAN SN %s", can_sn);
                break;
            }

            case CAN_STATUS_MSG:
            {
                can_status_msg_t *can_status_msg = (can_status_msg_t*)g_msg;
                can_status = can_status_updated;
                can_status_updated = can_status_msg->can_status;
                LOG_I(TAG, "received CAN status %d", can_status_updated);
#ifdef KRAIT
                if (can_status_updated != can_status)
                {
                    char * req_params;
                    json_t *root = json_object();
                    json_t *obd_status = json_object();
                    json_object_set_new( obd_status, "timestamp", json_integer(get_system_time()) );
                    json_object_set_new( obd_status, "can_status_updated", json_integer(can_status_updated));
                    json_object_set_new( root, "obd_status", obd_status );
                    json_object_set_new( root, "isArray", json_string("true") );
                    req_params = json_dumps(root, 0);
                    if(req_params == NULL){
                        LOG_E(TAG,"JSON creation failed for HS message");
                        json_decref(root);
                        return;
                    }
                    LOG_I(TAG, "sending msg to hs: %s", req_params);
                    int length = strlen(req_params);
                    nd_service_obj->send_msg_healthstats(req_params, length);
                    json_decref(root);
                    free(req_params);
                }
#endif
                break;
            }
            case CAN_SRC_MSG:
                  {
                      can_src_msg_t *can_src_msg = (can_src_msg_t*)g_msg;
                      can_src = can_src_msg->can_src;
                      LOG_I(TAG, "received CAN src msg %d", can_src);
                      break;
                  }

            case CAN_CONNECTIVITY_STATUS_MSG:
            {
                can_connectivity_status_msg_t *can_connectivity_status_msg = (can_connectivity_status_msg_t*)g_msg;
                can_connectivity_status = can_connectivity_status_msg->can_connectivity_status;
                LOG_I(TAG, "received CAN connectivity status %d", can_connectivity_status);
                break;
	    }
            case RES_DRV_LOGIN_QUERY:
            {
                int num_files_recording = get_num_digital_cam_files_recording();
                resp_drvlogin = (resp_drv_login_query*)g_msg;
                LOG_I (TAG,"RES_DRV_LOGIN_QUERY received");
                if (resp_drvlogin->num_drvids == 0) {
                    set_driver_id(UNKNOWN_DRVID);
                } else {
                    set_driver_id (resp_drvlogin->drv_ids, resp_drvlogin->login_time, resp_drvlogin->num_drvids);
                }
#if 0
                //Here we are setting both local variable and global variable.
                //Once service restarts, since this message may come after first
                //START_CAMERA. In that case,  until next RESTART_CAMERA, privacy setting
                //will remain default which is wrong.
                //To avoid that, we are directly setting global variable.
                //This message will not take much time (less than 1 minute). Hence we can avoid
                //copying back camera videos and also since metadata file is written at the end
                //of one minute, metadata will have correct privacy mode values.
                //For next RESTART_CAMERA onwards, privacy will be set based on local
                //variable 'privacy_status'

                if (ctx.off_duty_privacy) {
                    LOG_I(TAG, "off-duty mode is already enabled, no need to update the privacy");
                    break;
                }

                if (ctx.geofence_privacy) {
                    LOG_I(TAG, "geofence mode is already enabled, no need to update the privacy");
                    break;
                }

                engine_idle = resp_drvlogin->idle_on;
                ctx.idle_mode_RT_thread = engine_idle;
                ctx.fused_privacy = fuse_privacy(SOURCE_SPEED, ctx.fused_privacy, resp_drvlogin->privacy_on);
                privacy_reason = ctx.fused_privacy ? REASON_SPEED : REASON_NO_PRIVACY;
                write_privacy_to_file_with_reason(ctx.fused_privacy, privacy_reason, privacy_state_file_global);
                if (ctx.inward_privacy_state != (ctx.fused_privacy && ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_BACK])) {
                    if (ctx.fused_privacy && ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_BACK]) {
                        LOG_I(TAG, "Privacy Mode is Activated");
                        send_privacy_status_audio_play(REGULAR, true);
                    } else {
                        LOG_I(TAG, "Privacy Mode is Deactivated");
                        send_privacy_status_audio_play(REGULAR, false);
                    }
                }
                if (!ctx.privacy_params.enhanced_privacy && !ctx.off_duty_privacy && !ctx.geofence_privacy)
                    ctx.inward_privacy_state = ctx.fused_privacy && ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_BACK];
                else
                    ctx.inward_privacy_state = true;
                LOG_I(TAG, "RES_DRV_LOGIN_QUERY ctx.fused_privacy %d", ctx.fused_privacy);

                if (currvid_fname != "")
                {
                    set_device_mode_for_fname(currvid_fname, ctx.fused_privacy, engine_idle, num_files_recording, privacy_reason);
                    set_privacy_mode_led(ctx.fused_privacy && ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_BACK]);
                    check_set_irled(REASON_PRIVACY);
                }
#endif
                break;
            }

            case POWERMON_IGNITION:
                {
                    powermon_ignition_msg_t *msg = (powermon_ignition_msg_t*)g_msg;
                    int64_t time = msg->crank_change_time;

                    LOG_I(TAG, "Ign status = %lld, crank_change_time = %lld, lpw_status = %lld", msg->status, time, msg->lpw_status);
                    ctx.meta_buff[ctx.session_flipflop].add_ignition_status_records(msg->status, time);

                    if (msg->status == static_cast<int64_t>(IGNITION_ON)) {
                        if (!ctx.off_duty_privacy && !ctx.privacy_params.enhanced_privacy &&
                            ctx.privacy_params.privacy_deactivate_params.ignition_based_privacy == true) {
                            write_ignition_state_file(IGNITION_ON);

                            ignition_on_received = true;
                            ignition_off_received = false;
                            post_ignition_on_target_time = get_system_monotonic_time() +
                                                           (ctx.privacy_params.privacy_deactivate_params.post_ignition_on_duration * 1000);
                            post_ignition_off_target_time = 0;
                        }
                        /* For handling mix scenario in LPW */
                        if (file_is_present(lpw_no_record_persistent_file)) {
                            LOG_I (TAG, "IGNITION ON received during LPW session, end LPW");
                            file_delete(lpw_no_record_persistent_file);
                            /* Update the lpw_no_record as partially active in metadata */
                            /* 0 → feature not active, 1 → feature active, 2 → feature partially active */
                            ctx.genmeta->update_genmeta_header("lpw_no_record", "2", ctx.session_flipflop);
                        }
                        LOG_I (TAG, "IGNITION ON received");
                        LOG_I (TAG, "setting ignition_override to true");
                        ctx.ignition_override = true;
                        ctx.ignition_status = IGNITION_STATUS_ON;
                        ctx.crank_level_RT_thread = CRANK_HIGH;
                        ctx.send_rt_frames_till_time = INT_64_MAX;
                        ctx.send_rt_frames_till_time_outward = INT_64_MAX;

                        LOG_I(TAG, "crank_level_RT_thread %d ctx.send_rt_frames_till_time %lld, ctx.send_rt_frames_till_time_outward = %lld",
                                ctx.crank_level_RT_thread, ctx.send_rt_frames_till_time, ctx.send_rt_frames_till_time_outward);

                        /* Below condition is added for, suppose post_ignition_on there is some duration in config file
                        * Device was idle and it was in privacy. Ignition made ON. So after device privacy is disabled
                        * it should come back to speed based privacy only after ignition ON time + post_igniton_on_duration
                        * + speed threshold time. Before this change it was coming back to privacy almost immediately */
                        if (ctx.privacy_params.privacy_deactivate_params.ignition_based_privacy && ctx.privacy_params.privacy_activate_params.speed_based_privacy == true)
                            speed_privacy_activate_target_time = get_system_monotonic_time() + ctx.privacy_params.privacy_activate_params.threshold_time * 1000 +
                                                                    ctx.privacy_params.privacy_deactivate_params.post_ignition_on_duration * 1000;
                    }
                    else if (msg->status == static_cast<int64_t>(IGNITION_OFF)) {
                        LOG_I (TAG, "IGNITION OFF received");
                        if (ctx.lpw_no_record) {
                            bool is_lpw_active = (file_is_present(lpw_no_record_persistent_file)) ? true : false;
                            if (is_lpw_active) {
                                if (msg->lpw_status == true) {
                                    LOG_I(TAG, "LPW NO record is already active");
                                } else {
                                    LOG_I(TAG, "Not LPW case");
                                    file_delete(lpw_no_record_persistent_file);
                                    ctx.genmeta->update_genmeta_header("lpw_no_record", "2", ctx.session_flipflop);
                                }
                            } else {
                                if (msg->lpw_status == true) {
                                    LOG_I(TAG, "LPW NO record case");
                                    file_touch(lpw_no_record_persistent_file);
                                    ctx.genmeta->update_genmeta_header("lpw_no_record", "2", ctx.session_flipflop);
                                }
                            }
                        }
                        if (msg->lpw_status == true) {
                            ctx.ignition_status = IGNITION_STATUS_LPW;
                        } else {
                            ctx.ignition_status = IGNITION_STATUS_OFF;
                        }

                        if (ctx.off_duty_privacy) {
                            bool old_privacy = ctx.off_duty_privacy;
                            ctx.off_duty_privacy = false;
                            file_delete(offduty_mode_activated_file);
                            LOG_I(TAG, "Off duty driving mode is deactivated");
                            send_privacy_status_audio_play(OFF_DUTY, false);
                            set_device_mode_for_fname(currvid_fname, ctx.off_duty_privacy, ctx.idle_mode_RT_thread, get_num_digital_cam_files_recording(), REASON_OFFDUTY);

                            // Geofence need not handle since offduty -> geofence will never come
                            if (ctx.privacy_params.enhanced_privacy) {
                                // Enhanced privacy mode remains active after offduty exit
                                activate_enhanced_privacy_mode(currvid_fname);
                            } else {
                                /* If ignition based privacy activation is enabled then we will activate privacy after offduty exit on ignition off*/
                                if (ctx.privacy_params.privacy_activate_params.ignition_based_privacy == true && ctx.privacy_params.privacy_activate_params.post_ignition_off_duration == 0) {
                                    ctx.fused_privacy = true;
                                    privacy_reason = REASON_IGNITION;
                                    write_privacy_state_file(ctx.fused_privacy);
                                    write_privacy_to_file_with_reason(ctx.fused_privacy, privacy_reason, privacy_state_file_global);
                                    if (currvid_fname != "") {
                                        set_device_mode_for_fname(currvid_fname, ctx.fused_privacy, ctx.idle_mode_RT_thread, get_num_digital_cam_files_recording(), privacy_reason);
                                        ctx.inward_privacy_state = ctx.fused_privacy && ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_BACK];
                                        set_privacy_mode_led(ctx.inward_privacy_state);
                                        check_set_irled(REASON_PRIVACY);
                                    }
                                    LOG_I(TAG, "Privacy Mode is Activated");
                                    send_privacy_status_audio_play(REGULAR, true);
                                } else {
                                    bool ret = read_privacy_value_and_reason_from_file(ctx.fused_privacy, privacy_reason, privacy_state_file_global);
                                    write_privacy_state_file(ctx.fused_privacy);
                                    if (currvid_fname != "") {
                                        set_device_mode_for_fname(currvid_fname, ctx.fused_privacy, engine_idle, get_num_digital_cam_files_recording(), privacy_reason);
                                        ctx.inward_privacy_state = ctx.fused_privacy && ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_BACK];
                                        set_privacy_mode_led(ctx.inward_privacy_state);
                                        check_set_irled(REASON_PRIVACY);
                                    }
                                    if (ctx.fused_privacy == true) {
                                        LOG_I(TAG, "Privacy Mode is Activated");
                                        send_privacy_status_audio_play(REGULAR, true);
                                    }
                                }
                                
                                send_privacy_update(DEVICE_CAMERA_POSITION_BACK, old_privacy, ctx.fused_privacy);
                                send_privacy_update(DEVICE_CAMERA_POSITION_FRONT, old_privacy, ctx.fused_privacy);

                                stringstream ss_p_meta("");
                                ss_p_meta.str("");
                                ss_p_meta << "personal_privacy" << ", " << ctx.off_duty_privacy;
                                ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;
                            }
                        }
                        if (!ctx.privacy_params.enhanced_privacy &&
                                ctx.privacy_params.privacy_activate_params.ignition_based_privacy == true) {
                            write_ignition_state_file(IGNITION_OFF);

                            ignition_off_received = true;
                            ignition_on_received = false;
                            post_ignition_off_target_time = get_system_monotonic_time() +
                                                            (ctx.privacy_params.privacy_activate_params.post_ignition_off_duration * 1000);
                            post_ignition_on_target_time = 0;
                        }

                        ctx.crank_level_RT_thread = CRANK_LOW;
                        if (ctx.send_rt_frames_till_time == INT_64_MAX) {
                            ctx.send_rt_frames_till_time = get_system_monotonic_time() +
                                                            ctx.post_ignition_analytics_secs*1000;
                        }

                        if (ctx.send_rt_frames_till_time_outward == INT_64_MAX) {
                            ctx.send_rt_frames_till_time_outward = get_system_monotonic_time() +
                                                            ctx.post_ignition_analytics_secs_outward*1000;
                        }

                        LOG_I(TAG, "crank_level_RT_thread %d ctx.send_rt_frames_till_time %lld, ctx.send_rt_frames_till_time_outward = %lld",
                                ctx.crank_level_RT_thread, ctx.send_rt_frames_till_time, ctx.send_rt_frames_till_time_outward);
                    }
                    else {
                        LOG_E (TAG, "IGNITION ERROR received");
                        LOG_I (TAG, "setting ignition_override to true");
                        ctx.ignition_override = true;
                        ctx.crank_level_RT_thread = CRANK_HIGH;
#ifdef KRAIT
                        if (ctx.privacy_params.privacy_deactivate_params.ignition_based_privacy) {
                            ctx.crank_level_RT_thread = CRANK_LOW;
                        }
#endif
                        ctx.send_rt_frames_till_time = INT_64_MAX;
                        ctx.send_rt_frames_till_time_outward = INT_64_MAX;
                        LOG_I(TAG, "crank_level_RT_thread %d ctx.send_rt_frames_till_time %lld, ctx.send_rt_frames_till_time_outward = %lld",
                                    ctx.crank_level_RT_thread, ctx.send_rt_frames_till_time, ctx.send_rt_frames_till_time_outward);
                    }
                }
                break;
            case BUTTON_LONG_PRESS: {
                user_alert_msg = (user_alert_msg_t *)g_msg;
#ifdef KRAIT
                int64_t time = (int64_t)extract_time_64(user_alert_msg->timestamp1, user_alert_msg->timestamp2);
#else
                int64_t time = user_alert_msg->timestamp;
#endif
                LOG_C(TAG, "BUTTON_LONG_PRESS received for button %d at %lld, ctx.privacy_params.off_duty_mode = %d, ctx.ignition_status = %d", user_alert_msg->button, time, ctx.privacy_params.off_duty_mode, ctx.ignition_status);

                if (ctx.privacy_params.off_duty_mode) {
                    if (ctx.ignition_status == IGNITION_STATUS_ON)
                        activate_off_duty_privacy(cur_session_fname, engine_idle);
                }
                //If off-duty mode is not enabled, then check for button based privacy
                else if (ctx.privacy_params.privacy_activate_params.button_based_privacy) {
                    activate_button_based_privacy(cur_session_fname, engine_idle);
                } else {
                    LOG_I(TAG, "Not taking any action, as required privacy condition is not met");
                }
                break;
            }

            case TIME_SYNC_DONE:
                {
                    LOG_I(TAG, "TIME_SYNC_DONE message received. Stopping gps updates to time sync service");
                    stop_gps_updates_to_time_sync = true;
                    if(ctx.ext_cam_feature_enabled == true) {
                        send_msg_mdvr_time_set();
                    }
                }
                break;
            case TIME_SYNC_RTC_JUMP:
                {
                    time_sync_rtc_msg_t  *time_sync_rtc_jump_msg  = (time_sync_rtc_msg_t *)g_msg;
                    LOG_I(TAG, "TIME_SYNC_RTC_JUMP message received:  %lld %lld", time_sync_rtc_jump_msg->rtc_jump_from, time_sync_rtc_jump_msg->rtc_jump_to);
                    ctx.rtc_jump_from_string =  to_string(time_sync_rtc_jump_msg->rtc_jump_from) ;
                    ctx.rtc_jump_to_string =  to_string(time_sync_rtc_jump_msg->rtc_jump_to) ;
                    int64_t savedRtcTime = 0 ;
                    prop_data_t entry;
                    if (get_property_DB("rtcValidTime", &entry, db_handle)) {
                        ctx.rtcValidTime_string = entry.value;
                    }
                    if(true == string_to_int64(ctx.rtcValidTime_string, savedRtcTime)){
                        // Don't update GENPROP db if the time jump is less than an hour.
                        // Update is required to handle the when savedRtcTime itself is wrong.
                        if( abs((long long int)(savedRtcTime - time_sync_rtc_jump_msg->rtc_jump_to)) > (60 * ONE_MINUTE_DURATION_MS) ) {
                            if((set_property_DB("rtcValidTime", ctx.rtc_jump_to_string )) == false) {
                                nd_service_obj->send_err_msg(SM_E_NDC_SET_PROP_DB_FAIL, time_sync_rtc_jump_msg->rtc_jump_to/ONE_MINUTE_DURATION_MS, "set_property_DB failed" );
                                LOG_E(TAG, "set_property_DB failed for rtcValidTime");
                            }
                        }
                    }
                    ctx.rtcValid_string = "2" ;
#ifdef KRAIT
                    if(ctx.ext_cam_feature_enabled == true) {
                        send_msg_mdvr_time_set();
                    }
#endif
                }
                break;
            case SUPERCAP_STATUS:
            {
                supercap_msg_t *msg = (supercap_msg_t *)g_msg;
                // Supercap message will be posted by APM for pfi events
                // Supercap message will be posted by power_monitor/diagnostic for bad_battery/thermal_throttling events
                bool is_from_apm = (strcmp(msg->client_id, Q_APM.c_str()) == 0) ? true : false;
                LOG_I(TAG, "SUPERCAP_STATUS message received, status = %d", msg->status);
                if( msg->status == BATTERY_ACTIVE) {
                    ctx.supercap_status = false;
                    // for BAG3: We configure IMU in WOM Mode.
                    // For Normal IMU Mode we need to reconfigure.
                    if (nd_device_obj->is_enable_imu_sensor_supported() && (true == is_from_apm)){
                        LOG_I(TAG, "SUPERCAP_STATUS message received, enable_imu_sensor");
                        enable_imu_sensor();
                    }
                } else {
                    ctx.supercap_status = true;
                }
            }
            break;

            case REQ_NDC_MAKE_ERROR_CALLBACK:
                {
                    req_error_callback_t *msg = (req_error_callback_t *)g_msg;

                    LOG_I(TAG, "MAKE_ERROR_CALLBACK message received");
                    for (int i = 1; i < CAMERA_POSITION_MAXIMUM; i++) {
                        cam_crash_status.status[i] = msg->crash_status[i];
                        LOG_I(TAG, "cam_crash_status.status[%d] = %d", i, cam_crash_status.status[i]);
                    }
                    record_component_errorcb(FATAL_CAMERA_OTHER, (void *)&cam_crash_status);
                }
                break;

            case REQ_NDC_START_OTHER_CAM_SESSION:
                {
                	req_cam_session_start_callback_t *msg = (req_cam_session_start_callback_t *)g_msg;

                	void *cam_pos = (void *)(size_t)msg->cam_num;
                    int cam_num = *((int*)(&cam_pos));
                    LOG_I(TAG, "START_OTHER_CAM_SESSION message received with session_fname = %s, cam_num = %d, is_LD = %d", msg->session_fname, cam_num, msg->is_LD);

                	if (msg->is_LD) {
                		LOG_I(TAG, "Calling record_timestamp_cb for cam num %d", msg->cam_num);
                		record_timestamp_cb(msg->mux_start_ts, msg->session_start_pts, cam_pos);
                	} else {
                        record_start_cb(msg->session_fname, msg->mux_start_ts, msg->session_start_pts, cam_num);
                	}
                }
                break;

            case REQ_NDC_END_OTHER_CAM_SESSION:
                {
                	req_cam_session_end_callback_t *msg = (req_cam_session_end_callback_t *)g_msg;

                    LOG_I(TAG, "END_OTHER_CAM_SESSION message received with session_fname = %s, cam_num = %d", msg->session_fname, msg->cam_num);
                    record_stop_cb(msg->session_fname, msg->mux_end_ts, msg->session_start_pts, msg->cam_num, msg->session_frame_count, msg->is_LD);
                }
                break;

            case REQ_NDC_MOVE_PARTIAL_FILES:
                {
                	req_move_partial_files_t *msg = (req_move_partial_files_t *)g_msg;

                    LOG_I(TAG, "MOVE_PARTIAL_FILES message received with cam_num = %d, filename = %s", msg->cam_num, msg->filename);

                    move_other_cam_partial_files(msg->filename, msg->cam_num);
                }
                break;

            case REQ_NDC_END_SESSION_NOW:
                {
                    req_end_session_now_t *msg = (req_end_session_now_t *)g_msg;

                    LOG_I(TAG, "END_SESSION_NOW message received");

                    for (int i = 1; i < CAMERA_POSITION_MAXIMUM; i++) {
                        cams_enabled[i] = msg->cams_enabled[i];
                    }

                    Config_parser c(BAGHEERACONFIG_INI);
                    bool get_override_val = true;
                    bool is_val_overridden = false;

                    if (cams_enabled[DEVICE_CAMERA_POSITION_DMS] && c.getParseStatus() && (c.getConfig ("camera", "dms_ld_enabled", "false", get_override_val, is_val_overridden) == "true")) {
                        dms_ld = true;
                    } else {
                        dms_ld = false;
                    }

                    for (int i = 1; i < CAMERA_POSITION_MAXIMUM; i++) {
                        if (cams_enabled[i] && (ctx.session_cnt[i] != ctx.session_cnt[DEVICE_CAMERA_POSITION_FRONT])) {
                            LOG_I(TAG, "END_SESSION_NOW: Changing the session count of camera %d from %d to %d", i, ctx.session_cnt[i], ctx.session_cnt[DEVICE_CAMERA_POSITION_FRONT]);
                            /* camStartTime gets updated at the start of every session after which
                               a session count gets incremented by 1, so we need to subtract 1 from
                               the session count to get the start time of the session in which only
                               front camera was recording.
                             */
                            ctx.camStartTime[i][(ctx.session_cnt[i]) % 2] = ctx.camStartTime[DEVICE_CAMERA_POSITION_FRONT][(ctx.session_cnt[DEVICE_CAMERA_POSITION_FRONT] - 1) % 2];
                            ctx.session_cnt[i] = ctx.session_cnt[DEVICE_CAMERA_POSITION_FRONT];
                        }
                    }
                    if (cams_enabled[DEVICE_CAMERA_POSITION_BACK] && inward_ld && (ctx.session_cnt_ld[DEVICE_CAMERA_POSITION_BACK] != ctx.session_cnt_ld[DEVICE_CAMERA_POSITION_FRONT])) {
                        LOG_I(TAG, "END_SESSION_NOW: Changing the LD session count of inward camera from %d to %d", ctx.session_cnt_ld[DEVICE_CAMERA_POSITION_BACK], ctx.session_cnt_ld[DEVICE_CAMERA_POSITION_FRONT]);
                        /* camStartTime_ld gets updated at the start of every session after which
                           a LD session count gets incremented by 1, so we need to subtract 1 from
                           the LD session count to get the start time of the session in which only
                           front camera was recording.
                         */
                        ctx.camStartTime_ld[DEVICE_CAMERA_POSITION_BACK][(ctx.session_cnt_ld[DEVICE_CAMERA_POSITION_BACK]) % 2] = ctx.camStartTime_ld[DEVICE_CAMERA_POSITION_FRONT][(ctx.session_cnt_ld[DEVICE_CAMERA_POSITION_FRONT] - 1) % 2];
                        ctx.session_cnt_ld[DEVICE_CAMERA_POSITION_BACK] = ctx.session_cnt_ld[DEVICE_CAMERA_POSITION_FRONT];
                    }

                    if (cam_crash_status.status[DEVICE_CAMERA_POSITION_FRONT]) {
                        LOG_I(TAG, "Outward camera crash detected, setting restart_pipeline_required to false");
                        msg->restart_pipeline_required = false;
                    }

                    if (msg->restart_pipeline_required) {
                        ctx.stop_inward_cam_zmqsub = true; // making it true so that a new zmq subscriber instance gets created on cam_rec restart
                        ctx.is_camrec_zmqpub_created = false; // making it false so that bagheera service waits for creation of zmq publisher before instantiating the zmq subscriber

                    }
                    ctx.media_recorder[DEVICE_CAMERA_POSITION_FRONT]->end_record_session(msg->restart_pipeline_required);

                    if (file_is_present(qr_scan_started_file) && msg->restart_pipeline_required) {
                        //Since cam_rec service has restarted, sending QR scan start message to it to continue the QR scan
                        req_qr_login_scan_msg_t qrscan_req_msg;

                        // Copy the saved QR scan tags to the message
                        memcpy(qrscan_req_msg.tags, saved_qr_scan_tags, sizeof(saved_qr_scan_tags));

                        LOG_I(TAG, "Since cam_rec service has restarted, sending QR scan start message to it to continue the QR scan");
                        send_qrscan_msg_to_cam_rec_service(get_msgq_name(), REQ_CAMREC_START_QR_SCAN, &qrscan_req_msg);
                    }
                }
                break;

            case CAMREC_ZMQPUB_CREATE_SUCCESS_MSG:
                {
                    camrec_zmqpub_create_done_msg_t *msg = (camrec_zmqpub_create_done_msg_t *)g_msg;
                    LOG_I(TAG, "CAMREC_ZMQPUB_CREATE_SUCCESS message received");
                    if (msg->cam_pos == DEVICE_CAMERA_POSITION_BACK)
                        ctx.is_camrec_zmqpub_created = true;
                }
                break;

#ifdef DMS_CAMERA_SUPPORTED
            case CAMREC_DMS_CONNECTION_STATUS_MSG:
                {
                    camrec_dms_connection_status_msg_t *msg = (camrec_dms_connection_status_msg_t *)g_msg;
                    LOG_I(TAG, "CAMREC_DMS_CONNECTION_STATUS_MSG message received");
                    if (msg->is_connected)
                        ctx.is_dms_connected = true;
                    else {
                        ctx.is_dms_connected = false;
                        cams_enabled[DEVICE_CAMERA_POSITION_DMS] = false;
                        dms_ld = false;
                    }   
                    write_dms_connection_status_file(ctx.is_dms_connected);
                }
                break;

            case CAMREC_DMS_HEALTH_INFO_MSG:
                {
                    camrec_dms_health_info_msg_t *msg = (camrec_dms_health_info_msg_t *)g_msg;
                    LOG_I(TAG, "CAMREC_DMS_HEALTH_INFO_MSG message received with irled status:%d, SN:%s, sensor temperature:%d, fault register values:[0x%x, 0x%x, 0x%x, 0x%x, 0x%x], config register values:[0x%x, 0x%x, 0x%x, 0x%x], session filename:%s",
                                    msg->irled_status, msg->dmsCam_SN, msg->dms_sensor_temperature,
                                    msg->fault_register_values.reg_0x0A, msg->fault_register_values.reg_0x0B, msg->fault_register_values.reg_0x0C, msg->fault_register_values.reg_0x0D, msg->fault_register_values.reg_0x0E,
                                    msg->config_register_values.reg_0x02, msg->config_register_values.reg_0x03, msg->config_register_values.reg_0x04, msg->config_register_values.reg_0x05,
                                    msg->session_filename);
                    send_dms_connection_healthstats(get_dms_connection_status(), msg->irled_status, msg->dmsCam_SN, msg->dms_sensor_temperature,
                                                    msg->fault_register_values, msg->config_register_values, msg->session_filename);
                }
                break;
#endif

            case ANALYTICS_SERVICE_RESTARTED_MSG:
                {
                    LOG_I(TAG, "ANALYTICS_SERVICE_RESTARTED message received");
                    ctx.media_recorder[DEVICE_CAMERA_POSITION_FRONT]->recreate_rt_shared_mem();
#ifdef KRAIT
                    ctx.media_recorder[DEVICE_CAMERA_POSITION_BACK]->recreate_rt_shared_mem();
#endif
#ifdef DMS_CAMERA_SUPPORTED
                    if (cams_enabled[DEVICE_CAMERA_POSITION_DMS]) {
                        recreate_dmscam_rt_shared_memory(&(ctx.rt_config[DEVICE_CAMERA_POSITION_DMS])); // Recreate DMS cam shared memory
                        send_dmscam_shm_recreated_msg_to_cam_rec_service(Q_NAME); // convey DMS cam SHM recreation to cam_rec service
                    }
#endif
                }
                break;

            case GEO_FENCE_STATUS_UPDATE:
		
		{
#ifdef KRAIT
                    geo_fence_msg_t *geo_fence_msg = (geo_fence_msg_t *)g_msg;
#else
                    geo_fence_msg_64_t *geo_fence_msg = (geo_fence_msg_64_t *)g_msg;
#endif

                    uint64_t timestamp = ((uint64_t)geo_fence_msg->timestamp2 << 32) | geo_fence_msg->timestamp1;

                    LOG_I(TAG, "GEO_FENCE_STATUS_UPDATE Received with geo_fence_status:%d, timestamp:%lld",
                          geo_fence_msg->geo_fence_status, timestamp);

                    if (geo_fence_msg->geo_fence_status == 1) {
                        // Device entered geofence area - activate geofence privacy
                        LOG_I(TAG, "Device entered geofence area, activating geofence privacy");
                        activate_geofence_privacy(cur_session_fname, engine_idle);
                    } else if (geo_fence_msg->geo_fence_status == 0) {
                        // Device exited geofence area - deactivate geofence privacy
                        LOG_I(TAG, "Device exited geofence area, deactivating geofence privacy");
                        deactivate_geofence_privacy(cur_session_fname, engine_idle);
                    }
                }
                break;


            case REQ_BAGHEERA_ADD_FILE:
                {
                    file_info_msg_t *file_info_msg = (file_info_msg_t *)g_msg;
                    LOG_I(TAG, "REQ_BAGHEERA_ADD_FILE Received with filename:%s, filesize:%d", file_info_msg->file_name, file_info_msg->file_size);
                    circular_buffer_update_file_db_msg_t updatefile_msg;

                    nd_strncpy(updatefile_msg.file_info.base_file_name, file_info_msg->file_name, FNAME_LEN);
                    updatefile_msg.file_info.file_size = GetFileSize(CIRCULAR_BUFFER_PATH + "/" + file_info_msg->file_name);
                    updatefile_msg.file_info.file_type = CIRCULAR_BUFFER_TYPE_NORMAL;
                    updatefile_msg.file_info.upl_vid_enabled = UPL_VID_ENABLED_DO_NOT_UPDATE;
                    updatefile_msg.file_info.rec_vid_enabled = REC_VID_ENABLED_DO_NOT_UPDATE;
                    //send msg to CB
                    send_msg((generic_msg_t *)&updatefile_msg, REQ_CIRCULAR_BUFFER_UPDATE_FILE_DB,
                                    sizeof(circular_buffer_update_file_db_msg_t), get_msgq_name(), ctx.circular_buffer_q_name, 0);
                }
                break;

            case DRIVER_LOGIN_AUDIO_NOTIFY:
                {
                    LOG_I(TAG, "DRIVER_LOGIN_AUDIO_NOTIFY message received");
                    driver_login_audio_notify_msg_t *audio_msg = (driver_login_audio_notify_msg_t *)msg->get_buffer();
                    AudioEventType audio_alert_type = static_cast<AudioEventType>(audio_msg->alert_type);

                    if(NULL == audio_msg) {
                        LOG_E(TAG, "DRIVER_LOGIN_AUDIO_NOTIFY message is NULL");
                        break;
                    }

                    LOG_I(TAG, "audio_msg->file: %s, audio_alert_type: %s, client_id: %s, status: %d", audio_msg->file, AudioEventType::toString(audio_alert_type).c_str(), audio_msg->client_id, audio_msg->status);

                    if(Q_AUDIOPLAYBACK == audio_msg->client_id) {
                        bool play_status = (AUDIO_PLAY_SUCCESS == audio_msg->status) ? true : false;
                        update_audio_request_played_status(audio_msg->file, audio_alert_type, play_status);

                        if((audio_alert_type >= AudioEventType::IgnAl) && (audio_alert_type <= AudioEventType::DrvLoginAl)) { // PowerMonitor audio alerts
                            send_msg((generic_msg_t *)audio_msg, DRIVER_LOGIN_AUDIO_NOTIFY, sizeof(driver_login_audio_notify_msg_t), get_msgq_name(), Q_NAME_power_monitor, 0);
                        }
                    }
                    else {
                        std::string audio_type = AudioEventType::toString(audio_alert_type);
                        if((audio_alert_type >= AudioEventType::IgnAl) && (audio_alert_type <= AudioEventType::DrvLoginAl)) { // PowerMonitor audio alerts
                            add_audio_request(audio_msg->file, audio_alert_type);
                        } else {
                            LOG_C(TAG, "Not Adding to Queue Unknown AudioEventType: %s", AudioEventType::toString(audio_alert_type).c_str());
                            break;
                        }
                        if(false == send_power_mon_alert_audio_play(audio_msg->file, audio_type)) {
                            update_audio_request_played_status(audio_msg->file, audio_alert_type, false);
                            send_msg((generic_msg_t *)audio_msg, DRIVER_LOGIN_AUDIO_NOTIFY, sizeof(driver_login_audio_notify_msg_t), get_msgq_name(), Q_NAME_power_monitor, 0);
                        }
                    }
                }
                break;

            case START_QR_SCAN_REQ:
                {
                    LOG_I(TAG, "START_QR_SCAN_REQ message received");
                    req_qr_login_scan_msg_t *qr_scan_msg = (req_qr_login_scan_msg_t *)msg->get_buffer();
                    resp_qr_login_scan_status_msg_t qr_scan_start_status_msg;

                    // Initialize response message
                    memset(&qr_scan_start_status_msg, 0, sizeof(qr_scan_start_status_msg));
                    qr_scan_start_status_msg.msg_type = START_QR_SCAN_RES;
                    qr_scan_start_status_msg.length = sizeof(resp_qr_login_scan_status_msg_t);

#ifdef KRAIT
                    // Take a backup of qr scan tags embedded in the start QR scan request message
                    memcpy(saved_qr_scan_tags, qr_scan_msg->tags, sizeof(saved_qr_scan_tags));

                    if (ctx.is_driverlogin_qr_enabled == false) {
                        LOG_I(TAG, "QR driver login feature is disabled");
                        qr_scan_start_status_msg.status = QR_SCAN_ERROR_FEATURE_DISABLED;
                        nd_strncpy(qr_scan_start_status_msg.reason, "QR driver login feature is disabled", sizeof(qr_scan_start_status_msg.reason));
                    } else if (cams_enabled[DEVICE_CAMERA_POSITION_BACK] == false) {
                        LOG_I(TAG, "Ignoring the start QR scan request as inward camera is disabled");
                        nd_service_obj->send_err_msg(SM_E_NDC_QRSCAN_INWARD_CAM_DISABLED, NDService::UNUSED_ERR_AUX_CODE, "QR scan requested when inward camera is disabled");

                        qr_scan_start_status_msg.status = QR_SCAN_ERROR_INWARD_CAM_DISABLED;
                        nd_strncpy(qr_scan_start_status_msg.reason, "Inward camera is disabled", sizeof(qr_scan_start_status_msg.reason));
                    } else if (qr_scan_started == true) {
                        LOG_I(TAG, "Ignoring the start QR scan request as QR scan is already in progress");
                        qr_scan_start_status_msg.status = QR_SCAN_STATUS_IN_PROGRESS;
                        nd_strncpy(qr_scan_start_status_msg.reason, "QR scan already in progress", sizeof(qr_scan_start_status_msg.reason));
                    } else {
                        // QR scanning is enabled and not started yet
                        bool ret = ctx.media_recorder[DEVICE_CAMERA_POSITION_BACK]->start_QR_scan(qr_scan_msg->tags);
                        if (ret == false) {
                            LOG_E(TAG, "QR scan failed to start as QR scanner instance creation failed");
                            nd_service_obj->send_err_msg(SM_E_NDC_QRSCAN_INIT_FAIL, NDService::UNUSED_ERR_AUX_CODE, "Failed to create QR scanner instance");

                            qr_scan_start_status_msg.status = QR_SCAN_STATUS_FAILED;
                            nd_strncpy(qr_scan_start_status_msg.reason, "QR scanner instance creation failed", sizeof(qr_scan_start_status_msg.reason));
                        } else {
                            pthread_mutex_lock(&qr_scan_start_mutex);

                            qr_scan_started = true;

                            LOG_I(TAG, "QR scan started successfully, starting IR LED blinking");

                            // Start with IR LED ON
                            qr_scan_irled_toggle_state = true;
                            check_set_irled(REASON_QR_SCAN, qr_scan_irled_toggle_state);

                            // Start the IR LED blinking thread
                            qr_scan_irled_thread_running = true;
                            int thread_result = pthread_create(&qr_scan_irled_thread, NULL, qr_scan_irled_blink_thread, NULL);
                            if (thread_result != 0) {
                                LOG_E(TAG, "Failed to create QR scan IR LED blink thread, error: %d", thread_result);
                                qr_scan_irled_thread_running = false;
                            }

                            pthread_mutex_unlock(&qr_scan_start_mutex);

                            // Add QR scan start time to healthstats
                            int64_t qr_scan_start_timestamp = get_system_time();
                            char *req_params;
                            json_t *root = json_object();
                            json_t *qr_scan_info = json_object();
                            json_object_set_new(qr_scan_info, "qr_scan_start_time", json_integer(qr_scan_start_timestamp));
                            json_object_set_new(root, "qr_scan_info", qr_scan_info);
                            json_object_set_new(root, "isArray", json_string("true"));

                            req_params = json_dumps(root, 0);
                            if (req_params == NULL) {
                                LOG_E(TAG, "JSON creation failed for QR scan healthstats message");
                                json_decref(root);
                            } else {
                                LOG_I(TAG, "Sending QR scan start time to healthstats: %s", req_params);
                                int length = strlen(req_params);
                                nd_service_obj->send_msg_healthstats(req_params, length);
                                json_decref(root);
                                free(req_params);
                            }
                            qr_scan_start_status_msg.status = QR_SCAN_STATUS_SUCCESS;
                            nd_strncpy(qr_scan_start_status_msg.reason, "QR scan started successfully", sizeof(qr_scan_start_status_msg.reason));
                        }
                    }
                    // Send response message in all cases
                    send_msg((generic_msg_t*)&qr_scan_start_status_msg, START_QR_SCAN_RES,
                             sizeof(qr_scan_start_status_msg), get_msgq_name(), Q_NAME_BTFV, 0);
#else
                    // Take a backup of qr scan tags embedded in the start QR scan request message
                    memcpy(saved_qr_scan_tags, qr_scan_msg->tags, sizeof(saved_qr_scan_tags));

                    if (ctx.is_driverlogin_qr_enabled == false) {
                        LOG_I(TAG, "QR driver login feature is disabled");
                        qr_scan_start_status_msg.status = QR_SCAN_ERROR_FEATURE_DISABLED;
                        nd_strncpy(qr_scan_start_status_msg.reason, "QR driver login feature is disabled", sizeof(qr_scan_start_status_msg.reason));
                        // Send response message to BTFV service
                        send_msg((generic_msg_t*)&qr_scan_start_status_msg, START_QR_SCAN_RES,
                                 sizeof(qr_scan_start_status_msg), get_msgq_name(), Q_NAME_BTFV, 0);
                    } else if (cams_enabled[DEVICE_CAMERA_POSITION_BACK] == false) {
                        LOG_I(TAG, "Ignoring the start QR scan request as inward camera is disabled");
                        nd_service_obj->send_err_msg(SM_E_NDC_QRSCAN_INWARD_CAM_DISABLED, NDService::UNUSED_ERR_AUX_CODE, "QR scan requested when inward camera is disabled");

                        qr_scan_start_status_msg.status = QR_SCAN_ERROR_INWARD_CAM_DISABLED;
                        nd_strncpy(qr_scan_start_status_msg.reason, "Inward camera is disabled", sizeof(qr_scan_start_status_msg.reason));
                        // Send response message to BTFV service
                        send_msg((generic_msg_t*)&qr_scan_start_status_msg, START_QR_SCAN_RES,
                                 sizeof(qr_scan_start_status_msg), get_msgq_name(), Q_NAME_BTFV, 0);
                    } else if (file_is_present(qr_scan_started_file)) {
                        LOG_I(TAG, "Ignoring the start QR scan request as QR scan is already in progress");
                        qr_scan_start_status_msg.status = QR_SCAN_STATUS_IN_PROGRESS;
                        nd_strncpy(qr_scan_start_status_msg.reason, "QR scan already in progress", sizeof(qr_scan_start_status_msg.reason));
                        // Send response message to BTFV service
                        send_msg((generic_msg_t*)&qr_scan_start_status_msg, START_QR_SCAN_RES,
                                 sizeof(qr_scan_start_status_msg), get_msgq_name(), Q_NAME_BTFV, 0);

                        // check if qr_scan_irled_blink_thread is running, if not start it
                        if (qr_scan_irled_thread_running) {
                            LOG_I(TAG, "QR scan IR LED blink thread is already running");
                        } else {
                            LOG_I(TAG, "Starting QR scan IR LED blink thread as it is not running");
                            pthread_mutex_lock(&qr_scan_start_mutex);

                            qr_scan_started = true;

                            // Start with IR LED ON
                            qr_scan_irled_toggle_state = true;
                            check_set_irled(REASON_QR_SCAN, qr_scan_irled_toggle_state);

                            // Start the IR LED blinking thread
                            qr_scan_irled_thread_running = true;
                            int thread_result = pthread_create(&qr_scan_irled_thread, NULL, qr_scan_irled_blink_thread, NULL);
                            if (thread_result != 0) {
                                LOG_E(TAG, "Failed to create QR scan IR LED blink thread, error: %d", thread_result);
                                qr_scan_irled_thread_running = false;
                            }

                            pthread_mutex_unlock(&qr_scan_start_mutex);
                        }
                    } else {
                        // QR scanning is enabled and not started yet, send message to camrec service to start QR scanning
                        LOG_I(TAG, "Sending REQ_CAMREC_START_QR_SCAN message to camrec service to start QR scanning");
                        send_qrscan_msg_to_cam_rec_service(get_msgq_name(), REQ_CAMREC_START_QR_SCAN, qr_scan_msg);
                    }
#endif
                }
                break;

            case STOP_QR_SCAN_REQ:
                {
                    LOG_I(TAG, "STOP_QR_SCAN_REQ message received");
                    req_qr_login_scan_msg_t *qr_scan_msg = (req_qr_login_scan_msg_t *)msg->get_buffer();
                    resp_qr_login_scan_status_msg_t qr_scan_stop_status_msg;
                    
                    // Initialize response message
                    memset(&qr_scan_stop_status_msg, 0, sizeof(qr_scan_stop_status_msg));
                    qr_scan_stop_status_msg.msg_type = STOP_QR_SCAN_RES;
                    qr_scan_stop_status_msg.length = sizeof(resp_qr_login_scan_status_msg_t);

                    if (ctx.is_driverlogin_qr_enabled == false) {
                        LOG_I(TAG, "QR driver login feature is disabled");
                        qr_scan_stop_status_msg.status = QR_SCAN_ERROR_FEATURE_DISABLED;
                        nd_strncpy(qr_scan_stop_status_msg.reason, "QR driver login feature is disabled", sizeof(qr_scan_stop_status_msg.reason));
                    } else if (qr_scan_started == false) {
                        LOG_I(TAG, "Ignoring the stop QR scan request as QR scan is not in progress");
                        qr_scan_stop_status_msg.status = QR_SCAN_ERROR_NOT_IN_PROGRESS;
                        nd_strncpy(qr_scan_stop_status_msg.reason, "No QR scan in progress to stop", sizeof(qr_scan_stop_status_msg.reason));
                    } else {
                        // QR scanning is enabled and currently running
                        LOG_I(TAG, "QR scan has stopped, restoring IRLEDs to the saved state");
#ifdef BAGHEERA2
                        file_delete(qr_scan_started_file);
#endif
                        pthread_mutex_lock(&qr_scan_start_mutex);

                        qr_scan_started = false;
                        ctx.qr_code_scan_status = 0;

                        // Stop the IR LED blinking thread
                        if (qr_scan_irled_thread_running) {
                            pthread_join(qr_scan_irled_thread, NULL);
                        }

                        check_set_irled(saved_irled_data.reason, saved_irled_data.value);

                        pthread_mutex_unlock(&qr_scan_start_mutex);

#ifdef KRAIT
                        ctx.media_recorder[DEVICE_CAMERA_POSITION_BACK]->stop_QR_scan();
#else
                        // send message to camrec service to stop QR scanning
                        send_qrscan_msg_to_cam_rec_service(get_msgq_name(), REQ_CAMREC_STOP_QR_SCAN, qr_scan_msg);
#endif
                        // Add QR scan stop time to healthstats
                        qr_scan_stop_timestamp = get_system_time();
                        char *req_params;
                        json_t *root = json_object();
                        json_t *qr_scan_info = json_object();
                        json_object_set_new(qr_scan_info, "qr_scan_stop_time", json_integer(qr_scan_stop_timestamp));
                        json_object_set_new(root, "qr_scan_info", qr_scan_info);
                        json_object_set_new(root, "isArray", json_string("true"));

                        req_params = json_dumps(root, 0);
                        if (req_params == NULL) {
                            LOG_E(TAG, "JSON creation failed for QR scan stop healthstats message");
                            json_decref(root);
                        } else {
                            LOG_I(TAG, "Sending QR scan stop time to healthstats: %s", req_params);
                            int length = strlen(req_params);
                            nd_service_obj->send_msg_healthstats(req_params, length);
                            json_decref(root);
                            free(req_params);
                        }
                        qr_scan_stop_status_msg.status = QR_SCAN_STATUS_SUCCESS;
                        nd_strncpy(qr_scan_stop_status_msg.reason, "QR scan stopped successfully", sizeof(qr_scan_stop_status_msg.reason));
                    }
                    // Send response message in all cases
                    send_msg((generic_msg_t*)&qr_scan_stop_status_msg, STOP_QR_SCAN_RES,
                             sizeof(qr_scan_stop_status_msg), get_msgq_name(), Q_NAME_BTFV, 0);
                }
                break;

            case RESP_CAMREC_QR_SCAN_STATUS:
                {
                    resp_camrec_qr_scan_status_msg_t *qrscan_camrec_resp = (resp_camrec_qr_scan_status_msg_t *)g_msg;
                    LOG_I(TAG, "RESP_CAMREC_QR_SCAN_STATUS message received with status: %d", qrscan_camrec_resp->status);
                    resp_qr_login_scan_status_msg_t qr_scan_start_status_msg;

                    // Initialize response message
                    memset(&qr_scan_start_status_msg, 0, sizeof(qr_scan_start_status_msg));
                    qr_scan_start_status_msg.msg_type = START_QR_SCAN_RES;
                    qr_scan_start_status_msg.length = sizeof(resp_qr_login_scan_status_msg_t);

                    if (qrscan_camrec_resp->status == false) {
                        LOG_E(TAG, "QR scan failed to start as QR scanner instance creation failed");
                        nd_service_obj->send_err_msg(SM_E_NDC_QRSCAN_INIT_FAIL, NDService::UNUSED_ERR_AUX_CODE, "Failed to create QR scanner instance");

                        qr_scan_start_status_msg.status = QR_SCAN_STATUS_FAILED;
                        nd_strncpy(qr_scan_start_status_msg.reason, "QR scanner instance creation failed", sizeof(qr_scan_start_status_msg.reason));
                    } else {
                        LOG_I(TAG, "QR scan started successfully, starting IR LED blinking");
                        file_touch(qr_scan_started_file);

                        pthread_mutex_lock(&qr_scan_start_mutex);
                        qr_scan_started = true;

                        // Start with IR LED ON
                        qr_scan_irled_toggle_state = true;
                        check_set_irled(REASON_QR_SCAN, qr_scan_irled_toggle_state);

                        // Start the IR LED blinking thread
                        qr_scan_irled_thread_running = true;
                        int thread_result = pthread_create(&qr_scan_irled_thread, NULL, qr_scan_irled_blink_thread, NULL);
                        if (thread_result != 0) {
                            LOG_E(TAG, "Failed to create QR scan IR LED blink thread, error: %d", thread_result);
                            qr_scan_irled_thread_running = false;
                        }
                        pthread_mutex_unlock(&qr_scan_start_mutex);

                        // Add QR scan start time to healthstats
                        int64_t qr_scan_start_timestamp = get_system_time();
                        char *req_params;
                        json_t *root = json_object();
                        json_t *qr_scan_info = json_object();
                        json_object_set_new(qr_scan_info, "qr_scan_start_time", json_integer(qr_scan_start_timestamp));
                        json_object_set_new(root, "qr_scan_info", qr_scan_info);
                        json_object_set_new(root, "isArray", json_string("true"));

                        req_params = json_dumps(root, 0);
                        if (req_params == NULL) {
                            LOG_E(TAG, "JSON creation failed for QR scan healthstats message");
                            json_decref(root);
                        } else {
                            LOG_I(TAG, "Sending QR scan start time to healthstats: %s", req_params);
                            int length = strlen(req_params);
                            nd_service_obj->send_msg_healthstats(req_params, length);
                            json_decref(root);
                            free(req_params);
                        }
                        qr_scan_start_status_msg.status = QR_SCAN_STATUS_SUCCESS;
                        nd_strncpy(qr_scan_start_status_msg.reason, "QR scan started successfully", sizeof(qr_scan_start_status_msg.reason));
                    }
                    // Send response message to BTFV service
                    send_msg((generic_msg_t*)&qr_scan_start_status_msg, START_QR_SCAN_RES,
                                sizeof(qr_scan_start_status_msg), get_msgq_name(), Q_NAME_BTFV, 0);
                }
                break;    

            case ERROR:
            default:
                LOG_I(TAG, "Unknown message %d received", g_msg->type);
                break;
        }

        delete msg;
    }
}

int imu_read_cb(struct imu_data *data_cb);


void timer_handler (int signum) {

    ndc_cam_restart_msg_t restart_cam_msg;
    restart_cam_msg.type = RESTART_CAMERA;
    restart_cam_msg.len = sizeof( ndc_cam_restart_msg_t );
    restart_cam_msg.cam_num = -1;

    nd_msgq_t::nd_msg_t msg((char *)&restart_cam_msg, sizeof(restart_cam_msg), false);
    ctx.msg_q->send(msg, nd_msgq_t::ND_MSG_MED);
}

void timer_setting() {
    struct sigaction sa;
    struct itimerval timer;

    /* Install timer_handler as the signal handler for SIGVTALRM. */
    memset (&sa, 0, sizeof (sa));
    sa.sa_handler = &timer_handler;
    sigaction (SIGALRM, &sa, NULL);

    /* Configure the timer to expire after 60 sec... */
    timer.it_value.tv_sec = 60;
    timer.it_value.tv_usec = 0;
    /* ... and every 60 sec after that. */
    timer.it_interval.tv_sec = 60;
    timer.it_interval.tv_usec = 0;
    /* Start a virtual timer. It counts down whenever this process is
    executing. */
    setitimer (ITIMER_REAL, &timer, NULL);
    LOG_I(TAG, "DONE Starting timer");
}

void bagheera_exit (void)
{
    //Only if main thread PID matches print the message
    if( main_thread_pid == getpid() ) {
        LOG_E (TAG, "Bagheera exited, this shouldn't happen");
        return;
    }
}

#ifdef BAGHEERA2

static int check_outward_camera_boot_status()
{
    int count = 0;
    bool isp_status = false;

    // ret_status[0] - ISP
    int ret_status = 0x0;

    sleep (DELAY_CAMERA_BOOT_STATUS_CHECK);

    if ((cams_enabled[DEVICE_CAMERA_POSITION_FRONT] == false) ||
        ((cams_enabled[DEVICE_CAMERA_POSITION_FRONT] == true) && nd_device_obj->check_isp_boot_status())) {
        ret_status |= 1<<0;
    }

    LOG_I (TAG, "Front camera boot status is %d", ret_status);
    return ret_status;
}
#endif

void *sync_files(void *args)
{
    bool val_overridden;
    string sync_freq_str = ctx.bagheera_config->getConfig("camera",
                 "sync_freq_in_millisecs", "1000", true, val_overridden);
    int sync_freq = 1000;

    if(string_to_integer(sync_freq_str, sync_freq) == false) {
        LOG_E(TAG, "failed to get sync frequency from config");
        sync_freq = 1000;
    }
    LOG_I(TAG, "sync frequency in millisecs is %d", sync_freq);

    while(1) {
        if (!fnamecb_invoked)
        {
            usleep(10000);
            continue;
        }

        for (int i=0; i<(NUM_CAMERAS+1); i++) {
            /* Added this condition for DMS camera */
            if (i == NUM_CAMERAS)
                i += NUM_CAMERAS;
            pthread_mutex_lock(&file_mutex);

            if (!cams_enabled [i] || ctx.fname[i] == "") {
                pthread_mutex_unlock(&file_mutex);
                continue;
            }

            stringstream ss;
            ss << ctx.base_path_cam0 << "/" << ctx.fname[i] << video_file_extn;
            string ss_temp = ss.str();
            int fd = open(ss.str().c_str(), O_RDONLY, 0);
            if (fd < 0) {
                LOG_I(TAG, "failed to open %s for sync", ss_temp.c_str());
                //pthread_mutex_unlock(&file_mutex);
                //continue;
            } else {
            	fsync(fd);
            	close(fd);
            }

            if ((i == DEVICE_CAMERA_POSITION_FRONT && outward_ld) || (i == DEVICE_CAMERA_POSITION_BACK && inward_ld) || (i == DEVICE_CAMERA_POSITION_DMS && dms_ld)) {
                ss.str("");
                ss << ctx.base_path_cam0 << "/" << ctx.fname[i] << video_file_extn << ld_extn;
                string ss_temp = ss.str();
                fd = open(ss.str().c_str(), O_RDONLY, 0);
                if (fd < 0) {
                    LOG_I(TAG, "failed to open %s for sync", ss_temp.c_str());
                } else {
                    fsync(fd);
                    close(fd);
                }
            }

            if ((i == DEVICE_CAMERA_POSITION_FRONT) && (dp_enabled == true)) {
                ss.str("");
                ss << ctx.base_path_cam0 << "/" << ctx.fname[i] << video_file_extn << dp_extn;
                string ss_temp = ss.str();
                fd = open(ss.str().c_str(), O_RDONLY, 0);
                if (fd < 0) {
                    LOG_I(TAG, "failed to open %s for sync", ss_temp.c_str());
                } else {
                    fsync(fd);
                    close(fd);
                }
            }

            pthread_mutex_unlock(&file_mutex);
        }

        usleep(sync_freq*1000);

        if (end_of_session_atomic) {
            LOG_I (TAG," end_of_session_atomic is true");
        } else {
            if (audio_enable == true) {
                vector< Audio::audio_t > temp_vec;
                pthread_mutex_lock(&audio_partial_file_mutex);
                temp_vec = ctx.audio->audio_partial_pcm_buff_vec ;
                ctx.audio->audio_partial_pcm_buff_vec.clear() ;
                ctx.audio->fd_partial_pcm = open (ctx.audio->audio_pcm_partial_file_base_path.c_str(), O_RDWR | O_CREAT | O_APPEND, 00644);
                for (auto i = temp_vec.begin(); i != temp_vec.end(); ++i) {
                    if ((*i).size != write(ctx.audio->fd_partial_pcm, (const void *)&((*i).data), (*i).size)) {
                        LOG_E(TAG, "audio partial pcm file write failed");
                    }
                }
                LOG_D(TAG, "partial pcm file %s", (ctx.audio->audio_pcm_partial_file_base_path).c_str());
                fsync(ctx.audio->fd_partial_pcm);
                close(ctx.audio->fd_partial_pcm);
                pthread_mutex_unlock(&audio_partial_file_mutex);
            }

            pthread_mutex_lock(&meta_partial_file_mutex);
            ctx.meta_buff[ctx.session_flipflop].pick_last_second_metadata(ctx.hdmaps_mode_enabled, ctx.imu_data, ctx.ublox_enabled);
            int fd_partial_csv = open (ctx.next_meta_csv_partial_path.c_str(), O_RDWR | O_CREAT | O_APPEND, 00644);
            pthread_mutex_unlock(&meta_partial_file_mutex);
            fsync(fd_partial_csv);
            close(fd_partial_csv);
        }
    }
}

void read_file(string file, string& str)
{
    ifstream f(file); //taking file as inputstream
    if(f) {
        ostringstream ss;
        ss << f.rdbuf(); // reading data
        str = ss.str();
    }
}

bool get_obs_record_time(string json_contents, int64_t* starttime, int64_t* endtime)
{
    json_t *root_json_data;
    json_error_t error;

    root_json_data = json_loads(json_contents.c_str(), 0, &error);

    if (root_json_data == NULL) {  //File not present or corrupted
        LOG_E(TAG, "Invalid json content", json_contents.c_str());
        return false;
    }

    json_t *start_time_t = json_object_get(root_json_data, "startTime");
    if( start_time_t == NULL ) {
        LOG_E(TAG, "startTime section not found in observation file");
        json_decref(root_json_data);
        return false;
    }
    *starttime = json_integer_value(start_time_t);

    json_t *end_time_t = json_object_get(root_json_data, "endTime");

    if( end_time_t == NULL ) {
        LOG_E(TAG, "endTime section not found in observation file");
        json_decref(root_json_data);
        return false;
    }
    *endtime = json_integer_value(end_time_t);

    json_decref(root_json_data);
    return true;
}

bool add_copy_status_to_json(const std::string json_path, int copy_status_for_cams)
{
    json_error_t error;
    json_t* root = json_load_file(json_path.c_str(), 0, &error);
    if (!root) {
        LOG_E(TAG, "Failed to load json: %s", error.text);
        return false;
    }

    std::string binary_str =
        std::bitset<CAMERA_POSITION_MAXIMUM>(copy_status_for_cams).to_string();

    json_object_set_new(
        root,
        "copy_status_cam",
        json_string(binary_str.c_str())
    );

    if (json_dump_file(root, json_path.c_str(), JSON_INDENT(2)) != 0) {
        LOG_E(TAG, "Failed to write updated json");
        json_decref(root);
        return false;
    }

    json_decref(root);
    return true;
}

bool get_partial_copy_status(string source_path)
{
    copy_status_for_cams = default_status_value;
    edit_status_for_cams = 0x0;
    remove_status_for_cams = default_status_value;
    ifstream inputFile(source_path);
    int64_t startTime = 0;
    int privacy_sec_count = 0;
    int64_t start_time_privacy = 0;
    int64_t start_time_inward_privacy = 0;
    bool need_to_copy = false;

    LOG_I(TAG, "Entered get_partial_copy_status");

    // Priority: Geofence > LPW > User Alert > Offduty > Enhanced > Regular
    
    // LPW no record case 
    if (ctx.previous_lpw_no_record) {
        copy_status_for_cams = 0x0;
        edit_status_for_cams = 0x0;
        remove_status_for_cams = default_status_value;
        LOG_I(TAG, "previous boot cycle was lpw no record case, going to delete the partial files");
        clear_disabled_camera_bits(copy_status_for_cams, edit_status_for_cams, remove_status_for_cams, true);
        return true;
    }

    // User alert case (only honored if no geofence or LPW)
    if (device_mode_global_partial.partial_privacy_params.save_user_alert_video &&
             device_mode_global_partial.partial_privacy_params.has_user_alert) {
        need_to_copy = true;
    }

    if (need_to_copy == true && device_mode_global_partial.privacy_status_geofence == PRIVACY_OFF) {
        copy_status_for_cams = default_status_value;
        edit_status_for_cams = 0x0;
        remove_status_for_cams = default_status_value;
        LOG_I(TAG, "Need to copy all CAM partial files because of user alert");
        clear_disabled_camera_bits(copy_status_for_cams, edit_status_for_cams, remove_status_for_cams, true);
        return true;
    }

    // Handle enhanced privacy for all cameras
    if (device_mode_global_partial.partial_privacy_params.enhanced_privacy) {
        if (device_mode_global_partial.privacy_status_geofence != PRIVACY_OFF) {
            if (device_mode_global_partial.privacy_status_geofence == PRIVACY_ON) {
                copy_status_for_cams = 0x0;
                edit_status_for_cams = 0x0;
                remove_status_for_cams = default_status_value;
                LOG_I(TAG, "Will delete all CAM partial files because Geofence mode is enabled");
            } else {
                // geofence mixed
                /* Geofence MIXED + Offduty ON */
                if (device_mode_global_partial.privacy_status_offduty == PRIVACY_ON) {
                    copy_status_for_cams = 0x0;
                    edit_status_for_cams = 0x0;
                    remove_status_for_cams = default_status_value;
                    LOG_I(TAG, "Will delete all CAM partial files because Geofence MIXED + OFF-duty mode is enabled");
                } else {
                    // offduty mixed/off + geofence mixed
                    // Handle inward and DMS camera
                    clear_bit(copy_status_for_cams, DEVICE_CAMERA_POSITION_BACK);
                    clear_bit(copy_status_for_cams, DEVICE_CAMERA_POSITION_DMS);
                    clear_bit(edit_status_for_cams, DEVICE_CAMERA_POSITION_BACK);
                    clear_bit(edit_status_for_cams, DEVICE_CAMERA_POSITION_DMS);
                    set_bit(remove_status_for_cams, DEVICE_CAMERA_POSITION_BACK);
                    set_bit(remove_status_for_cams, DEVICE_CAMERA_POSITION_DMS);

		    if(device_mode_global_partial.privacy_status == PRIVACY_MIXED) {
			    /* session has both enhanced privacy and offduty privacy */
			    for (int i = DEVICE_CAMERA_POSITION_FRONT; i <= DEVICE_CAMERA_POSITION_RIGHT; i++) {
				    if (i != DEVICE_CAMERA_POSITION_BACK) {
					    set_bit(edit_status_for_cams, i);
					    set_bit(copy_status_for_cams, i);
					    set_bit(remove_status_for_cams, i);
					    LOG_I(TAG, "Will edit CAM%d partial files because of Enhanced Privacy + Offduty Privacy Mode", i);
				    }
			    }
			    LOG_I(TAG, "Will delete Inward CAM partial files because of Enhanced Privacy + Offduty privacy");
		    } else {
			    copy_status_for_cams = 0x0;
			    edit_status_for_cams = 0x0;
			    remove_status_for_cams = default_status_value;
			    LOG_I(TAG, "Will delete all CAM partial files because Geofence MIXED + OFF-duty mode is enabled");
		    }
		}
            }
        } else {
            // geofence off
            if (device_mode_global_partial.privacy_status_offduty == PRIVACY_ON) {
                copy_status_for_cams = 0x0;
                edit_status_for_cams = 0x0;
                remove_status_for_cams = default_status_value;
                LOG_I(TAG, "Will delete all CAM partial files because OFF-duty mode is enabled");
            } else {
                // offduty mixed/off + geofence off
                // Handle inward and DMS camera
                clear_bit(copy_status_for_cams, DEVICE_CAMERA_POSITION_BACK);
                clear_bit(copy_status_for_cams, DEVICE_CAMERA_POSITION_DMS);
                clear_bit(edit_status_for_cams, DEVICE_CAMERA_POSITION_BACK);
                clear_bit(edit_status_for_cams, DEVICE_CAMERA_POSITION_DMS);
                set_bit(remove_status_for_cams, DEVICE_CAMERA_POSITION_BACK);
                set_bit(remove_status_for_cams, DEVICE_CAMERA_POSITION_DMS);

                // Handle cameras other than inward and DMS
                if (device_mode_global_partial.privacy_status_offduty == PRIVACY_MIXED) {
                    /* session has both enhanced privacy and offduty privacy */
                    for (int i = DEVICE_CAMERA_POSITION_FRONT; i <= DEVICE_CAMERA_POSITION_RIGHT; i++) {
                        if (i != DEVICE_CAMERA_POSITION_BACK) {
                            set_bit(edit_status_for_cams, i);
                            set_bit(copy_status_for_cams, i);
                            set_bit(remove_status_for_cams, i);
                            LOG_I(TAG, "Will edit CAM%d partial files because of Enhanced Privacy + Offduty Privacy Mode", i);
                        }
                    }
                    LOG_I(TAG, "Will delete Inward CAM partial files because of Enhanced Privacy + Offduty privacy");
                } else {
                    /* complete session is in enhanced privacy mode */
                    for (int i = DEVICE_CAMERA_POSITION_FRONT; i <= DEVICE_CAMERA_POSITION_RIGHT; i++) {
                        if (i != DEVICE_CAMERA_POSITION_BACK) {
                            clear_bit(edit_status_for_cams, i);
                            set_bit(copy_status_for_cams, i);
                            set_bit(remove_status_for_cams, i);
                            LOG_I(TAG, "Will copy CAM%d partial files because of Enhanced Privacy", i);
                        }
                    }
                    LOG_I(TAG, "Will delete Inward CAM partial files because of Enhanced Privacy");
                }
            }
        }
        clear_disabled_camera_bits(copy_status_for_cams, edit_status_for_cams, remove_status_for_cams, true);
        return true;
    }

    // Handle Geofence privacy for all cameras (non-enhanced privacy)
    if (device_mode_global_partial.privacy_status_geofence != PRIVACY_OFF) {
        if (device_mode_global_partial.privacy_status_geofence == PRIVACY_ON) {
            copy_status_for_cams = 0x0;
            edit_status_for_cams = 0x0;
            remove_status_for_cams = default_status_value;
            LOG_I(TAG, "Will delete all CAM partial files because Geofence mode is enabled");
        } else {
            if (device_mode_global_partial.privacy_status_offduty == PRIVACY_ON) {
                copy_status_for_cams = 0x0;
                edit_status_for_cams = 0x0;
                remove_status_for_cams = default_status_value;
                LOG_I(TAG, "Will delete all CAM partial files because Geofence MIXED + OFF-duty mode is enabled");
            } else {
                // offduty off/mixed + geofence mixed + REGULAR PRIVACY MIXED/OFF
                if (device_mode_global_partial.privacy_status != PRIVACY_ON) {
                    copy_status_for_cams = default_status_value;
                    edit_status_for_cams = default_status_value;
                    remove_status_for_cams = default_status_value;
                    LOG_I(TAG, "Will edit all CAM partial files because of multiple privacy in geo-fence mode");
                } else {
                    // regular on + geofence mixed and offduty mixed/off
                    for (int i = DEVICE_CAMERA_POSITION_FRONT; i <= DEVICE_CAMERA_POSITION_RIGHT; i++) {
                        if (!device_mode_global_partial.partial_privacy_params.cam_privacy[i]) {
                            set_bit(edit_status_for_cams, i);
                            set_bit(copy_status_for_cams, i);
                            set_bit(remove_status_for_cams, i);
                            if (i == DEVICE_CAMERA_POSITION_BACK) {
                                set_bit(edit_status_for_cams, DEVICE_CAMERA_POSITION_DMS);
                                set_bit(copy_status_for_cams, DEVICE_CAMERA_POSITION_DMS);
                                set_bit(remove_status_for_cams, DEVICE_CAMERA_POSITION_DMS);
                            }
                            LOG_I(TAG, "Will edit CAM%d partial file because of multiple privacy in geo-fence mode", i);
                        } else {
                            clear_bit(copy_status_for_cams, i);
                            clear_bit(edit_status_for_cams, i);
                            set_bit(remove_status_for_cams, i);
                            if (i == DEVICE_CAMERA_POSITION_BACK) {
                                clear_bit(copy_status_for_cams, DEVICE_CAMERA_POSITION_DMS);
                                clear_bit(edit_status_for_cams, DEVICE_CAMERA_POSITION_DMS);
                                set_bit(remove_status_for_cams, DEVICE_CAMERA_POSITION_DMS);
                            }
                            LOG_I(TAG, "Will delete CAM%d partial file because of multiple privacy", i);
                        }
                    }
                }
            }
        }
        clear_disabled_camera_bits(copy_status_for_cams, edit_status_for_cams, remove_status_for_cams, true);
        return true;
    }


    // Handle OFF-duty privacy for all cameras
    if (device_mode_global_partial.privacy_status_offduty != PRIVACY_OFF) {
        if (device_mode_global_partial.privacy_status_offduty == PRIVACY_ON) {
            copy_status_for_cams = 0x0;
            edit_status_for_cams = 0x0;
            remove_status_for_cams = default_status_value;
            LOG_I(TAG, "Will delete all CAM partial files because of OFF-duty privacy");
        } else {
            if (device_mode_global_partial.privacy_status == PRIVACY_MIXED) {
                copy_status_for_cams = default_status_value;
                edit_status_for_cams = default_status_value;
                remove_status_for_cams = default_status_value;
                LOG_I(TAG, "Will edit all CAM partial files because of multiple privacy in off-duty mode");
            } else {
                for (int i = DEVICE_CAMERA_POSITION_FRONT; i <= DEVICE_CAMERA_POSITION_RIGHT; i++) {
                    if (!device_mode_global_partial.partial_privacy_params.cam_privacy[i]) {
                        set_bit(edit_status_for_cams, i);
                        set_bit(copy_status_for_cams, i);
                        set_bit(remove_status_for_cams, i);
                        if (i == DEVICE_CAMERA_POSITION_BACK) {
                            set_bit(edit_status_for_cams, DEVICE_CAMERA_POSITION_DMS);
                            set_bit(copy_status_for_cams, DEVICE_CAMERA_POSITION_DMS);
                            set_bit(remove_status_for_cams, DEVICE_CAMERA_POSITION_DMS);
                        }
                        LOG_I(TAG, "Will edit CAM%d partial file because of multiple privacy in off-duty mode", i);
                    } else {
                        clear_bit(copy_status_for_cams, i);
                        clear_bit(edit_status_for_cams, i);
                        set_bit(remove_status_for_cams, i);
                        if (i == DEVICE_CAMERA_POSITION_BACK) {
                            clear_bit(copy_status_for_cams, DEVICE_CAMERA_POSITION_DMS);
                            clear_bit(edit_status_for_cams, DEVICE_CAMERA_POSITION_DMS);
                            set_bit(remove_status_for_cams, DEVICE_CAMERA_POSITION_DMS);
                        }
                        LOG_I(TAG, "Will delete CAM%d partial file because of multiple privacy", i);
                    }
                }
            }
        }
        clear_disabled_camera_bits(copy_status_for_cams, edit_status_for_cams, remove_status_for_cams, true);
        return true;
    }

    // Handle regular privacy for all cameras
    if (device_mode_global_partial.privacy_status == PRIVACY_OFF) {
        for (int i = DEVICE_CAMERA_POSITION_FRONT; i <= DEVICE_CAMERA_POSITION_RIGHT; i++) {
            clear_bit(edit_status_for_cams, i);
            set_bit(copy_status_for_cams, i);
            set_bit(remove_status_for_cams, i);
            if (i == DEVICE_CAMERA_POSITION_BACK) {
                clear_bit(edit_status_for_cams, DEVICE_CAMERA_POSITION_DMS);
                set_bit(copy_status_for_cams, DEVICE_CAMERA_POSITION_DMS);
                set_bit(remove_status_for_cams, DEVICE_CAMERA_POSITION_DMS);
            }
            LOG_I(TAG, "Will copy CAM%d partial file because of no privacy", i);
        }
    } else if (device_mode_global_partial.privacy_status == PRIVACY_ON) {
        edit_status_for_cams = 0x0;
        remove_status_for_cams = default_status_value;
        for (int i = DEVICE_CAMERA_POSITION_FRONT; i <= DEVICE_CAMERA_POSITION_RIGHT; i++) {
            if (device_mode_global_partial.partial_privacy_params.cam_privacy[i]) {
                clear_bit(copy_status_for_cams, i);
                if (i == DEVICE_CAMERA_POSITION_BACK) {
                    clear_bit(copy_status_for_cams, DEVICE_CAMERA_POSITION_DMS);
                }
                LOG_I(TAG, "Will delete CAM%d partial file because of regular privacy", i);
            } else {
                set_bit(copy_status_for_cams, i);
                if (i == DEVICE_CAMERA_POSITION_BACK) {
                    set_bit(copy_status_for_cams, DEVICE_CAMERA_POSITION_DMS);
                }
                LOG_I(TAG, "Will copy CAM%d partial file because of no privacy", i);
            }
        }
    } else {
        copy_status_for_cams = default_status_value;
        remove_status_for_cams = default_status_value;
        for (int i = DEVICE_CAMERA_POSITION_FRONT; i <= DEVICE_CAMERA_POSITION_RIGHT; i++) {
            if (device_mode_global_partial.partial_privacy_params.cam_privacy[i]) {
                set_bit(edit_status_for_cams, i);
                if (i == DEVICE_CAMERA_POSITION_BACK) {
                    set_bit(edit_status_for_cams, DEVICE_CAMERA_POSITION_DMS);
                }
                LOG_I(TAG, "Will edit CAM%d partial file because of regular privacy", i);
            } else {
                set_bit(copy_status_for_cams, i);
                if (i == DEVICE_CAMERA_POSITION_BACK) {
                    set_bit(copy_status_for_cams, DEVICE_CAMERA_POSITION_DMS);
                }
                LOG_I(TAG, "Will copy CAM%d partial file because of no privacy", i);
            }
        }
    }

    clear_disabled_camera_bits(copy_status_for_cams, edit_status_for_cams, remove_status_for_cams, true);
    return true;
}

bool is_data_outage_happened(const std::string destination_path, const std::string header)
{

    std::string file_content = "";

    read_file(destination_path,file_content);

    json_error_t error;
    json_t* root = json_loads(file_content.c_str(), 0, &error);

    if (!root) {
        LOG_E(TAG, "Failed to parse JSON in file %s at line %d: %s", destination_path.c_str(), error.line, error.text);
        return false;
    }

    json_t* header_data = json_object_get(root, header.c_str());
    if (json_is_object(header_data)) {
    LOG_I(TAG, "'%s' is an object", header.c_str());
} else if (json_is_array(header_data)) {
    LOG_I(TAG, "'%s' is an array", header.c_str());
} else {
    LOG_I(TAG, "'%s' is of unknown type", header.c_str());
}
    if (!header_data) {
        LOG_E(TAG, "'%s' entry is missing in the JSON file in %s", header.c_str(), destination_path.c_str());
        json_decref(root);
        return true;
    }

    if (json_array_size(header_data) == 0) {
        LOG_E(TAG, "'%s'  is an empty array in the JSON file at %s", header.c_str(), destination_path.c_str());
        json_decref(root);
        return true;
    }

    json_decref(root);
    return false;
}

#ifdef BAGHEERA2
void move_partial_files(string folder_name, vector<string> vec)
{
    string hs_reason = "partial";
    int64_t hs_starttime = -1, hs_endtime = -1;
    bool copied = false;
    /* The below variable added for, if there is any user alert in a partial session file,
     * it should check that and copy the file even if the camera is in any privacy state */
    bool need_to_copy = false;

    string csv_file = "";

    LOG_I(TAG, "moving partial files to circular buffer folder and send msg to add to db");

    for (vector<string>::iterator iter = vec.begin(), end = vec.end(); iter != end; iter++) {
        if ((*iter).find("partial.csv") != string::npos) {
            bool partial_res = false;
            LOG_I(TAG, "csv file found: %s", (*iter).c_str());
            string meta_partial_file = (*iter).substr(0, (*iter).find("_partial.csv")) + ".txt";
            string source_path = folder_name + "/" + (*iter) ;
            string destination_path = folder_name + "/" + meta_partial_file ;
            LOG_I(TAG, "source_path: %s", source_path.c_str());
            LOG_I(TAG, "destination_path: %s", destination_path.c_str());

            try {
                partial_res = csv_to_json(source_path, destination_path,
                                          device_mode_global_partial, partial_video_len_sec);

		LOG_I(TAG, "Final device_mode_global_partial privacy_status =%d, privacy_status_offduy = %d, privacy_status_geofence = %d\n", device_mode_global_partial.privacy_status, device_mode_global_partial.privacy_status_offduty , device_mode_global_partial.privacy_status_geofence );
            }
            catch (...) {
                LOG_E(TAG, "Exception in csv_to_json(), file: %s",(*iter).c_str() );
                nd_service_obj->send_err_msg(SM_E_NDC_CSV_JSON_FAIL, NDService::UNUSED_ERR_AUX_CODE, "Exception fron csv_to_json()");
                partial_res = false;
            }

            get_partial_copy_status(source_path);
            string copy_status_in_binary = std::bitset<CAMERA_POSITION_MAXIMUM>(copy_status_for_cams).to_string();
            string edit_status_in_binary = std::bitset<CAMERA_POSITION_MAXIMUM>(edit_status_for_cams).to_string();
            string remove_status_in_binary = std::bitset<CAMERA_POSITION_MAXIMUM>(remove_status_for_cams).to_string();
            LOG_I(TAG,"Final binary string for copy_status_for_cams, edit_status_for_cams and remove_status_for_cams are %s, %s and %s",
                       copy_status_in_binary.c_str(), edit_status_in_binary.c_str(), remove_status_in_binary.c_str());

            add_copy_status_to_json(destination_path, copy_status_for_cams);

            if (!partial_res) {
                stringstream command;
                FILE *fp = NULL;
                command.str("");
                string dest_full_name = "/home/ubuntu/.nddevice/log/ndcentral/" + *iter + ".log" ;
                command << "mv " << source_path << " " << dest_full_name;
                LOG_E(TAG, "move csv to log, command %s", command.str().c_str());
                fp = popen(command.str().c_str(), "r");
                if (fp == NULL)
                    LOG_E(TAG, "Failed to run mv csv file, command :: %s" , command.str().c_str() );
                else
                    pclose(fp);

                file_delete(source_path);
                file_delete(destination_path);
                nd_service_obj->send_err_msg(SM_E_NDC_PARTIAL_FILE_OPERATION_FAIL, NDService::UNUSED_ERR_AUX_CODE, ("Error generating observation file for partial session " + (*iter)));
                LOG_E(TAG, "error generating partial file; continue to next file");
                continue;
            }

            if (is_data_outage_happened(destination_path, "sensorMetaData")) {
                nd_service_obj->send_err_msg(SM_E_NDC_IMU_DATA_OUTAGE, NDService::UNUSED_ERR_AUX_CODE, ("IMU data outage for partial file " + destination_path));
            }

            if (is_data_outage_happened(destination_path, "videoMetaData")) {
                nd_service_obj->send_err_msg(SM_E_NDC_GPS_DATA_OUTAGE, NDService::UNUSED_ERR_AUX_CODE, ("GPS data outage for partial file " + destination_path));
            }
            string folder_name, file_prefix, json_contents;
            read_file(destination_path , json_contents);

            if (!get_folder_file_names(destination_path, folder_name, file_prefix))
                LOG_E(TAG, "Failed to get folder and file names from given path");

            std::string::size_type pos = file_prefix.find(".txt");
            if (pos != std::string::npos)
                file_prefix = file_prefix.substr(0, pos);

            int irled_status = -1;
            string irled_states;
            if (get_obs_irled_data(json_contents, &irled_status, &irled_states)) {
                LOG_I(TAG, "Partial - irled_status = %d, irled_states = %s", irled_status, irled_states.c_str());
                send_session_irled_info_healthstats(file_prefix, irled_status, irled_states);
            } else {
                LOG_I(TAG, "Error reading irled_status and irled_states from partial metadata file");
            }

            int64_t record_start, record_end;
            if (get_obs_record_time(json_contents, &record_start, &record_end)) {
                LOG_I(TAG, "Partial - rec start = %lld rec end = %lld", record_start, record_end);
                send_alert_info_recording_info_healthstats(file_prefix, record_start, record_end);
            } else {
                LOG_I(TAG, "Error reading start and end time from partial metadata file");
            }
            copy_metadata_for_obs_upload(folder_name, file_prefix, json_contents);
            copy_metadata_and_chm_for_scheduler (folder_name, file_prefix);

            //delete csv file after videos are processed
            csv_file = folder_name + "/" + (*iter);

            bool session_name_found = get_session_name_from_string(*iter, partial_session_name);
            if (session_name_found) {
                partial_session_name = partial_session_name.substr(1); //removed '0' from the session name
            } else {
                partial_session_name = " ";
            }

            //only one csv, so break
            break;
        }
    }

    if (ctx.udid_string == "-1") {   // when csv file is not there
        LOG_I(TAG, "csv file is not there or udid is not there");
        prop_data_t udid_entry;
        if (get_property_DB("udid", &udid_entry, db_handle)) {
            ctx.udid_string = udid_entry.value ;

            if (first_after_boot) {
                int64_t udidTemp = 0;
                string_to_int64(udid_entry.value, udidTemp);
                udidTemp -= 1;
                ctx.udid_string = to_string(udidTemp);
            }
        }
        LOG_I(TAG, "ctx.udid_string is %s", ctx.udid_string.c_str());
        prop_data_t sc_entry;
        ctx.sessionCount_string = "";
        if (get_property_DB("sessionCount", &sc_entry)) {
            ctx.sessionCount_string = sc_entry.value;
        } else {
            if ((set_property_DB("sessionCount", to_string(0))) == false) {
                nd_service_obj->send_err_msg(SM_E_NDC_SET_PROP_DB_FAIL, NDService::UNUSED_ERR_AUX_CODE, "set_property_DB failed" );
                LOG_E(TAG, "set_property_DB failed for sessionCount");
            }
            ctx.sessionCount_string = "0";
        }
        LOG_I(TAG, "ctx.sessionCount_string is %s", ctx.sessionCount_string.c_str());
    }

    string latest_inward_cam_file = "";
    string latest_inward_cam_ld_file = "";
    string latest_left_cam_file = "";
    string latest_right_cam_file = "";
    string latest_dms_cam_file = "";
    string latest_dms_cam_ld_file = "";

    string command = "";
    if (inward_ld) {
        command = "cd " + folder_name + "; ls -t 1_trip*.ld.mp4 | head -1";
        LOG_D(TAG, "command_executed: %s", command.c_str());
        system_execute_with_resp(TAG, command, latest_inward_cam_ld_file);
        if (latest_inward_cam_ld_file.size()) {
            latest_inward_cam_ld_file.resize(latest_inward_cam_ld_file.size() - 1);
            latest_inward_cam_file = latest_inward_cam_ld_file.substr(0, latest_inward_cam_ld_file.find(".ld.mp4"));
            LOG_I(TAG, "latest_inward_cam_file: %s", latest_inward_cam_file.c_str());
            LOG_I(TAG, "latest_inward_cam_ld_file: %s", latest_inward_cam_ld_file.c_str());
        } else {
            LOG_I(TAG, "No inward cam partial file present.");
        }
    } else {
        command = "cd " + folder_name + "; ls -t 1_trip*.mp4 | head -1";
        LOG_D(TAG, "command_executed: %s", command.c_str());
        system_execute_with_resp(TAG, command, latest_inward_cam_file);
        if (latest_inward_cam_file.size()) {
            latest_inward_cam_file.resize(latest_inward_cam_file.size() - 1);
            LOG_I(TAG, "latest_inward_cam_file: %s", latest_inward_cam_file.c_str());
        } else {
            LOG_I(TAG, "No inward cam partial file present.");
        }
    }

    command = "cd " + folder_name + "; ls -t 2_trip* | head -1";
    LOG_D(TAG, "command_executed: %s", command.c_str());
    system_execute_with_resp(TAG, command, latest_left_cam_file);
    if (latest_left_cam_file.size()) {
        latest_left_cam_file.resize(latest_left_cam_file.size() - 1);
        LOG_I(TAG, "latest_left_cam_file: %s", latest_left_cam_file.c_str());
    } else {
        LOG_I(TAG, "No left cam partial file present.");
    }

    command = "cd " + folder_name + "; ls -t 3_trip* | head -1";
    LOG_D(TAG, "command_executed: %s", command.c_str());
    system_execute_with_resp(TAG, command, latest_right_cam_file);
    if (latest_right_cam_file.size()) {
        latest_right_cam_file.resize(latest_right_cam_file.size() - 1);
        LOG_I(TAG, "latest_right_cam_file: %s", latest_right_cam_file.c_str());
    } else {
        LOG_I(TAG, "No right cam partial file present.");
    }

    if (dms_ld) {
        command = "cd " + folder_name + "; ls -t 8_trip*.ld.mp4 | head -1";
        LOG_D(TAG, "command_executed: %s", command.c_str());
        system_execute_with_resp(TAG, command, latest_dms_cam_ld_file);
        if (latest_dms_cam_ld_file.size()) {
            latest_dms_cam_ld_file.resize(latest_dms_cam_ld_file.size() - 1);
            latest_dms_cam_file = latest_dms_cam_ld_file.substr(0, latest_dms_cam_ld_file.find(".ld.mp4"));
            LOG_I(TAG, "latest_dms_cam_file: %s", latest_dms_cam_file.c_str());
            LOG_I(TAG, "latest_dms_cam_ld_file: %s", latest_dms_cam_ld_file.c_str());
        } else {
            LOG_I(TAG, "No dms cam partial file present.");
        }
    } else {
        command = "cd " + folder_name + "; ls -t 8_trip*.mp4 | head -1";
        LOG_D(TAG, "command_executed: %s", command.c_str());
        system_execute_with_resp(TAG, command, latest_dms_cam_file);
        if (latest_dms_cam_file.size()) {
            latest_dms_cam_file.resize(latest_dms_cam_file.size() - 1);
            LOG_I(TAG, "latest_dms_cam_file: %s", latest_dms_cam_file.c_str());
        } else {
            LOG_I(TAG, "No dms cam partial file present.");
        }
    }

    if (device_mode_global_partial.partial_privacy_params.save_user_alert_video &&
                    device_mode_global_partial.partial_privacy_params.has_user_alert) {
        need_to_copy = true;
    }

    for (vector<string>::iterator iter = vec.begin(), end = vec.end(); iter != end; iter++) {
        if ((*iter).find("partial.csv") != string::npos) {
            continue;
        }

        /* split the strings by "_" and store the substrings in vec
         * if file name is 0_trip_something, vec[0] will have "0", vec[1]
         * will have "trip" and vec[2] will have "something" */
        vector<string> vec = split_by_delim(*iter, "_");
        string cam_num_str = vec[0];
        int cam_num;
        if (string_to_integer(cam_num_str, cam_num) == false) {
            LOG_I(TAG, "failed to get cam num from filename, sending 0 as default");
            cam_num = DEVICE_CAMERA_POSITION_FRONT;
        }

        if (first_after_boot == false) {
            if ((((*iter).find("1_trip") != std::string::npos) && (*iter == latest_inward_cam_file)) ||
                (((*iter).find("1_trip") != std::string::npos) && (*iter == latest_inward_cam_ld_file)) ||
                (((*iter).find("2_trip") != std::string::npos) && (*iter == latest_left_cam_file)) ||
                (((*iter).find("3_trip") != std::string::npos) && (*iter == latest_right_cam_file)) ||
                (((*iter).find("8_trip") != std::string::npos) && (*iter == latest_dms_cam_file)) ||
                (((*iter).find("8_trip") != std::string::npos) && (*iter == latest_dms_cam_ld_file))) {

                LOG_I(TAG, "skip moving other cam file: %s", (*iter).c_str());
        	    continue;
            }
        }

        int partial_size = file_size(folder_name + "/" + (*iter));

        if (((*iter).find(video_file_extn) == string::npos) &&
            ((*iter).find(ea_extn) == string::npos) &&
            ((*iter).find(META_PARTIAL_FILE_SUFFIX) == string::npos) &&
            ((*iter).find(AUDIO_PARTIAL_FILE_SUFFIX) == string::npos) &&
	        ((*iter).find(audio_file_extn) == string::npos)) {
            // delete any files other than videos
            if (file_is_present(folder_name + "/" + (*iter)) == true) {
                LOG_E(TAG, "deleting unknown file %s with size %d", folder_name + "/" + (*iter), partial_size);
                file_delete(folder_name + "/" + (*iter));
            }
            continue;
        }

        // Need to make sure file has valid session format
        if (is_video_audio_file_name_valid((*iter)) == false) {
            LOG_E(TAG, "Invalid partial file name: %s, deleting it", (*iter).c_str());
            file_delete(folder_name + "/" + (*iter));
            continue;
        }

        if (!copy_status_for_cams && (file_is_present(folder_name + "/" + (*iter)) == true)) {
            file_delete(folder_name + "/" + (*iter));

            if ((*iter).find(ea_extn) != std::string::npos) {
                if (ctx.previous_lpw_no_record == true) {
                    LOG_I(TAG, "Deleting EA image file %s of partial session because previous session was low_power_no_record", (*iter).c_str());
                    post_uploader_add_ea_file_db((*iter), DEVICE_CAMERA_POSITION_FRONT, LPW_NO_CAPTURE);
                } else {
                    LOG_I(TAG, "Deleting EA image file %s of partial session because previous session was full privacy", (*iter).c_str());
                    post_uploader_add_ea_file_db((*iter), DEVICE_CAMERA_POSITION_FRONT, RECORD_PRIVACY);
                }
            } else {
                post_circular_buffer_add_file_db((*iter), CIRCULAR_BUFFER_TYPE_NORMAL, cam_num);
            }
            continue;
        }
        if ((*iter).find(AUDIO_PARTIAL_FILE_SUFFIX) != string::npos) {
            if (partial_size == 0) {
                LOG_I(TAG, "Audio file (.pcm) is of 0 size, deleting: %s", (folder_name + "/" + (*iter)).c_str());
                file_delete(folder_name + "/" + (*iter));
                nd_service_obj->send_err_msg(SM_E_NDC_AUDIO_RECORD_FAILED, NDService::UNUSED_ERR_AUX_CODE,
                    std::string("Audio pcm file has 0 size: ") + (*iter));
                continue;
            }
            LOG_I(TAG, "moving %s to SdCard folder with size %d", (*iter).c_str(), partial_size);
            string audio_partial_encoded_file = (*iter).substr(0, (*iter).find(AUDIO_PARTIAL_FILE_SUFFIX)) + ".aac";
            bool record_privacy = false;
            check_partial_session_audio_privacy(record_privacy);
            if (record_privacy) {
                file_delete(folder_name + "/" + (*iter));
                LOG_I(TAG, "delete file: %s, Privacy mode: %d, Enhanced Privacy: %d", (*iter).c_str(),
                            device_mode_global_partial.privacy_status, device_mode_global_partial.partial_privacy_params.enhanced_privacy);
                post_circular_buffer_add_file_db(audio_partial_encoded_file, CIRCULAR_BUFFER_TYPE_TEXT, 0);
                continue;
            }
            task_result_t timed_task_result;
            timed_task_result = nd_timed_task(audio_partial_encode_task, AUDIO_ENCODE_TIME_MAX, (void*)(*iter).c_str(), "audio_partial_encode_task");
            if (timed_task_result != TASK_SUCCESS) {
                LOG_E (TAG, "nd_timed_task for audio_encode failed");
            }
            file_delete(folder_name + "/" + (*iter));
            string source_path = AUDIO_PARTIAL_PCM_FILE_BASE_PATH + "/" + audio_partial_encoded_file ;
            string destination_path = CIRCULAR_BUFFER_PATH + "/" + audio_partial_encoded_file ;
            string filename_prefix = (*iter);
            if (GetFileSize(source_path.c_str()) == 0) {
                LOG_E(TAG, "Audio file (.aac) is of 0 size, deleting: %s", source_path.c_str());
                file_delete(source_path.c_str());
                nd_service_obj->send_err_msg(SM_E_NDC_AUDIO_ENCODE_FAILED, NDService::UNUSED_ERR_AUX_CODE,
                    std::string("Audio aac file has 0 size: ") + (*iter));
                continue;
            }
            LOG_I(TAG, "move_partial_files() dest: %s", destination_path.c_str() );
            if (audio_encryption) {
                if (ND_AUTH_SUCCESS != nd_file_operate_to_file(source_path.c_str(), destination_path.c_str())) {
                    LOG_E(TAG, "Audio File operate failed.");
                } else {
                    file_delete(source_path);
                }
            } else {
                if (!file_copy( source_path, destination_path, true, 0644, false)) {
                    LOG_E(TAG, "Audio partial file_copy failed");
                } else {
                    file_delete(source_path);
                }
            }

            //sync after move
            if (file_fd_sync( destination_path ) == false) {
                LOG_E(TAG, "failed to sync file after move");
            }
            int file_type = CIRCULAR_BUFFER_TYPE_NORMAL;
            if (destination_path.find(audio_file_extn) != string::npos) {
                file_type = CIRCULAR_BUFFER_TYPE_TEXT;
            }
            post_circular_buffer_add_file_db( audio_partial_encoded_file, file_type, 0);
            continue;
        } else {
            hs_starttime = get_system_time();

            /* Check for already existing zero sized .aac files */
            if ((*iter).find(audio_file_extn) != std::string::npos) {
                if (partial_size == 0) {
                    LOG_I(TAG, "Audio file (.aac) is of 0 size, deleting: %s", (folder_name + "/" + (*iter)).c_str());
                    file_delete(folder_name + "/" + (*iter));
                    continue;
                }
            }

            /* Move inward and dms cam video files */
            if (((*iter).find("1_trip") != std::string::npos) || (*iter).find("8_trip") != std::string::npos) {
                LOG_I(TAG, "moving %s to SdCard folder with size %d", (*iter).c_str(), partial_size);
                if (is_bit_set(edit_status_for_cams, DEVICE_CAMERA_POSITION_BACK) && ((*iter).find(partial_session_name) != std::string::npos)) {
                    LOG_I(TAG, "Applying partial privacy to partial video: %s, len %d", (*iter).c_str(), partial_video_len_sec);

                    if ((*iter).find("8_trip") != std::string::npos) {
                        if ((*iter).find(ld_extn) != std::string::npos) {
                            copied = apply_partial_privacy_and_save_video(
                                    folder_name + "/" + (*iter), /* src_file */
                                    CIRCULAR_BUFFER_PATH, /* dest_path */
                                    device_mode_global_partial, /* device_mode */
                                    DEVICE_CAMERA_POSITION_DMS, /* cam_num */
                                    true, /* ldFile */
                                    partial_video_len_sec /* video_length_sec */);
                        } else {
                            copied = apply_partial_privacy_and_save_video(
                                    folder_name + "/" + (*iter), /* src_file */
                                    CIRCULAR_BUFFER_PATH, /* dest_path */
                                    device_mode_global_partial, /* device_mode */
                                    DEVICE_CAMERA_POSITION_DMS, /* cam_num */
                                    false, /* ldFile */
                                    partial_video_len_sec /* video_length_sec */);
                        }
                    }
                    if ((*iter).find("1_trip") != std::string::npos) {
                        if ((*iter).find(ld_extn) != std::string::npos) {
                            copied = apply_partial_privacy_and_save_video(
                                    folder_name + "/" + (*iter), /* src_file */
                                    CIRCULAR_BUFFER_PATH, /* dest_path */
                                    device_mode_global_partial, /* device_mode */
                                    DEVICE_CAMERA_POSITION_BACK, /* cam_num */
                                    true, /* ldFile */
                                    partial_video_len_sec /* video_length_sec */);
                        } else {
                            copied = apply_partial_privacy_and_save_video(
                                    folder_name + "/" + (*iter), /* src_file */
                                    CIRCULAR_BUFFER_PATH, /* dest_path */
                                    device_mode_global_partial, /* device_mode */
                                    DEVICE_CAMERA_POSITION_BACK, /* cam_num */
                                    false, /* ldFile */
                                    partial_video_len_sec /* video_length_sec */);
                        }
                    }

                    file_delete(folder_name + "/" + (*iter));
                } else if (!is_bit_set(copy_status_for_cams, DEVICE_CAMERA_POSITION_BACK) &&
                              (file_is_present(folder_name + "/" + (*iter)) == true)) {
                    file_delete(folder_name + "/" + (*iter));
                    post_circular_buffer_add_file_db((*iter), CIRCULAR_BUFFER_TYPE_NORMAL, cam_num);
                    continue;
                }

                if (!copied) {
                    copied = move_files((folder_name + "/" + (*iter)), CIRCULAR_BUFFER_PATH);
                }

                if (copied == false) {
                    LOG_E(TAG, "failed to move partial file %s to circular buffer folder", (*iter).c_str());
                    nd_service_obj->send_err_msg(SM_E_NDC_PARTIAL_FILE_COPY_FAIL, NDService::UNUSED_ERR_AUX_CODE, ("Error copying file for partial session " + (*iter)));
                    // delete src file when failed to move
                    if (file_is_present(folder_name + "/" + (*iter)) == true) {
                        file_delete(folder_name + "/" + (*iter));
                    }
                    continue;
                }
            }

            // request for ext_cam files if outward non-ld file encountered
            // Handling ext cams file before outward/side cam files
            bool ext_camera_feature_flag = false;
            read_ext_camera_common_config(ext_camera_feature_flag);
            if ((cam_num == DEVICE_CAMERA_POSITION_FRONT) && (ext_camera_feature_flag) &&
                (((*iter).find(ld_extn.c_str(), 0) == string::npos) && ((*iter).find(dp_extn.c_str(), 0) == string::npos) &&
                ((*iter).find(ea_extn.c_str(), 0) == string::npos))) {

                if (ctx.previous_lpw_no_record) {
                    LOG_I(TAG, "previous boot cycle was lpw no record case, not requesting files from ext cam");
                } else {
                    LOG_I(TAG, "send_msg_ext_cam_partial_files() : %s ", (*iter).c_str());
                    send_msg_ext_cam_partial_files(folder_name, *iter, need_to_copy);
                }
            }

            /* Move outward and side cam video files*/
            if (((*iter).find("0_trip") != std::string::npos) ||
                ((*iter).find("2_trip") != std::string::npos) ||
                ((*iter).find("3_trip") != std::string::npos)) {
                LOG_I(TAG, "moving %s to SdCard folder with size %d", (*iter).c_str(), partial_size);

                int cam_num_temp = 0;
                if (((*iter).find("0_trip") != std::string::npos))
                    cam_num_temp = DEVICE_CAMERA_POSITION_FRONT;
                else if (((*iter).find("2_trip") != std::string::npos))
                    cam_num_temp = DEVICE_CAMERA_POSITION_LEFT;
                else
                    cam_num_temp = DEVICE_CAMERA_POSITION_RIGHT;

                if (is_bit_set(edit_status_for_cams, cam_num_temp) && ((*iter).find(partial_session_name) != std::string::npos)) {
                    //apply partial privacy (blackout) to the video.
                    LOG_I(TAG, "Applying partial privacy to partial video: %s, len %d", (*iter).c_str(), partial_video_len_sec);

                    if ((*iter).find("0_trip") != std::string::npos) {
                        if (((*iter).find(dp_extn) == std::string::npos) && ((*iter).find(ea_extn) == std::string::npos)) {
                            if ((*iter).find(ld_extn) != std::string::npos) {
                                copied = apply_partial_privacy_and_save_video(
                                        folder_name + "/" + (*iter), /* src_file */
                                        CIRCULAR_BUFFER_PATH, /* dest_path */
                                        device_mode_global_partial, /* device_mode */
                                        DEVICE_CAMERA_POSITION_FRONT, /* cam_num */
                                        true, /* ldFile */
                                        partial_video_len_sec /* video_length_sec */);
                            }
                            else {
                                copied = apply_partial_privacy_and_save_video(
                                        folder_name + "/" + (*iter), /* src_file */
                                        CIRCULAR_BUFFER_PATH, /* dest_path */
                                        device_mode_global_partial, /* device_mode */
                                        DEVICE_CAMERA_POSITION_FRONT, /*  cam_num */
                                        false, /* ldFile */
                                        partial_video_len_sec /* video_length_sec */);
                            }
                        } else {
                            if ((*iter).find(dp_extn) != std::string::npos) {
                                LOG_I(TAG, "Not applying partial privacy to DP partial video: %s", (*iter).c_str());
                                /* Since the framerate of DP file is configurable and for blacking out
                                 * of the frames in case of mixed privacy, we need a blackoutvideo with
                                 * predefined framerate which is not possible in case of DP video,
                                 * we are going to delete the DP file in case of mixed privacy.
                                 */
                                file_delete(folder_name + "/" + (*iter)); // Delete partial DP file in case of mixed privacy
                                continue;
                            } else if ((*iter).find(ea_extn) != std::string::npos) {
                                LOG_I(TAG, "Deleting EA image file %s of partial session because of PARTIAL_PRIVACY", (*iter).c_str());
                                file_delete(folder_name + "/" + (*iter)); // Delete EA image file in case of mixed privacy
                                post_uploader_add_ea_file_db((*iter), DEVICE_CAMERA_POSITION_FRONT, PARTIAL_PRIVACY);
                                continue;
                            }
                        }
                    } else {
                        if ((*iter).find("2_trip") != std::string::npos) {
                            copied = apply_partial_privacy_and_save_video(
                                    folder_name + "/" + (*iter), /* src_file */
                                    CIRCULAR_BUFFER_PATH, /* dest_path */
                                    device_mode_global_partial, /* device_mode */
                                    DEVICE_CAMERA_POSITION_LEFT, /* cam_num */
                                    false, /* ldFile */
                                    partial_video_len_sec /* video_length_sec */);
                        }
                        if ((*iter).find("3_trip") != std::string::npos) {
                            copied = apply_partial_privacy_and_save_video(
                                    folder_name + "/" + (*iter), /* src_file */
                                    CIRCULAR_BUFFER_PATH, /* dest_path */
                                    device_mode_global_partial, /* device_mode */
                                    DEVICE_CAMERA_POSITION_RIGHT, /* cam_num */
                                    false, /* ldFile */
                                    partial_video_len_sec /* video_length_sec */);
                        }
                    }
                    file_delete(folder_name + "/" + (*iter));
                } else if (!is_bit_set(copy_status_for_cams, cam_num_temp) && ((*iter).find(partial_session_name) != std::string::npos)) {
                    file_delete(folder_name + "/" + (*iter));

                    if ((*iter).find(ea_extn) != std::string::npos) {
                        LOG_I(TAG, "Deleting EA image file %s of partial session because of RECORD_PRIVACY", (*iter).c_str());
                        post_uploader_add_ea_file_db((*iter), DEVICE_CAMERA_POSITION_FRONT, RECORD_PRIVACY);
                    } else {
                        post_circular_buffer_add_file_db((*iter), CIRCULAR_BUFFER_TYPE_NORMAL, cam_num);
                    }
                    continue;
                }

                if (!copied) {
                    if ((*iter).find(ea_extn) == std::string::npos) {
                        copied = move_files((folder_name + "/" + (*iter)), CIRCULAR_BUFFER_PATH);
                    } else {
                        bool should_upload_vid = device_mode_global_partial.partial_privacy_params.upload_video[DEVICE_CAMERA_POSITION_FRONT];
                        if ((device_mode_global_partial.privacy_status == PRIVACY_ON) && (need_to_copy == false) && (should_upload_vid == false)) {
                            LOG_I(TAG, "Deleting EA image file %s of partial session because of UPLOAD_PRIVACY", (*iter).c_str());
                            file_delete(folder_name + "/" + (*iter));
                            post_uploader_add_ea_file_db((*iter), DEVICE_CAMERA_POSITION_FRONT, UPLOAD_PRIVACY);
                        } else {
                            LOG_I(TAG, "Copying EA image file %s of partial session to sdcard", (*iter).c_str());
                            copied = move_files((folder_name + "/" + (*iter)), CIRCULAR_BUFFER_PATH_EA);
                            if (copied == true) {
                                // Get the status of corresponding partial video file
                                string video_fname = (*iter);
                                size_t pos = video_fname.find(ea_extn);
                                if (pos != string::npos) {
                                    video_fname.replace(pos, ea_extn.length(), ".mp4");
                                }
                                if ((GetFileSize(folder_name + "/" + video_fname) > 0) || (GetFileSize(CIRCULAR_BUFFER_PATH + "/" + video_fname) > 0)) {
                                    LOG_I(TAG, "Posting the availability of EA image file %s of partial session to uploader", (*iter).c_str());
                                    post_uploader_add_ea_file_db((*iter), DEVICE_CAMERA_POSITION_FRONT, NO_PRIVACY);
                                } else {
                                    LOG_I(TAG, "Avoid posting the availability of EA image file %s of partial session to uploader because the corresponding video is either not available or is of 0 size", (*iter).c_str());
                                    if (file_is_present(CIRCULAR_BUFFER_PATH_EA + "/" + (*iter)) == true) {
                                        LOG_I(TAG, "Deleting EA image file %s of partial session from the sdcard", (*iter).c_str());
                                        file_delete(CIRCULAR_BUFFER_PATH_EA + "/" + (*iter));
                                    }
                                    nd_service_obj->send_err_msg(SM_E_NDC_PARTIAL_EA_IMG_WITH_NO_VIDEO, NDService::UNUSED_ERR_AUX_CODE, ("Partial EA image available but corresponding video not available " + (*iter)));
                                }
                            } else {
                                LOG_I(TAG, "EA image file %s of partial session is of 0 size, hence not posting to uploader", (*iter).c_str());

                                // Get the status of corresponding partial video file
                                string video_fname = (*iter);
                                size_t pos = video_fname.find(ea_extn);
                                if (pos != string::npos) {
                                    video_fname.replace(pos, ea_extn.length(), ".mp4");
                                }
                                if ((GetFileSize(folder_name + "/" + video_fname) > 0) || (GetFileSize(CIRCULAR_BUFFER_PATH + "/" + video_fname) > 0)) {
                                    nd_service_obj->send_err_msg(SM_E_NDC_PARTIAL_VIDEO_WITH_NO_EA_IMG, NDService::UNUSED_ERR_AUX_CODE, ("Partial EA image not available but corresponding video available " + (*iter)));
                                }
                            }
                        }
                    }
                }

                if (copied == false) {
                    LOG_E(TAG, "failed to move partial file %s to circular buffer folder", (*iter).c_str());
                    nd_service_obj->send_err_msg(SM_E_NDC_PARTIAL_FILE_COPY_FAIL, NDService::UNUSED_ERR_AUX_CODE, ("Error copying file for partial session " + (*iter)));
                    // delete src file when failed to move
                    if (file_is_present(folder_name + "/" + (*iter)) == true) {
                        file_delete(folder_name + "/" + (*iter));
                    }
                    continue;
                }
            }
        }

        hs_endtime = get_system_time();

        int file_type = CIRCULAR_BUFFER_TYPE_NORMAL;
        if ((*iter).find(audio_file_extn) != string::npos) {
            file_type = CIRCULAR_BUFFER_TYPE_TEXT;
        }

        if (cam_num < DEVICE_CAMERA_POSITION_MAX || cam_num == DEVICE_CAMERA_POSITION_DMS) {
            if ((*iter).find(ea_extn) == std::string::npos) {
                post_circular_buffer_add_file_db((*iter), file_type, cam_num);
                fill_map_add_file_healthstats( (folder_name + "/" + (*iter)), CIRCULAR_BUFFER_PATH, cam_num, hs_starttime, hs_endtime, copied, hs_reason, 0);
            }
        }

        copied = false;
    }
    if (csv_file != "") {
        file_delete(csv_file);
    }
}

#elif KRAIT
void move_partial_files(string folder_name, vector<string> vec)
{
    string hs_reason = "partial";
    int64_t hs_starttime = -1, hs_endtime = -1;
    bool copied = false;
    /* The below variable added for, if there is any user alert in a partial session file,
     * it should check that and copy the file even if the camera is in any privacy state */
    bool need_to_copy = false;

    string csv_file = "";

    LOG_I(TAG, "moving partial files to circular buffer folder and send msg to add to db");

    for (vector<string>::iterator iter = vec.begin(), end = vec.end(); iter != end; iter++) {
        if ((*iter).find("partial.csv") != string::npos) {
            bool partial_res = false;
            LOG_I(TAG, "csv file found: %s", (*iter).c_str());
            string meta_partial_file = (*iter).substr(0, (*iter).find("_partial.csv")) + ".txt";
            string source_path = folder_name + "/" + (*iter) ;
            string destination_path = folder_name + "/" + meta_partial_file ;
            LOG_I(TAG, "source_path: %s", source_path.c_str());
            LOG_I(TAG, "destination_path: %s", destination_path.c_str());

            try {
                partial_res = csv_to_json(source_path, destination_path,
                                          device_mode_global_partial, partial_video_len_sec);
            }
            catch (...) {
                LOG_E(TAG, "Exception in csv_to_json(), file: %s",(*iter).c_str() );
                nd_service_obj->send_err_msg(SM_E_NDC_CSV_JSON_FAIL, NDService::UNUSED_ERR_AUX_CODE, "Exception fron csv_to_json()");
                partial_res = false;
            }

            get_partial_copy_status(source_path);
            string copy_status_in_binary = std::bitset<CAMERA_POSITION_MAXIMUM>(copy_status_for_cams).to_string();
            string edit_status_in_binary = std::bitset<CAMERA_POSITION_MAXIMUM>(edit_status_for_cams).to_string();
            string remove_status_in_binary = std::bitset<CAMERA_POSITION_MAXIMUM>(remove_status_for_cams).to_string();
            LOG_I(TAG,"Final binary string for copy_status_for_cams, edit_status_for_cams and remove_status_for_cams are %s, %s and %s",
                       copy_status_in_binary.c_str(), edit_status_in_binary.c_str(), remove_status_in_binary.c_str());

            add_copy_status_to_json(destination_path, copy_status_for_cams);

            if (!partial_res) {
                stringstream command;
                FILE *fp = NULL;
                command.str("");
                string dest_full_name = "/home/ubuntu/.nddevice/log/ndcentral/" + *iter + ".log" ;
                command << "mv " << source_path << " " << dest_full_name;
                LOG_E(TAG, "move csv to log, command %s", command.str().c_str());
                fp = popen(command.str().c_str(), "r");
                if (fp == NULL)
                    LOG_E(TAG, "Failed to run mv csv file, command :: %s" , command.str().c_str() );
                else
                    pclose(fp);

                file_delete(source_path);
                file_delete(destination_path);
                LOG_E(TAG, "error generating partial file; continue to next file");
                nd_service_obj->send_err_msg(SM_E_NDC_PARTIAL_FILE_OPERATION_FAIL, NDService::UNUSED_ERR_AUX_CODE, ("Error generating observation file for partial session " + (*iter)));
                continue;
            }
            if (is_data_outage_happened(destination_path, "imuData")) {
                nd_service_obj->send_err_msg(SM_E_NDC_IMU_DATA_OUTAGE, NDService::UNUSED_ERR_AUX_CODE, ("IMU data outage for partial session " + destination_path));
            }

           if (is_data_outage_happened(destination_path, "videoMetaData")) {
                nd_service_obj->send_err_msg(SM_E_NDC_GPS_DATA_OUTAGE, NDService::UNUSED_ERR_AUX_CODE, ("GPS data outage for partial file " + destination_path));
            }
            string folder_name, file_prefix, json_contents;
            read_file(destination_path , json_contents);

            if (!get_folder_file_names(destination_path, folder_name, file_prefix))
                LOG_E(TAG, "Failed to get folder and file names from given path");

            std::string::size_type pos = file_prefix.find(".txt");
            if (pos != std::string::npos)
                file_prefix = file_prefix.substr(0, pos);

            int irled_status = -1;
            string irled_states;
            if (get_obs_irled_data(json_contents, &irled_status, &irled_states)) {
                LOG_I(TAG, "Partial - irled_status = %d, irled_states = %s", irled_status, irled_states.c_str());
                send_session_irled_info_healthstats(file_prefix, irled_status, irled_states);
            } else {
                LOG_I(TAG, "Error reading irled_status and irled_states from partial file");
            }

            int64_t record_start, record_end;
            if (get_obs_record_time(json_contents, &record_start, &record_end)) {
                LOG_I(TAG, "Partial - rec start = %lld rec end = %lld", record_start, record_end);
                send_alert_info_recording_info_healthstats(file_prefix, record_start, record_end);
            } else {
                LOG_I(TAG, "Error reading start and end time from partial file");
            }
            copy_metadata_for_obs_upload(folder_name, file_prefix, json_contents);
            copy_metadata_and_chm_for_scheduler (folder_name, file_prefix);

            //delete csv file after videos are processed
            csv_file = folder_name + "/" + (*iter);

            bool session_name_found = get_session_name_from_string(*iter, partial_session_name);
            if (session_name_found) {
                partial_session_name = partial_session_name.substr(1); //removed '0' from the session name
            } else {
                partial_session_name = " ";
            }

            //only one csv, so break
            break;
        }
    }

    if (ctx.udid_string == "-1") {   // when csv file is not there
        LOG_I(TAG, "csv file is not there or udid is not there");
        prop_data_t udid_entry;
        if (get_property_DB("udid", &udid_entry, db_handle)) {
            ctx.udid_string = udid_entry.value ;

            if (first_after_boot) {
                int64_t udidTemp = 0;
                string_to_int64(udid_entry.value, udidTemp);
                udidTemp -= 1;
                ctx.udid_string = to_string(udidTemp);
            }
        }
        LOG_I(TAG, "ctx.udid_string is %s", ctx.udid_string.c_str());
        prop_data_t sc_entry;
        ctx.sessionCount_string = "";
        if (get_property_DB("sessionCount", &sc_entry)) {
            ctx.sessionCount_string = sc_entry.value;
        } else {
            if ((set_property_DB("sessionCount", to_string(0))) == false) {
                nd_service_obj->send_err_msg(SM_E_NDC_SET_PROP_DB_FAIL, NDService::UNUSED_ERR_AUX_CODE, "set_property_DB failed" );
                LOG_E(TAG, "set_property_DB failed for sessionCount");
            }
            ctx.sessionCount_string = "0";
        }
        LOG_I(TAG, "ctx.sessionCount_string is %s", ctx.sessionCount_string.c_str());
    }

    if (device_mode_global_partial.partial_privacy_params.save_user_alert_video &&
                    device_mode_global_partial.partial_privacy_params.has_user_alert) {
        need_to_copy = true;
    }

    for (vector<string>::iterator iter = vec.begin(), end = vec.end(); iter != end; iter++) {
        if ((*iter).find("partial.csv") != string::npos) {
            continue;
        }
        /* split the strings by "_" and store the substrings in vec
         * if file name is 0_trip_something, vec[0] will have "0", vec[1]
         * will have "trip" and vec[2] will have "something" */
        vector<string> vec = split_by_delim(*iter, "_");
        string cam_num_str = vec[0];
        int cam_num;
        if (string_to_integer(cam_num_str, cam_num) == false) {
            LOG_I(TAG, "failed to get cam num from filename, sending 0 as default");
            cam_num = DEVICE_CAMERA_POSITION_FRONT;
        }

        int partial_size = file_size(folder_name + "/" + (*iter));

        if (((*iter).find(video_file_extn) == string::npos) &&
            ((*iter).find(ea_extn) == string::npos) &&
            ((*iter).find(META_PARTIAL_FILE_SUFFIX) == string::npos) &&
            ((*iter).find(AUDIO_PARTIAL_FILE_SUFFIX) == string::npos) &&
	        ((*iter).find(audio_file_extn) == string::npos)) {
            // delete any files other than videos
            if (file_is_present(folder_name + "/" + (*iter)) == true) {
                LOG_E(TAG, "deleting unknown file %s with size %d", (*iter).c_str(), partial_size);
                file_delete(folder_name + "/" + (*iter));
            }
            continue;
        }

        // Need to make sure file has valid session format
        if (is_video_audio_file_name_valid((*iter)) == false) {
            LOG_E(TAG, "Invalid partial file name: %s, deleting it", (*iter).c_str());
            file_delete(folder_name + "/" + (*iter));
            continue;
        }

        if (!copy_status_for_cams && (file_is_present(folder_name + "/" + (*iter)) == true)) {
            file_delete(folder_name + "/" + (*iter));

            if ((*iter).find(ea_extn) != std::string::npos) {
                if (ctx.previous_lpw_no_record == true) {
                    LOG_I(TAG, "Deleting EA image file %s of partial session because previous session was low_power_no_record", (*iter).c_str());
                    post_uploader_add_ea_file_db((*iter), DEVICE_CAMERA_POSITION_FRONT, LPW_NO_CAPTURE);
                } else {
                    LOG_I(TAG, "Deleting EA image file %s of partial session because previous session was full privacy", (*iter).c_str());
                    post_uploader_add_ea_file_db((*iter), DEVICE_CAMERA_POSITION_FRONT, RECORD_PRIVACY);
                }
            } else {
                post_circular_buffer_add_file_db((*iter), CIRCULAR_BUFFER_TYPE_NORMAL, cam_num);
            }
            continue;
        }

        if ((*iter).find(AUDIO_PARTIAL_FILE_SUFFIX) != string::npos) {
            if (partial_size == 0) {
                LOG_I(TAG, "Audio file (.pcm) is of 0 size, deleting: %s", (folder_name + "/" + (*iter)).c_str());
                file_delete(folder_name + "/" + (*iter));
                nd_service_obj->send_err_msg(SM_E_NDC_AUDIO_RECORD_FAILED, NDService::UNUSED_ERR_AUX_CODE,
                    std::string("Audio pcm file has 0 size: ") + (*iter));
                continue;
            }
            LOG_I(TAG, "moving %s to SdCard folder with size %d", (*iter).c_str(), partial_size);
            string audio_partial_encoded_file = (*iter).substr(0, (*iter).find(AUDIO_PARTIAL_FILE_SUFFIX)) + ".aac";
            bool record_privacy = false;
            check_partial_session_audio_privacy(record_privacy);
            if (record_privacy) {
                file_delete(folder_name + "/" + (*iter));
                LOG_I(TAG, "delete file: %s, Privacy mode: %d, Enhanced Privacy: %d", (*iter).c_str(),
                            device_mode_global_partial.privacy_status, device_mode_global_partial.partial_privacy_params.enhanced_privacy);
                post_circular_buffer_add_file_db(audio_partial_encoded_file, CIRCULAR_BUFFER_TYPE_TEXT, 0);
                continue;
            }
            task_result_t timed_task_result;
            timed_task_result = nd_timed_task(audio_partial_encode_task, AUDIO_ENCODE_TIME_MAX, (void*)(*iter).c_str(), "audio_partial_encode_task");
            if (timed_task_result != TASK_SUCCESS) {
                LOG_E (TAG, "nd_timed_task for audio_encode failed");
            }
            file_delete(folder_name + "/" + (*iter));
            string source_path = AUDIO_PARTIAL_PCM_FILE_BASE_PATH + "/" + audio_partial_encoded_file ;
            string destination_path = CIRCULAR_BUFFER_PATH + "/" + audio_partial_encoded_file ;
            string filename_prefix = (*iter);
            if (GetFileSize(source_path.c_str()) == 0) {
                LOG_E(TAG, "Audio file (.aac) is of 0 size, deleting: %s", source_path.c_str());
                file_delete(source_path.c_str());
                nd_service_obj->send_err_msg(SM_E_NDC_AUDIO_ENCODE_FAILED, NDService::UNUSED_ERR_AUX_CODE,
                    std::string("Audio aac file has 0 size: ") + (*iter));
                continue;
            }
            LOG_I(TAG, "move_partial_files() dest: %s", destination_path.c_str() );
            if (audio_encryption) {
                if (ND_AUTH_SUCCESS != nd_file_operate_to_file(source_path.c_str(), destination_path.c_str())) {
                    LOG_E(TAG, "Audio File operate failed.");
                } else {
                    file_delete(source_path);
                }
            } else {
                if (!file_copy(source_path, destination_path, true, 0644, false)) {
                    LOG_E(TAG, "Audio partial file_copy failed");
                } else {
                    file_delete(source_path);
                }
            }

            //sync after move
            if (file_fd_sync(destination_path) == false) {
                LOG_E(TAG, "failed to sync file after move");
            }
            int file_type = CIRCULAR_BUFFER_TYPE_NORMAL;
            if (destination_path.find(audio_file_extn) != string::npos) {
                file_type = CIRCULAR_BUFFER_TYPE_TEXT;
            }
            post_circular_buffer_add_file_db( audio_partial_encoded_file, file_type, 0);
            continue;
        } else {
            hs_starttime = get_system_time();

            /* Check for already existing zero sized .aac files */
            if ((*iter).find(audio_file_extn) != std::string::npos) {
                if (partial_size == 0) {
                    LOG_I(TAG, "Audio file (.aac) is of 0 size, deleting: %s", (folder_name + "/" + (*iter)).c_str());
                    file_delete(folder_name + "/" + (*iter));
                    continue;
                }
            }

            /* Move inward cam video files */
            if ((*iter).find("1_trip") != std::string::npos) {
                LOG_I(TAG, "moving %s to SdCard folder with size %d", (*iter).c_str(), partial_size);
                if (is_bit_set(edit_status_for_cams, DEVICE_CAMERA_POSITION_BACK) &&
                           ((*iter).find(partial_session_name) != std::string::npos)) {
                    LOG_I(TAG, "Applying partial privacy to partial video: %s, len %d", (*iter).c_str(), partial_video_len_sec);
                    bool ld_file = false;
                    if ((*iter).find(ld_extn) != std::string::npos) {
                        ld_file = true;
                    }
                    LOG_I(TAG, "Applying partial privacy to partial video: %s, len %d", (*iter).c_str(), partial_video_len_sec);
                    copied = apply_partial_privacy_and_save_video(
                                folder_name + "/" + (*iter), /* src_file */
                                CIRCULAR_BUFFER_PATH, /* dest_path */
                                device_mode_global_partial, /* device_mode */
                                DEVICE_CAMERA_POSITION_BACK, /* cam_num */
                                ld_file, /* ldFile */
                                partial_video_len_sec /* video_length_sec */);

                    file_delete(folder_name + "/" + (*iter));
                } else if (!is_bit_set(copy_status_for_cams, DEVICE_CAMERA_POSITION_BACK) && ((*iter).find(partial_session_name) != std::string::npos)) {
                    file_delete(folder_name + "/" + (*iter));
                    post_circular_buffer_add_file_db((*iter), CIRCULAR_BUFFER_TYPE_NORMAL, cam_num);
                    continue;
                }

                if (!copied) {
                    copied = move_files((folder_name + "/" + (*iter)), CIRCULAR_BUFFER_PATH);
                }

                if (copied == false) {
                    LOG_E(TAG, "failed to move partial file %s to circular buffer folder", (*iter).c_str());
                    nd_service_obj->send_err_msg(SM_E_NDC_PARTIAL_FILE_COPY_FAIL, NDService::UNUSED_ERR_AUX_CODE, ("Error copying file for partial session " + (*iter)));
                    // delete src file when failed to move
                    if (file_is_present(folder_name + "/" + (*iter)) == true) {
                        file_delete(folder_name + "/" + (*iter));
                    }
                    continue;
                }
            }

            // request for ext_cam files if outward non-ld file encountered
            // Handling ext cams file before outward/side cam files
            bool ext_camera_feature_flag= false;
            read_ext_camera_common_config(ext_camera_feature_flag);
            if ((cam_num == DEVICE_CAMERA_POSITION_FRONT) && (ext_camera_feature_flag) &&
                (((*iter).find(ld_extn.c_str(), 0) == string::npos) && ((*iter).find(dp_extn.c_str(), 0) == string::npos) &&
                ((*iter).find(ea_extn.c_str(), 0) == string::npos))) {

                if (ctx.previous_lpw_no_record) {
                    LOG_I(TAG, "previous boot cycle was lpw no record case, not requesting files from ext cam");
                    continue;
                }
                LOG_I(TAG, "send_msg_ext_cam_partial_files() : %s ", (*iter).c_str());
                send_msg_ext_cam_partial_files(folder_name, *iter, need_to_copy);
            }

            /* Move outward and side cam video files*/
            if (((*iter).find("0_trip") != std::string::npos) ||
                ((*iter).find("2_trip") != std::string::npos) ||
                ((*iter).find("3_trip") != std::string::npos)) {
                LOG_I(TAG, "moving %s to SdCard folder with size %d", (*iter).c_str(), partial_size);
                int cam_num_temp = 0;
                if (((*iter).find("0_trip") != std::string::npos))
                    cam_num_temp = DEVICE_CAMERA_POSITION_FRONT;
                else if (((*iter).find("2_trip") != std::string::npos))
                    cam_num_temp = DEVICE_CAMERA_POSITION_LEFT;
                else
                    cam_num_temp = DEVICE_CAMERA_POSITION_RIGHT;

                if (is_bit_set(edit_status_for_cams, cam_num_temp) && ((*iter).find(partial_session_name) != std::string::npos)) {
                    //apply partial privacy (blackout) to the video.
                    LOG_I(TAG, "Applying partial privacy to partial video: %s, len %d", (*iter).c_str(), partial_video_len_sec);

                    if ((*iter).find("0_trip") != std::string::npos) {
                        if (((*iter).find(dp_extn) == std::string::npos) && ((*iter).find(ea_extn) == std::string::npos)) {
                            bool ld_file = false;
                            if ((*iter).find(ld_extn) != std::string::npos) {
                                ld_file = true;
                            }
                            copied = apply_partial_privacy_and_save_video(
                                        folder_name + "/" + (*iter), /* src_file */
                                        CIRCULAR_BUFFER_PATH, /* dest_path */
                                        device_mode_global_partial, /* device_mode */
                                        DEVICE_CAMERA_POSITION_FRONT, /* cam_num */
                                        ld_file, /* ldFile */
                                        partial_video_len_sec /* video_length_sec */);
                        } else {
                            if ((*iter).find(dp_extn) != std::string::npos) {
                                LOG_I(TAG, "Not applying partial privacy to DP partial video: %s", (*iter).c_str());
                                /* Since the framerate of DP file is configurable and for blacking out
                                 * of the frames in case of mixed privacy, we need a blackoutvideo with
                                 * predefined framerate which is not possible in case of DP video,
                                 * we are going to delete the DP file in case of mixed privacy.
                                 */
                                file_delete(folder_name + "/" + (*iter)); // Delete partial DP file in case of mixed privacy
                                continue;
                            } else if ((*iter).find(ea_extn) != std::string::npos) {
                                LOG_I(TAG, "Deleting EA image file %s of partial session because of PARTIAL_PRIVACY", (*iter).c_str());
                                file_delete(folder_name + "/" + (*iter)); // Delete EA image file in case of mixed privacy
                                post_uploader_add_ea_file_db((*iter), DEVICE_CAMERA_POSITION_FRONT, PARTIAL_PRIVACY);
                                continue;
                            }
                        }
                    } else {
                        if((*iter).find("2_trip") != std::string::npos) {
                            copied = apply_partial_privacy_and_save_video(
                                    folder_name + "/" + (*iter), /* src_file */
                                    CIRCULAR_BUFFER_PATH, /* dest_path */
                                    device_mode_global_partial, /* device_mode */
                                    DEVICE_CAMERA_POSITION_LEFT, /* cam_num */
                                    false, /* ldFile */
                                    partial_video_len_sec /* video_length_sec */);
                        }
                        if((*iter).find("3_trip") != std::string::npos) {
                            copied = apply_partial_privacy_and_save_video(
                                    folder_name + "/" + (*iter), /* src_file */
                                    CIRCULAR_BUFFER_PATH, /* dest_path */
                                    device_mode_global_partial, /* device_mode */
                                    DEVICE_CAMERA_POSITION_RIGHT, /* cam_num */
                                    false, /* ldFile */
                                    partial_video_len_sec /* video_length_sec */);
                        }
                    }
                    file_delete(folder_name + "/" + (*iter));
                } else if (!is_bit_set(copy_status_for_cams, cam_num_temp) && ((*iter).find(partial_session_name) != std::string::npos)) {
                    file_delete(folder_name + "/" + (*iter));

                    if ((*iter).find(ea_extn) != std::string::npos) {
                        LOG_I(TAG, "Deleting EA image file %s of partial session because of RECORD_PRIVACY", (*iter).c_str());
                        post_uploader_add_ea_file_db((*iter), DEVICE_CAMERA_POSITION_FRONT, RECORD_PRIVACY);
                    } else {
                        post_circular_buffer_add_file_db((*iter), CIRCULAR_BUFFER_TYPE_NORMAL, cam_num);
                    }
                    continue;
                }

                if (!copied) {
                    if ((*iter).find(ea_extn) == std::string::npos) {
                        copied = move_files((folder_name + "/" + (*iter)), CIRCULAR_BUFFER_PATH);
                    } else {
                        bool should_upload_vid = device_mode_global_partial.partial_privacy_params.upload_video[DEVICE_CAMERA_POSITION_FRONT];
                        if ((device_mode_global_partial.privacy_status == PRIVACY_ON) && (need_to_copy == false) && (should_upload_vid == false)) {
                            LOG_I(TAG, "Deleting EA image file %s of partial session because of UPLOAD_PRIVACY", (*iter).c_str());
                            file_delete(folder_name + "/" + (*iter));
                            post_uploader_add_ea_file_db((*iter), DEVICE_CAMERA_POSITION_FRONT, UPLOAD_PRIVACY);
                        } else {
                            LOG_I(TAG, "Copying EA image file %s of partial session to sdcard", (*iter).c_str());
                            copied = move_files((folder_name + "/" + (*iter)), CIRCULAR_BUFFER_PATH_EA);
                            if (copied == true) {
                                // Get the status of corresponding partial video file
                                string video_fname = (*iter);
                                size_t pos = video_fname.find(ea_extn);
                                if (pos != string::npos) {
                                    video_fname.replace(pos, ea_extn.length(), ".mp4");
                                }
                                if ((GetFileSize(folder_name + "/" + video_fname) > 0) || (GetFileSize(CIRCULAR_BUFFER_PATH + "/" + video_fname) > 0)) {
                                    LOG_I(TAG, "Posting the availability of EA image file %s of partial session to uploader", (*iter).c_str());
                                    post_uploader_add_ea_file_db((*iter), DEVICE_CAMERA_POSITION_FRONT, NO_PRIVACY);
                                } else {
                                    LOG_I(TAG, "Avoid posting the availability of EA image file %s of partial session to uploader because the corresponding video is either not available or is of 0 size", (*iter).c_str());
                                    if (file_is_present(CIRCULAR_BUFFER_PATH_EA + "/" + (*iter)) == true) {
                                        LOG_I(TAG, "Deleting EA image file %s of partial session from the sdcard", (*iter).c_str());
                                        file_delete(CIRCULAR_BUFFER_PATH_EA + "/" + (*iter));
                                    }
                                    nd_service_obj->send_err_msg(SM_E_NDC_PARTIAL_EA_IMG_WITH_NO_VIDEO, NDService::UNUSED_ERR_AUX_CODE, ("Partial EA image available but corresponding video not available " + (*iter)));
                                }
                            } else {
                                LOG_I(TAG, "EA image file %s of partial session is of 0 size, hence not posting to uploader", (*iter).c_str());

                                // Get the status of corresponding partial video file
                                string video_fname = (*iter);
                                size_t pos = video_fname.find(ea_extn);
                                if (pos != string::npos) {
                                    video_fname.replace(pos, ea_extn.length(), ".mp4");
                                }
                                if ((GetFileSize(folder_name + "/" + video_fname) > 0) || (GetFileSize(CIRCULAR_BUFFER_PATH + "/" + video_fname) > 0)) {
                                    nd_service_obj->send_err_msg(SM_E_NDC_PARTIAL_VIDEO_WITH_NO_EA_IMG, NDService::UNUSED_ERR_AUX_CODE, ("Partial EA image not available but corresponding video available " + (*iter)));
                                }
                            }
                        }
                    }
                }

                if (copied == false) {
                    LOG_E(TAG, "failed to move partial file %s to circular buffer folder", (*iter).c_str());
                    nd_service_obj->send_err_msg(SM_E_NDC_PARTIAL_FILE_COPY_FAIL, NDService::UNUSED_ERR_AUX_CODE, ("Error copying file for partial session " + (*iter)));
                    // delete src file when failed to move
                    if (file_is_present(folder_name + "/" + (*iter)) == true) {
                        file_delete(folder_name + "/" + (*iter));
                    }
                    continue;
                }
            }
        }

        hs_endtime = get_system_time();

        int file_type = CIRCULAR_BUFFER_TYPE_NORMAL;
        if ((*iter).find(audio_file_extn) != string::npos) {
            file_type = CIRCULAR_BUFFER_TYPE_TEXT;
        }

        if (cam_num < DEVICE_CAMERA_POSITION_MAX) {
            if ((*iter).find(ea_extn) == std::string::npos) {
                post_circular_buffer_add_file_db((*iter), file_type, cam_num);
                fill_map_add_file_healthstats( (folder_name + "/" + (*iter)), CIRCULAR_BUFFER_PATH, cam_num, hs_starttime, hs_endtime, copied, hs_reason, 0);
            }
        }

        copied = false;
    }
    if (csv_file != "") {
        file_delete(csv_file);
    }
}
#endif

void move_other_cam_partial_files(const char *fname, int cam_num)
{
    string hs_reason = "partial";
    int64_t hs_starttime = -1, hs_endtime = -1;

    bool copied = false;
    /* The below variable added for, if there is any user alert in a partial session file,
     * it should check that and copy the file even if the camera is in any privacy state */
    bool need_to_copy = false;
    int temp_cam_num = cam_num;
    string folder_name = "/home/iriscli/files/";

    LOG_I(TAG, "moving %s to SdCard folder", fname);

    string filename = fname;

    hs_starttime = get_system_time();
    /* This function will be called if cam_rec restarts in between because of any reason
     * In that case we need to get the device mode for existing session and not partial
     * and also we need to take care of LD files also if the cam_num is 1 along with HD */
    device_mode_t device_mode_global;

    memset((void *)&device_mode_global, 0, sizeof(device_mode_global));

    if (!get_device_mode_for_fname (filename, device_mode_global)) {
        LOG_E (TAG,"Failed to get device_mode for %s",filename.c_str());
    }

    int copy_status_for_cams = device_mode_global.session_status.copy_status_for_cams;
    int edit_status_for_cams = device_mode_global.session_status.edit_status_for_cams;
    int remove_status_for_cams = device_mode_global.session_status.remove_status_for_cams;

    if (is_bit_set(edit_status_for_cams, cam_num)) {
        LOG_I(TAG, "Applying partial privacy to partial video: %s", filename.c_str());
        copied = apply_partial_privacy_and_save_video(
                    folder_name + "/" + filename, /* src_file */
                    CIRCULAR_BUFFER_PATH, /* dest_path */
                    device_mode_global, /* device_mode */
                    cam_num, /* cam_num */
                    false, /* ldFile */
                    UNKNOWN_VIDEO_DURATION /* video_length_sec */);
        post_circular_buffer_add_file_db(filename, CIRCULAR_BUFFER_TYPE_NORMAL, cam_num);
        if ((cam_num == DEVICE_CAMERA_POSITION_BACK) || (cam_num == DEVICE_CAMERA_POSITION_FRONT) || (cam_num == DEVICE_CAMERA_POSITION_DMS)) {
            LOG_I(TAG, "Applying partial privacy to partial video: %s", (filename + ld_extn).c_str());
            copied = apply_partial_privacy_and_save_video(
                        folder_name + "/" + filename + ld_extn, /* src_file */
                        CIRCULAR_BUFFER_PATH, /* dest_path */
                        device_mode_global, /* device_mode */
                        cam_num, /* cam_num */
                        true, /* ldFile */
                        UNKNOWN_VIDEO_DURATION /* video_length_sec */);
            post_circular_buffer_add_file_db(filename + ld_extn, CIRCULAR_BUFFER_TYPE_NORMAL, cam_num);
        }
    } else if (is_bit_set(copy_status_for_cams, cam_num)) {
        LOG_I(TAG, "Copying partial video file: %s", filename.c_str());
        copied = move_files((folder_name + "/" + filename), CIRCULAR_BUFFER_PATH);
        post_circular_buffer_add_file_db(filename, CIRCULAR_BUFFER_TYPE_NORMAL, cam_num);
        if ((cam_num == DEVICE_CAMERA_POSITION_BACK) || (cam_num == DEVICE_CAMERA_POSITION_FRONT) || (cam_num == DEVICE_CAMERA_POSITION_DMS)) {
            LOG_I(TAG, "Copying partial LD video file: %s", (filename + ld_extn).c_str());
            copied = move_files((folder_name + "/" + filename + ld_extn), CIRCULAR_BUFFER_PATH);
            post_circular_buffer_add_file_db(filename + ld_extn, CIRCULAR_BUFFER_TYPE_NORMAL, cam_num);
        }
    } else if(is_bit_set(remove_status_for_cams, cam_num)) {
        LOG_I(TAG, "Deleting partial video file: %s", filename.c_str());
        if (file_is_present(folder_name + "/" + filename) == true) {
            file_delete(folder_name + "/" + filename);
        }
        post_circular_buffer_add_file_db(filename, CIRCULAR_BUFFER_TYPE_NORMAL, cam_num);
        if ((cam_num == DEVICE_CAMERA_POSITION_BACK) || (cam_num == DEVICE_CAMERA_POSITION_FRONT) || (cam_num == DEVICE_CAMERA_POSITION_DMS)) {
            LOG_I(TAG, "Deleting partial LD video file: %s", (filename + ld_extn).c_str());
            if (file_is_present(folder_name + "/" + filename + ld_extn) == true) {
                file_delete(folder_name + "/" + filename + ld_extn);
            }
            post_circular_buffer_add_file_db(filename + ld_extn, CIRCULAR_BUFFER_TYPE_NORMAL, cam_num);
        }
    }

    if (!copied && (file_is_present(folder_name + "/" + filename) == true)) {
        copied = move_files((folder_name + filename), CIRCULAR_BUFFER_PATH);
        if ((cam_num == DEVICE_CAMERA_POSITION_BACK) || (cam_num == DEVICE_CAMERA_POSITION_DMS))
            copied = move_files((folder_name + filename + ld_extn), CIRCULAR_BUFFER_PATH);
    } else {
        file_delete(folder_name + "/" + filename);
        if ((cam_num == DEVICE_CAMERA_POSITION_BACK) || (cam_num == DEVICE_CAMERA_POSITION_DMS))
            file_delete(folder_name + "/" + filename + ld_extn);
        return;
    }
    hs_endtime = get_system_time();
    if (copied == false) {
        LOG_E(TAG, "failed to move partial file %s to circular buffer folder", filename.c_str());
        nd_service_obj->send_err_msg(SM_E_NDC_PARTIAL_FILE_COPY_FAIL, NDService::UNUSED_ERR_AUX_CODE, ("Error copying file for partial session " + (partial_session_name)));
        // delete src file when failed to move
        if (file_is_present(folder_name + "/" + filename) == true) {
            file_delete(folder_name + "/" + filename);
            if ((cam_num == DEVICE_CAMERA_POSITION_BACK) || (cam_num == DEVICE_CAMERA_POSITION_DMS))
                file_delete(folder_name + "/" + filename + ld_extn);
        }
        return;
    }
    int file_type = CIRCULAR_BUFFER_TYPE_NORMAL;
    post_circular_buffer_add_file_db(filename, file_type, cam_num);
    fill_map_add_file_healthstats( (folder_name + "/" + filename), CIRCULAR_BUFFER_PATH, cam_num, hs_starttime, hs_endtime, copied, hs_reason, 0);
    if ((cam_num == DEVICE_CAMERA_POSITION_BACK) || (cam_num == DEVICE_CAMERA_POSITION_DMS)) {
        post_circular_buffer_add_file_db(filename + ld_extn, file_type, cam_num);
        fill_map_add_file_healthstats( (folder_name + "/" + filename + ld_extn), CIRCULAR_BUFFER_PATH, cam_num, hs_starttime, hs_endtime, copied, hs_reason, 0);
    }
}

int64_t parse_session_start_timestamp(std::string session_filename, std::string delimiter) {
    size_t pos = 0;
    int count = 0;
    int64_t return_val = 0;
    std::string token;
    while ((pos = session_filename.find(delimiter)) != std::string::npos) {
        count++;
        token = session_filename.substr(0, pos);
        if(count == 7) { // timestamp
            //std::cout << count << " " << token << std::endl;
            std::istringstream(token) >> return_val;
            return return_val;
        }
        session_filename.erase(0, pos + delimiter.length());
    }
    //std::cout << session_filename << std::endl;
}

bool add_device_fields_lla(string lla_message, string &lla_message_final) {
    //parse json and add info needed here
    json_t *root = NULL;
    json_error_t error;
    char *res_json = NULL;
    root = json_loads(lla_message.c_str(), 0, &error);
    if(root == NULL){
        LOG_E(TAG,"json_loads failed in add_device_fields_lla");
        LOG_E(TAG,"error: on line %d: %s", error.line, error.text);
        return false;
    }

    json_t *videoName_t = json_object_get(root, "videoName");
    if( videoName_t == NULL ) {
        LOG_E(TAG, "videoName section not found in lla_message");
        json_decref(root);
        return false;
    }

    string vdeo_name_full = json_string_value(videoName_t);
    string vdeo_name;
    if(!remove_extension_from_session(vdeo_name_full, vdeo_name)){
        LOG_E(TAG, "file name doesn't contain any . : %s",vdeo_name_full.c_str());
        json_decref(root);
        return false;
    }
    int i = 0;
    bool found_file = false;
    pthread_mutex_lock(&file_start_time_vec_mutex);
    for(i = 0; i < file_start_time_vec_pair.size(); i++)
    {
        if(file_start_time_vec_pair[i].first == vdeo_name ) {
            found_file = true;
            break;
        }
    }

    int64_t start_time;
    if(found_file == true) {
        start_time = file_start_time_vec_pair[i].second;
    } else {
        /* Condition where starttime has not updated, parse start time from filename */
        start_time = parse_session_start_timestamp(vdeo_name_full, "_");
        LOG_I(TAG, "Start time is not available hence taking start time from file name : %s, start time is = %lld",vdeo_name_full.c_str(), start_time);
    }
    pthread_mutex_unlock(&file_start_time_vec_mutex);

    if(start_time == 0) {
        LOG_E(TAG, "start time 0 for %s",vdeo_name.c_str());
        json_decref(root);
        return false;
    }

    // edit analytics payload to add .mp4 and reletive times instead absolute
    json_t *inference_data = json_object_get(root, "inference_data");
    if( inference_data == NULL ) {
        LOG_E(TAG, "inference_data section not found in lla_message");
        json_decref(root);
        return false;
    }
    json_t *alerts_data = json_object_get(inference_data, "alerts_data");
    if( alerts_data == NULL ) {
        LOG_E(TAG, "alerts_data section not found in lla_message");
        json_decref(root);
        return false;
    }
    json_t *alerts = json_object_get(alerts_data, "alerts");
    if( alerts == NULL ) {
        LOG_E(TAG, "alerts section not found in lla_message");
        json_decref(root);
        return false;
    }

    int alerts_len = json_array_size(alerts);
    if(alerts_len != 1) {
        LOG_E(TAG, "alerts_len is %d not 1 dropping this LLA message", alerts_len );
        json_decref(root);
        return false;
    }

    json_t *alerts_0 = json_array_get(alerts, 0);
    json_t *start_time_alert = json_object_get(alerts_0, "start_timestamp");
    if( start_time_alert == NULL ) {
        LOG_E(TAG, "start_timestamp section not found in lla_message");
        json_decref(root);
        return false;
    }
    uint64_t start_ts = json_integer_value(start_time_alert);

    json_t *end_time_alert = json_object_get(alerts_0, "end_timestamp");
    if( end_time_alert == NULL ) {
        LOG_E(TAG, "end_timestamp section not found in lla_message");
        json_decref(root);
        return false;
    }
    uint64_t end_ts = json_integer_value(end_time_alert);

    start_ts -= start_time;
    end_ts -= start_time;
    json_object_set_new( alerts_0, "start_timestamp", json_integer(start_ts));
    json_object_set_new( alerts_0, "end_timestamp", json_integer(end_ts));

    if(vdeo_name_full.find(".mp4") == std::string::npos) {
        vdeo_name_full = vdeo_name_full + ".mp4";
        json_object_set_new( root, "videoName", json_string(vdeo_name_full.c_str()));
    }
    // END of edit analytics payload

    // read headers
    string device_id = ctx.device_config->getConfig("identity","deviceId","");
    string devicetype = ctx.device_config->getConfig("identity","deviceType","");
    string observation_type = "LLA";

    json_object_set_new( root, "Time", json_string(get_date().c_str()));
    json_object_set_new( root, "deviceId", json_string(device_id.c_str()));
    json_object_set_new( root, "observation_type", json_string(observation_type.c_str()));
    json_object_set_new( root, "deviceType", json_string(devicetype.c_str()));
    json_object_set_new( root, "startTime", json_integer(start_time));

    char* lla_message_final_ptr = json_dumps(root, 0);
    if(lla_message_final_ptr == NULL){
        LOG_E(TAG,"JSON creation failed for add_device_fields_lla message");
        json_decref(root);
        return false;
    }
    lla_message_final = lla_message_final_ptr;

    free(lla_message_final_ptr);
    json_decref(root);

    return true;
}

void *copy_or_move_files_thread(void *args) {

    nd_msgq_t::nd_msg_t *msg;

    while (1) {
        if ((msg = ctx.cmf_msg_q->receive( )) == NULL) {
            continue;
        }

        ndc_generic_msg_t *g_msg = (ndc_generic_msg_t *)msg->get_buffer();
        if (NULL == g_msg) {
            LOG_E(TAG, "msg->get_buffer() returned NULL");
            continue;
        }

        switch (g_msg->type) {
            case COPY_OR_MOVE_FILE:
                {
                    ndc_copy_or_move_file_msg_t *cmf_msg = (ndc_copy_or_move_file_msg_t *)msg->get_buffer();
                    if (cmf_msg->len != sizeof(ndc_copy_or_move_file_msg_t)) {
                        LOG_E(TAG, "cmf_msg->len != sizeof(ndc_copy_or_move_file_msg_t)");
                        break;
                    }
                    copy_or_move_files(cmf_msg->fname, cmf_msg->cam_num, cmf_msg->flipflop, cmf_msg->epoch_time, cmf_msg->raw_time, cmf_msg->pts_time, cmf_msg->is_ld);
                    if (cmf_msg->fname)
                        free(cmf_msg->fname);
                    break;
                }

            case MOVE_PARTIAL_FILES:
                {
                    ndc_move_partial_files_t *mpf_msg = (ndc_move_partial_files_t *)msg->get_buffer();
                    if (mpf_msg->len != sizeof(ndc_move_partial_files_t)) {
                        LOG_E(TAG, "mpf_msg->len != sizeof(ndc_move_partial_files_t)");
                        break;
                    }
                    // Move a single file to circular buffer folder
                    LOG_I(TAG, "start: Move partial file %s to sdcard", mpf_msg->file_name);
                    vector<string> single_file_vec = {mpf_msg->file_name};
                    move_partial_files(mpf_msg->folder_name, single_file_vec);
                    LOG_I(TAG, "end: Move partial file %s to sdcard", mpf_msg->file_name);
                    break;
                }

            default:
                LOG_I(TAG, "Unknown message %d received", g_msg->type);
                break;
        }
        delete msg;
    }
    return NULL;
}

void* qr_scan_data_fetcher_thread_func(void* arg)
{
    const char* TAG = "qr_scan_data_fetcher";

    prctl(PR_SET_NAME, "qr_data_fetcher", 0, 0, 0);
    LOG_I(TAG, "QR scan data fetcher thread started");

    qr_scan_data_fetcher_thread_running = true;

    while (qr_scan_data_fetcher_thread_running) {
        try {
            // Subscribe and wait for QR scan data message
            string qr_message = qr_scan_data_fetcher.subscribe();

            if (qr_message.empty()) {
                LOG_D(TAG, "Received empty QR message; timeout or connection issue");
                continue;
            }

            LOG_D(TAG, "Received QR scan data: %s", qr_message.c_str());

            // Parse the received JSON message
            json_error_t error;
            json_t *root = json_loads(qr_message.c_str(), 0, &error);

            if (!root) {
                LOG_E(TAG, "Failed to parse JSON message: %s", error.text);
                continue;
            }

            // Extract decoded_data from JSON
            json_t *decoded_data = json_object_get(root, "decoded_data");
            if (!decoded_data || !json_is_object(decoded_data)) {
                LOG_E(TAG, "Invalid JSON format: missing or invalid 'decoded_data' object");
                json_decref(root);
                continue;
            }

            // Extract num_qr_codes and status
            json_t *num_qr_codes_json = json_object_get(root, "num_qr_codes");
            json_t *status_json = json_object_get(root, "status");
            json_t *frame_epoch_json = json_object_get(root, "qr_frame_epoch");

            int num_qr_codes = 0;
            uint64_t frame_epoch = 0;
            int qrscan_status = 0;

            if (num_qr_codes_json && json_is_integer(num_qr_codes_json)) {
                num_qr_codes = json_integer_value(num_qr_codes_json);
            }

            if (frame_epoch_json && json_is_integer(frame_epoch_json)) {
                frame_epoch = json_integer_value(frame_epoch_json);
            }

            if (status_json && json_is_integer(status_json)) {
                qrscan_status = json_integer_value(status_json);
            }

            // Create the map in the format expected by qr_scan_callback
            unordered_map<string, vector<string>> qr_scan_out;

            // Extract all tag arrays from decoded_data and pass them directly
            // The qr_scan_callback will handle filtering based on saved_qr_scan_tags
            const char *key;
            json_t *value;
            json_object_foreach(decoded_data, key, value) {
                if (json_is_array(value)) {
                    vector<string> tag_strings;
                    size_t array_size = json_array_size(value);

                    for (size_t i = 0; i < array_size; i++) {
                        json_t *tag_json = json_array_get(value, i);
                        if (json_is_string(tag_json)) {
                            const char *tag_str = json_string_value(tag_json);
                            tag_strings.push_back(string(tag_str));
                        }
                    }

                    qr_scan_out[string(key)] = tag_strings;
                }
            }

            // Call the callback with the data (even if empty - valid scenario)
            qr_scan_callback(qr_scan_out, num_qr_codes, frame_epoch, (qr_scan_status)qrscan_status);

            json_decref(root);

        } catch(const std::exception& e) {
            LOG_E(TAG, "Exception in QR data fetcher thread: %s", e.what());
        } catch(...) {
            LOG_E(TAG, "Unknown exception in QR data fetcher thread");
        }

        // Small delay to prevent busy waiting
        usleep(100000); // 100ms
    }

    LOG_I(TAG, "QR scan data fetcher thread stopped");
    return NULL;
}

void *lla_main(void *args) {

    Config_parser sock_addr_config(ND_SOCKET_INI);
    if( sock_addr_config.getParseStatus() != true) {
        LOG_E(TAG, "Cannot allocate ND_CONFIG_ANALYTICS");
        return NULL;
    }

    bool get_override_val = true;
    bool is_val_overridden = false;
    string messenger_addr_ndc_lla = sock_addr_config.getConfig("messenger_sockets","lla_ndcentral", "",
                                get_override_val, is_val_overridden);
    string messenger_addr_uploader_lla = sock_addr_config.getConfig("messenger_sockets","lla_uploader", "",
                                get_override_val, is_val_overridden);

    string messenger_topic_ndc_lla = sock_addr_config.getConfig("messenger_topics","lla_ndcentral", "",
                                get_override_val, is_val_overridden);
    string messenger_topic_uploader_lla = sock_addr_config.getConfig("messenger_topics","lla_uploader", "",
                                get_override_val, is_val_overridden);

    LOG_I(TAG, "%s, %s, %s, %s", messenger_addr_ndc_lla.c_str(), messenger_addr_uploader_lla.c_str(),
                 messenger_topic_ndc_lla.c_str(), messenger_topic_uploader_lla.c_str() );

    if(messenger_addr_ndc_lla == "" ||
        messenger_addr_uploader_lla == "" ||
        messenger_topic_ndc_lla == "" ||
        messenger_topic_uploader_lla == "" ) {

        LOG_E(TAG, "failed to get socket params in lla_main");
        return NULL;
    }

    NDMessenger::ClientBuilder lla_analytics_listner;
    lla_analytics_listner.setServer(messenger_addr_ndc_lla);
    lla_analytics_listner.setTopic(messenger_topic_ndc_lla);

    NDMessenger::ServerBuilder lla_uploader_publisher;
    lla_uploader_publisher.setServer(messenger_addr_uploader_lla);
    lla_uploader_publisher.setTopic(messenger_topic_uploader_lla);

    LOG_I(TAG, "blocking for message in lla_main");
    while(1) {

        string lla_message = lla_analytics_listner.subscribe();
        string lla_message_final;

        if(lla_message == "") {
            LOG_I(TAG, "received empty lla_message; must be a timeout message");
            continue;
        }
        LOG_C(TAG, "lla_message :%s:", lla_message.c_str());

        if(!(add_device_fields_lla(lla_message, lla_message_final))) {
            LOG_E(TAG, "false returned from add_device_fields_lla");
            continue;
        }

        LOG_I(TAG, "lla_message_final: :%s:", lla_message_final.c_str());
        lla_uploader_publisher.setMessage(lla_message_final).publish();
        LOG_I(TAG, "shared content to uploader; blocking for next message");
    }

    return NULL;
}

static void read_ext_camera_config() {
    int framerate;
    bool audio_enable;
    //read_ext_camera_common_config(ctx.ext_cam_feature_enabled);

    ctx.ext_cam_feature_enabled = is_ext_cam_feature_enabled();
    if(ctx.ext_cam_feature_enabled == false) {
        return;
    }

    read_ext_cam_ch1_config(ctx.ext_cam_enabled[0], ctx.ext_cam_framerate[0], ctx.ext_cam_audio_enable[0]);
    read_ext_cam_ch2_config(ctx.ext_cam_enabled[1], ctx.ext_cam_framerate[1], ctx.ext_cam_audio_enable[1]);
    read_ext_cam_ch3_config(ctx.ext_cam_enabled[2], ctx.ext_cam_framerate[2], ctx.ext_cam_audio_enable[2]);
    read_ext_cam_ch4_config(ctx.ext_cam_enabled[3], ctx.ext_cam_framerate[3], ctx.ext_cam_audio_enable[3]);
}


void trace_logs_config() {

    Config_parser c(BAGHEERACONFIG_INI);

    if (c.getParseStatus() != true)
    {
        LOG_E (TAG,"Can't parse %s", BAGHEERACONFIG_INI);
        return;
    }

    bool get_override_val = true;
    bool is_val_overridden = false;
    if(c.isPresent("camera", "trace_logs") ) {
        if( "false" == c.getConfig("camera", "trace_logs", "false",
                                get_override_val, is_val_overridden) ) {
            LOG_I (TAG,"trace logs config is not enabled in config file");
            trace_logs_enable = false;
        }
        else {
            LOG_I (TAG,"trace_logs config is enabled");
            trace_logs_enable = true;
            setup_rtcpu_logs_task();
        }
    }
    else {
        LOG_E (TAG,"camera:trace_logs not present in config file");
    }
}

static bool trace_logs_control_tt(void *args)
{

    if(trace_logs_enable == true) {
        LOG_I(TAG,"*************TRACE LOGS RENAME*****************");
        for (int i=0; i< (sizeof (trace_logs_rename)/sizeof(string)); i++)
        {
            execute_cmd(trace_logs_rename[i], "TRACE LOGS RENAME");
        }
        LOG_I(TAG,"*************TRACE LOGS RENAME END*****************");
    }
    else {
        LOG_I(TAG,"*************TRACE LOGS DELETE*****************");
        for (int i=0; i< (sizeof (trace_logs_delete)/sizeof(string)); i++)
        {
            execute_cmd(trace_logs_delete[i], "TRACE LOGS DELETE");
        }
        LOG_I(TAG,"*************TRACE LOGS DELETE END*****************");
    }

    return true;
}


static bool trace_logs_control()
{
    task_result_t task_result = nd_timed_task(trace_logs_control_tt,
                        TRACE_LOG_TIMEOUT, (void *)NULL, "trace_logs");
    if (task_result != TASK_SUCCESS) {
        LOG_E (TAG, "nd_timed_task for trace_logs_control_tt, timedout");
        nd_service_obj->send_err_msg(SM_E_NDC_TRACE_LOG_FAIL, 0, "trace_logs_control timedout");
    }
    LOG_I(TAG, "trace_logs_control_tt return status %d", task_result);
    return true;
}


void nd_vm_configure(){
    bool val_overridden;
    string dirty_writeback_centisecs = vm_default_dirty_writeback_centisecs;
    string dirty_expire_centisecs = vm_default_dirty_expire_centisecs;
    int dirty_writeback_centisecs_int = 0, dirty_expire_centisecs_int = 0;
    int fp_dwb = -1,fp_de = -1, ret = -1;
    LOG_I(TAG, "Entering vm_configure");

    Config_parser c(BAGHEERACONFIG_INI);

    if (c.getParseStatus() != true)
    {
        LOG_E (TAG,"Can't parse %s", BAGHEERACONFIG_INI);
        return;
    }

    if(c.isPresent("vm","enabled")) {
        if( "true" != c.getConfig("vm","enabled", vm_default_enable, true, val_overridden)){
            LOG_I(TAG, "vm configuration is not enabled in bagheera config.");
            LOG_I(TAG, "Exiting vm_configure");
            return;
        }
    }

    if(c.isPresent("vm","dirty_writeback_centisecs")) {
        dirty_writeback_centisecs = c.getConfig("vm","dirty_writeback_centisecs",vm_default_dirty_writeback_centisecs, true, val_overridden);
    }

    if(c.isPresent("vm", "dirty_expire_centisecS")) {
        dirty_expire_centisecs = c.getConfig("vm","dirty_expire_centisecs",vm_default_dirty_expire_centisecs, true, val_overridden);
    }

    if (!string_to_integer(dirty_writeback_centisecs,dirty_writeback_centisecs_int) || (dirty_writeback_centisecs_int < vm_min_dirty_writeback_centisecs)){
        dirty_writeback_centisecs = vm_default_dirty_writeback_centisecs;
    }

    if (!string_to_integer(dirty_expire_centisecs,dirty_expire_centisecs_int) || (dirty_expire_centisecs_int < vm_min_dirty_expire_centisecs)){
        dirty_expire_centisecs = vm_default_dirty_expire_centisecs;
    }


    fp_dwb = open(vm_path_dirty_writeback_centisecs.c_str(),O_WRONLY);
    if (fp_dwb == -1){
        LOG_E(TAG, "Error opening file %s", vm_path_dirty_writeback_centisecs.c_str());
    }else{
        LOG_I(TAG, "Writing %s in %s", dirty_writeback_centisecs.c_str(), vm_path_dirty_writeback_centisecs.c_str());
        ret = write( fp_dwb, dirty_writeback_centisecs.c_str(),dirty_writeback_centisecs.size() );
        if (ret == -1){
            LOG_E(TAG, "Write to %s failed", vm_path_dirty_writeback_centisecs.c_str());
        }
    }
    close(fp_dwb);

    fp_de = open(vm_path_dirty_expire_centisecs.c_str(),O_WRONLY);
    if (fp_de == -1){
        LOG_E(TAG, "Error opening file %s", vm_path_dirty_expire_centisecs.c_str());
    }else{
        LOG_I(TAG, "Writing %s in %s", dirty_expire_centisecs.c_str(), vm_path_dirty_expire_centisecs.c_str());
        ret = write( fp_de, dirty_expire_centisecs.c_str(),dirty_expire_centisecs.size() );
        if (ret == -1){
            LOG_E(TAG, "Write to %s failed", vm_path_dirty_writeback_centisecs.c_str());
        }
    }
    close(fp_de);


    string response  = "";
    string command;
    command = "cat "+vm_path_dirty_writeback_centisecs;
    system_execute_with_resp(TAG, command, response);
    LOG_I(TAG, "%s : %s", command.c_str(),response.c_str());
    response  = "";
    command = "cat "+vm_path_dirty_expire_centisecs;
    system_execute_with_resp(TAG, command, response);
    LOG_I(TAG, "%s : %s", command.c_str(),response.c_str());
    LOG_I(TAG, "Exiting vm_configure");
    return;

}

bool check_live_streaming_feature_enabled() {

    bool get_override_val = true;
    bool is_val_overridden = false;

    Config_parser c(BAGHEERACONFIG_INI);

    if (c.getParseStatus() != true)
    {
        LOG_E (TAG,"Can't parse bagheera config");
        return false;
    }

    if(c.isPresent("live_streaming","enabled") ) {
        if( "true" == c.getConfig("live_streaming","enabled", "false", get_override_val, is_val_overridden) ) {
            LOG_I (TAG,"live streaming feature is enabled");
            live_streaming_enabled = true;
        }
        else {
            LOG_I (TAG,"live_streaming is not enabled in config file");
            live_streaming_enabled = false;
        }
    }
    else {
        LOG_E (TAG,"live_streaming:enabled not present in config file");
        live_streaming_enabled = false;
    }
    return true;
}

/* Thread to check for ignition privacy in 1 sec interval */
void *ignition_privacy_apply(void *args) {

    while(1) {
        if (ignition_on_received && !ctx.off_duty_privacy && ctx.privacy_params.privacy_deactivate_params.ignition_based_privacy) {
            if (get_system_monotonic_time() >= post_ignition_on_target_time) {
                bool old_privacy; privacy_reason_t old_privacy_reason;
                bool ret = read_privacy_value_and_reason_from_file(old_privacy, old_privacy_reason, privacy_state_file_global);
                bool new_privacy = fuse_privacy(SOURCE_IGNITION, ctx.fused_privacy, false);
                if (!ctx.geofence_privacy) {
                    ctx.fused_privacy = new_privacy;
                }
                privacy_reason = REASON_NO_PRIVACY;
                write_privacy_to_file_with_reason(new_privacy, privacy_reason, privacy_state_file_global);

                ignition_on_received = false;
                post_ignition_on_target_time = 0;
                // Reset the flags to avoid re-triggering
                privacy_deactivate_update_received = false;
                privacy_activate_update_received = false;

                if (old_privacy == new_privacy && old_privacy_reason == privacy_reason) {
                    sleep(1);
                    continue;
                } else {
                    string cur_session_fname = currvid_fname;
					if (!ctx.geofence_privacy) {
						if (cur_session_fname != "") {
							int num_files_recording = get_num_digital_cam_files_recording();
							set_device_mode_for_fname(cur_session_fname, new_privacy, ctx.idle_mode_RT_thread, num_files_recording, privacy_reason);
							// LOG_I(TAG, "ctx.fused_privacy %d inward privacy %d", ctx.fused_privacy, ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_BACK]);
							write_privacy_state_file(new_privacy);
						}
						// Only apply LED/audio/camera changes if geofence is not active
						if (ctx.inward_privacy_state != (ctx.fused_privacy && ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_BACK])) {
							set_privacy_mode_led(ctx.fused_privacy && ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_BACK]);
							check_set_irled(REASON_PRIVACY);
							if (ctx.fused_privacy && ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_BACK]) {
								LOG_I(TAG, "Privacy Mode is Activated");
								send_privacy_status_audio_play(REGULAR, true);
							} else {
								LOG_I(TAG, "Privacy Mode is Deactivated");
								send_privacy_status_audio_play(REGULAR, false);
							}
						}
						ctx.inward_privacy_state = ctx.fused_privacy && ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_BACK];

						send_privacy_update(DEVICE_CAMERA_POSITION_BACK, old_privacy, ctx.fused_privacy);
						send_privacy_update(DEVICE_CAMERA_POSITION_FRONT, old_privacy, ctx.fused_privacy);

					}
                }
            }
        }
        if (ignition_off_received && !ctx.off_duty_privacy && ctx.privacy_params.privacy_activate_params.ignition_based_privacy) {
            if (get_system_monotonic_time() >= post_ignition_off_target_time) {
                bool old_privacy; privacy_reason_t old_privacy_reason;
                bool ret = read_privacy_value_and_reason_from_file(old_privacy, old_privacy_reason, privacy_state_file_global);
                bool new_privacy = fuse_privacy(SOURCE_IGNITION, ctx.fused_privacy, true);
                if (!ctx.geofence_privacy) {
                    ctx.fused_privacy = new_privacy;
                }
                privacy_reason = REASON_IGNITION;
                write_privacy_to_file_with_reason(new_privacy, privacy_reason, privacy_state_file_global);

                ignition_off_received = false;
                post_ignition_off_target_time = 0;
                // Reset the flags to avoid re-triggering
                privacy_deactivate_update_received = false;
                privacy_activate_update_received = false;
                if (old_privacy == new_privacy && old_privacy_reason == privacy_reason) {
                    sleep(1);
                    continue;
                } else {
					string cur_session_fname = currvid_fname;
					if (!ctx.geofence_privacy) {
						if (cur_session_fname != "") {
							int num_files_recording = get_num_digital_cam_files_recording();
							set_device_mode_for_fname(cur_session_fname, new_privacy, ctx.idle_mode_RT_thread, num_files_recording, privacy_reason);
							// LOG_I(TAG, "ctx.fused_privacy %d inward privacy %d", ctx.fused_privacy, ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_BACK]);
							write_privacy_state_file(new_privacy);
						}
						// Only apply LED/audio/camera changes if geofence is not active
						if (ctx.inward_privacy_state != (ctx.fused_privacy && ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_BACK])) {
							set_privacy_mode_led(ctx.fused_privacy && ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_BACK]);
							check_set_irled(REASON_PRIVACY);
							if (ctx.fused_privacy && ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_BACK]) {
								LOG_I(TAG, "Privacy Mode is Activated");
								send_privacy_status_audio_play(REGULAR, true);
							} else {
								LOG_I(TAG, "Privacy Mode is Deactivated");
								send_privacy_status_audio_play(REGULAR, false);
							}
						}
						ctx.inward_privacy_state = ctx.fused_privacy && ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_BACK];

						send_privacy_update(DEVICE_CAMERA_POSITION_BACK, old_privacy, ctx.fused_privacy);
						send_privacy_update(DEVICE_CAMERA_POSITION_FRONT, old_privacy, ctx.fused_privacy);
					}

                }
            }
        }
        sleep(1);
    }
}

/* Thread to check for speed privacy in 1 sec interval */
void *speed_privacy_apply(void *args) {

    while(1) {
        if (privacy_activate_update_received && ctx.privacy_params.privacy_activate_params.speed_based_privacy) {
            if (get_system_monotonic_time() >= speed_privacy_activate_target_time) {
                bool old_privacy; privacy_reason_t old_privacy_reason;
                bool ret = read_privacy_value_and_reason_from_file(old_privacy, old_privacy_reason, privacy_state_file_global);
                bool new_privacy = fuse_privacy(SOURCE_SPEED, ctx.fused_privacy, true);
                if (!ctx.off_duty_privacy && !ctx.geofence_privacy) {
                    ctx.fused_privacy = new_privacy;
                }
                privacy_reason = REASON_SPEED;
                write_privacy_to_file_with_reason(new_privacy, privacy_reason, privacy_state_file_global);

                privacy_activate_update_received = false;
                speed_privacy_activate_target_time = 0;
                // Reset the flags to avoid re-triggering
                ignition_on_received = false;
                ignition_off_received = false;

                if (old_privacy == new_privacy && old_privacy_reason == privacy_reason) {
                    sleep(1);
                    continue;
                } else {
                    string cur_session_fname = currvid_fname;
					if (!ctx.off_duty_privacy && !ctx.geofence_privacy) {
						if (cur_session_fname != "") {
							int num_files_recording = get_num_digital_cam_files_recording();
							set_device_mode_for_fname(cur_session_fname, new_privacy, ctx.idle_mode_RT_thread, num_files_recording, privacy_reason);
							// LOG_I(TAG, "ctx.fused_privacy %d inward privacy %d", ctx.fused_privacy, ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_BACK]);
							write_privacy_state_file(new_privacy);
						}
						if (ctx.inward_privacy_state != (ctx.fused_privacy && ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_BACK])) {
							set_privacy_mode_led(ctx.fused_privacy && ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_BACK]);
							check_set_irled(REASON_PRIVACY);
							if (ctx.fused_privacy && ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_BACK]) {
								LOG_I(TAG, "Privacy Mode is Activated");
								send_privacy_status_audio_play(REGULAR, true);
							} else {
								LOG_I(TAG, "Privacy Mode is Deactivated");
								send_privacy_status_audio_play(REGULAR, false);
							}
						}
						if (!ctx.privacy_params.enhanced_privacy && !ctx.off_duty_privacy && !ctx.geofence_privacy)
							ctx.inward_privacy_state = ctx.fused_privacy && ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_BACK];
						else
							ctx.inward_privacy_state = true;

						send_privacy_update(DEVICE_CAMERA_POSITION_BACK, old_privacy, ctx.fused_privacy);
						send_privacy_update(DEVICE_CAMERA_POSITION_FRONT, old_privacy, ctx.fused_privacy);
					}
                }
            }
        }
        if (privacy_deactivate_update_received && ctx.privacy_params.privacy_deactivate_params.speed_based_privacy) {
            if (get_system_monotonic_time() >= speed_privacy_deactivate_target_time) {
                bool old_privacy; privacy_reason_t old_privacy_reason;
                bool ret = read_privacy_value_and_reason_from_file(old_privacy, old_privacy_reason, privacy_state_file_global);
                bool new_privacy = fuse_privacy(SOURCE_SPEED, ctx.fused_privacy, false);
                if (!ctx.off_duty_privacy && !ctx.geofence_privacy) {
                    ctx.fused_privacy = new_privacy;
                }
                privacy_reason = REASON_NO_PRIVACY;
                write_privacy_to_file_with_reason(new_privacy, privacy_reason, privacy_state_file_global);

                privacy_deactivate_update_received = false;
                speed_privacy_deactivate_target_time = 0;
                // Reset the flags to avoid re-triggering
                ignition_on_received = false;
                ignition_off_received = false;

                if (old_privacy == new_privacy && old_privacy_reason == privacy_reason) {
                    sleep(1);
                    continue;
                } else {
                    string cur_session_fname = currvid_fname;
					if (!ctx.off_duty_privacy && !ctx.geofence_privacy) {
						if (cur_session_fname != "") {
							int num_files_recording = get_num_digital_cam_files_recording();
							set_device_mode_for_fname(cur_session_fname, new_privacy, ctx.idle_mode_RT_thread, num_files_recording, privacy_reason);
							// LOG_I(TAG, "ctx.fused_privacy %d inward privacy %d", ctx.fused_privacy, ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_BACK]);
							write_privacy_state_file(new_privacy);
						}
						if (!ctx.off_duty_privacy && !ctx.geofence_privacy && (ctx.inward_privacy_state != (ctx.fused_privacy && ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_BACK]))) {
							set_privacy_mode_led(ctx.fused_privacy && ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_BACK]);
							check_set_irled(REASON_PRIVACY);
							if (ctx.fused_privacy && ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_BACK]) {
								LOG_I(TAG, "Privacy Mode is Activated");
								send_privacy_status_audio_play(REGULAR, true);
							} else {
								LOG_I(TAG, "Privacy Mode is Deactivated");
								send_privacy_status_audio_play(REGULAR, false);
							}
						}
                        if (!ctx.privacy_params.enhanced_privacy && !ctx.off_duty_privacy && !ctx.geofence_privacy)
                            ctx.inward_privacy_state = ctx.fused_privacy && ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_BACK];
                        else
                            ctx.inward_privacy_state = true;

                        send_privacy_update(DEVICE_CAMERA_POSITION_BACK, old_privacy, ctx.fused_privacy);
                        send_privacy_update(DEVICE_CAMERA_POSITION_FRONT, old_privacy, ctx.fused_privacy);
					}
                }
            }
        }
        sleep(1);
    }
}

void init_config_privacy() {

    bool is_val_overridden = false;

    ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_FRONT] = (ctx.bagheera_config->getConfig("privacy_mode",
                                                                                                   "outward", "false", true,
                                                                                                   is_val_overridden) == "true");
    ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_BACK] = (ctx.bagheera_config->getConfig("privacy_mode",
                                                                                                  "inward", "true", true,
                                                                                                  is_val_overridden) == "true");
    ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_LEFT] = (ctx.bagheera_config->getConfig("privacy_mode",
                                                                                                  "left", "false", true,
                                                                                                  is_val_overridden) == "true");
    ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_RIGHT] = (ctx.bagheera_config->getConfig("privacy_mode",
                                                                                                   "right", "false", true,
                                                                                                   is_val_overridden) == "true");
    ctx.privacy_params.ext_cam_privacy = (ctx.bagheera_config->getConfig("privacy_mode", "ext_cam", "false",
                                                                         true, is_val_overridden) == "true");
    ctx.privacy_params.driveri_audio_privacy = (ctx.bagheera_config->getConfig("privacy_mode", "audio", "true",
                                                                               true, is_val_overridden) == "true");
    ctx.privacy_params.ext_cam_audio_privacy = (ctx.bagheera_config->getConfig("privacy_mode", "ext_cam_audio", "true",
                                                                               true, is_val_overridden) == "true");
    ctx.privacy_params.save_user_alert_video = (ctx.bagheera_config->getConfig("privacy_mode", "save_user_alert_video", "false",
                                                                               true, is_val_overridden) == "true");
    ctx.privacy_params.gps_privacy = (ctx.bagheera_config->getConfig("privacy_mode", "gps", "false",
                                                                     true, is_val_overridden) == "true");
    ctx.privacy_params.off_duty_mode = (ctx.bagheera_config->getConfig("privacy_mode", "off_duty_mode", "false",
                                                                       true, is_val_overridden) == "true");
    ctx.privacy_params.inward_led_color = ctx.bagheera_config->getConfig("privacy_mode", "inward_led_color",
                                                                         "red", true, is_val_overridden);
    ctx.privacy_params.default_privacy = (ctx.bagheera_config->getConfig("privacy_mode", "default_privacy_v3", "true",
                                                                         true, is_val_overridden) == "true");
    ctx.privacy_params.enhanced_privacy = (ctx.bagheera_config->getConfig("privacy_mode", "enhanced_privacy", "false",
                                                                          true, is_val_overridden) == "true");

    ctx.privacy_params.upload_video[DEVICE_CAMERA_POSITION_FRONT] = (ctx.bagheera_config->getConfig("upload_video", "outward", "true",
                                                                                                    true, is_val_overridden) == "true");
    ctx.privacy_params.upload_video[DEVICE_CAMERA_POSITION_BACK] = (ctx.bagheera_config->getConfig("upload_video", "inward", "false",
                                                                                                    true, is_val_overridden) == "true");
    ctx.privacy_params.upload_video[DEVICE_CAMERA_POSITION_LEFT] = (ctx.bagheera_config->getConfig("upload_video", "left", "true",
                                                                                                    true, is_val_overridden) == "true");
    ctx.privacy_params.upload_video[DEVICE_CAMERA_POSITION_RIGHT] = (ctx.bagheera_config->getConfig("upload_video", "right", "true",
                                                                                                    true, is_val_overridden) == "true");
    ctx.privacy_params.upload_video_ext_cam = (ctx.bagheera_config->getConfig("upload_video", "ext_cam", "true",
                                                                                                    true, is_val_overridden) == "true");

    ctx.privacy_params.privacy_activate_params.speed_based_privacy = (ctx.bagheera_config->getConfig("privacy_mode_activate",
                                                                                                     "speed_based", "true",
                                                                                                    true, is_val_overridden) == "true");
    string_to_integer (ctx.bagheera_config->getConfig("privacy_mode_activate", "enable_speed", "0", true, is_val_overridden),
                       ctx.privacy_params.privacy_activate_params.threshold_speed);
    string_to_integer (ctx.bagheera_config->getConfig("privacy_mode_activate", "enable_time", "30", true, is_val_overridden),
                       ctx.privacy_params.privacy_activate_params.threshold_time);
    ctx.privacy_params.privacy_activate_params.ignition_based_privacy = (ctx.bagheera_config->getConfig("privacy_mode_activate",
                                                                                                        "ignition_based", "true",
                                                                                                        true, is_val_overridden) == "true");
    string_to_integer (ctx.bagheera_config->getConfig("privacy_mode_activate", "post_ignition_off_duration", "0", true, is_val_overridden),
                       ctx.privacy_params.privacy_activate_params.post_ignition_off_duration);
    ctx.privacy_params.privacy_activate_params.button_based_privacy = (ctx.bagheera_config->getConfig("privacy_mode_activate",
                                                                                                     "button_long_press", "false",
                                                                                                     true, is_val_overridden) == "true");
    string_to_integer (ctx.bagheera_config->getConfig("privacy_mode_activate", "long_press_duration_ms", "5000", true, is_val_overridden),
                       ctx.privacy_params.privacy_activate_params.long_press_duration_ms);

    ctx.privacy_params.privacy_activate_params.transition_audio_feedback = (ctx.bagheera_config->getConfig("privacy_mode_activate",
                                                                                                          "transition_audio_alert",
                                                                                                          "false", true, is_val_overridden) == "true");
#ifdef BAGHEERA2
    ctx.privacy_params.privacy_activate_params.transition_audio_alert_file_regular = ctx.bagheera_config->getConfig("privacy_mode_activate",
                                                                                                                   "transition_audio_alert_file_regular",
                                                                                                                   "/home/ubuntu/autocam/audio/nd_debug2/privacy_mode_is_activated_en_f.wav", true, is_val_overridden);
    ctx.privacy_params.privacy_activate_params.transition_audio_alert_file_enhanced = ctx.bagheera_config->getConfig("privacy_mode_activate",
                                                                                                                   "transition_audio_alert_file_enhanced",
                                                                                                                   "/home/ubuntu/autocam/audio/nd_debug2/enhanced_privacy_mode_with_inward_camera_in_local_mode_is_activated_en_f.wav", true, is_val_overridden);
    ctx.privacy_params.privacy_activate_params.transition_audio_alert_file_offduty = ctx.bagheera_config->getConfig("privacy_mode_activate",
                                                                                                                   "transition_audio_alert_file_offduty",
                                                                                                                   "/home/ubuntu/autocam/audio/nd_debug2/off_duty_driving_mode_is_activated_en_f.wav", true, is_val_overridden);
    ctx.privacy_params.privacy_deactivate_params.transition_audio_alert_file_regular = ctx.bagheera_config->getConfig("privacy_mode_deactivate",
                                                                                                                   "transition_audio_alert_file_regular",
                                                                                                                   "/home/ubuntu/autocam/audio/nd_debug2/privacy_mode_is_deactivated_en_f.wav", true, is_val_overridden);
    ctx.privacy_params.privacy_deactivate_params.transition_audio_alert_file_offduty = ctx.bagheera_config->getConfig("privacy_mode_deactivate",
                                                                                                                   "transition_audio_alert_file_offduty",
                                                                                                                   "/home/ubuntu/autocam/audio/nd_debug2/off_duty_driving_mode_is_deactivated_en_f.wav", true, is_val_overridden);
#elif KRAIT
    ctx.privacy_params.privacy_activate_params.transition_audio_alert_file_regular = ctx.bagheera_config->getConfig("privacy_mode_activate",
                                                                                                                   "transition_audio_alert_file_regular",
                                                                                                                   "/data/nd_files/autocam/audio/nd_debug2/privacy_mode_is_activated_en_f.wav", true, is_val_overridden);
    ctx.privacy_params.privacy_activate_params.transition_audio_alert_file_enhanced = ctx.bagheera_config->getConfig("privacy_mode_activate",
                                                                                                                   "transition_audio_alert_file_enhanced",
                                                                                                                   "/data/nd_files/autocam/audio/nd_debug2/enhanced_privacy_mode_with_inward_camera_in_local_mode_is_activated_en_f.wav", true, is_val_overridden);
    ctx.privacy_params.privacy_activate_params.transition_audio_alert_file_offduty = ctx.bagheera_config->getConfig("privacy_mode_activate",
                                                                                                                   "transition_audio_alert_file_offduty",
                                                                                                                   "/data/nd_files/autocam/audio/nd_debug2/off_duty_driving_mode_is_activated_en_f.wav", true, is_val_overridden);
    ctx.privacy_params.privacy_deactivate_params.transition_audio_alert_file_regular = ctx.bagheera_config->getConfig("privacy_mode_deactivate",
                                                                                                                   "transition_audio_alert_file_regular",
                                                                                                                   "/data/nd_files/autocam/audio/nd_debug2/privacy_mode_is_deactivated_en_f.wav", true, is_val_overridden);
    ctx.privacy_params.privacy_deactivate_params.transition_audio_alert_file_offduty = ctx.bagheera_config->getConfig("privacy_mode_deactivate",
                                                                                                                   "transition_audio_alert_file_offduty",
                                                                                                                   "/data/nd_files/autocam/audio/nd_debug2/off_duty_driving_mode_is_deactivated_en_f.wav", true, is_val_overridden);
#endif

    ctx.privacy_params.privacy_deactivate_params.speed_based_privacy = (ctx.bagheera_config->getConfig("privacy_mode_deactivate",
                                                                                                       "speed_based", "true",
                                                                                                        true, is_val_overridden) == "true");
    string_to_integer (ctx.bagheera_config->getConfig("privacy_mode_deactivate", "disable_speed", "5", true, is_val_overridden),
                       ctx.privacy_params.privacy_deactivate_params.threshold_speed);
    string_to_integer (ctx.bagheera_config->getConfig("privacy_mode_deactivate", "disable_time", "5", true, is_val_overridden),
                       ctx.privacy_params.privacy_deactivate_params.threshold_time);
    ctx.privacy_params.privacy_deactivate_params.ignition_based_privacy = (ctx.bagheera_config->getConfig("privacy_mode_deactivate",
                                                                                                          "ignition_based", "true",
                                                                                                          true, is_val_overridden) == "true");
    string_to_integer (ctx.bagheera_config->getConfig("privacy_mode_deactivate", "post_ignition_on_duration", "0", true, is_val_overridden),
                       ctx.privacy_params.privacy_deactivate_params.post_ignition_on_duration);
    ctx.privacy_params.privacy_deactivate_params.transition_audio_feedback = (ctx.bagheera_config->getConfig("privacy_mode_deactivate",
                                                                                                             "transition_audio_alert",
                                                                                                             "false", true, is_val_overridden) == "true");
    LOG_I(TAG, "Below are the current privacy configuration:\n");
    LOG_I(TAG, "outward_privacy:%d, inward_privacy:%d, left_privacy:%d, right_privacy:%d, ext_cam_privacy:%d, driveri_audio_privacy:%d, ext_cam_audio_privacy:%d\n"
               "save_usr_alert_video:%d, gps_privacy:%d, default_privacy:%d, inward_led_color:%s, off_duty_mode:%d, enhanced_privacy:%d\n",
			   ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_FRONT], ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_BACK],
			   ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_LEFT], ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_RIGHT],
			   ctx.privacy_params.ext_cam_privacy, ctx.privacy_params.driveri_audio_privacy, ctx.privacy_params.ext_cam_audio_privacy,
			   ctx.privacy_params.save_user_alert_video, ctx.privacy_params.gps_privacy, ctx.privacy_params.default_privacy,
			   ctx.privacy_params.inward_led_color.c_str(), ctx.privacy_params.off_duty_mode, ctx.privacy_params.enhanced_privacy);

    LOG_I(TAG, "Below are the current privacy activate conditions:\n");
    LOG_I(TAG, "speed_based_privacy:%d, privacy_enable_speed:%d, privacy_enable_time:%d, ignition_based_privacy:%d, "
               "post_ignition_off_duration:%d, button_based_privacy:%d, button_long_press_duration_in_ms:%d, "
               "audio_feedback_for_privacy_transition:%d", ctx.privacy_params.privacy_activate_params.speed_based_privacy,
			   ctx.privacy_params.privacy_activate_params.threshold_speed, ctx.privacy_params.privacy_activate_params.threshold_time,
			   ctx.privacy_params.privacy_activate_params.ignition_based_privacy, ctx.privacy_params.privacy_activate_params.post_ignition_off_duration,
			   ctx.privacy_params.privacy_activate_params.button_based_privacy, ctx.privacy_params.privacy_activate_params.long_press_duration_ms,
			   ctx.privacy_params.privacy_activate_params.transition_audio_feedback);

    LOG_I(TAG, "Below are the current privacy deactivate conditions:\n");
    LOG_I(TAG, "speed_based_privacy:%d, privacy_disable_speed:%d, privacy_disable_time:%d, ignition_based_privacy:%d, "
               "post_ignition_on_duration:%d, audio_feedback_for_privacy_transition:%d",
			   ctx.privacy_params.privacy_deactivate_params.speed_based_privacy, ctx.privacy_params.privacy_deactivate_params.threshold_speed,
			   ctx.privacy_params.privacy_deactivate_params.threshold_time, ctx.privacy_params.privacy_deactivate_params.ignition_based_privacy,
			   ctx.privacy_params.privacy_deactivate_params.post_ignition_on_duration, ctx.privacy_params.privacy_deactivate_params.transition_audio_feedback);

    LOG_I(TAG, "Below is the upload video configuration for different cameras:");
    LOG_I(TAG, "upload_outward_video:%d, upload_inward_video:%d, upload_left_video:%d, upload_right_video:%d, upload_ext_cam_video:%d",
               ctx.privacy_params.upload_video[DEVICE_CAMERA_POSITION_FRONT], ctx.privacy_params.upload_video[DEVICE_CAMERA_POSITION_BACK],
               ctx.privacy_params.upload_video[DEVICE_CAMERA_POSITION_LEFT], ctx.privacy_params.upload_video[DEVICE_CAMERA_POSITION_RIGHT],
               ctx.privacy_params.upload_video_ext_cam);

    LOG_C(TAG, "Privacy configuration loaded - save_user_alert_video=%d, enhanced_privacy=%d, off_duty_mode=%d, "
               "upload_video[outward=%d, inward=%d, left=%d, right=%d, ext=%d]",
               ctx.privacy_params.save_user_alert_video, ctx.privacy_params.enhanced_privacy, ctx.privacy_params.off_duty_mode,
               ctx.privacy_params.upload_video[DEVICE_CAMERA_POSITION_FRONT], ctx.privacy_params.upload_video[DEVICE_CAMERA_POSITION_BACK],
               ctx.privacy_params.upload_video[DEVICE_CAMERA_POSITION_LEFT], ctx.privacy_params.upload_video[DEVICE_CAMERA_POSITION_RIGHT],
               ctx.privacy_params.upload_video_ext_cam);

    if (!ctx.privacy_params.enhanced_privacy) {
        if (ctx.privacy_params.privacy_activate_params.ignition_based_privacy ||
              ctx.privacy_params.privacy_deactivate_params.ignition_based_privacy) {
            pthread_t ignition_privacy_apply_th;
            if (!pthread_create (&ignition_privacy_apply_th, NULL, ignition_privacy_apply, NULL)) {
                LOG_I (TAG, "Thread created for ignition_privacy_apply");
            }
        }
        if (ctx.privacy_params.privacy_activate_params.speed_based_privacy ||
              ctx.privacy_params.privacy_deactivate_params.speed_based_privacy) {
            pthread_t speed_privacy_apply_th;
            if (!pthread_create (&speed_privacy_apply_th, NULL, speed_privacy_apply, NULL)) {
                LOG_I (TAG, "Thread created for speed_privacy_apply");
            }
        }
    }
}

bool config_init_from_factory(void) {

    CIRCULAR_BUFFER_PATH = nd_device_obj->get_external_eMMC_mount_path();
    CIRCULAR_BUFFER_PATH_EA = CIRCULAR_BUFFER_PATH + "/ea/";
    return true;

}

int main(int argc, char *argv[])
{
    nd_service_obj = NDService::get_service_obj(TAG);

    if(nd_service_obj)
        set_nd_service_object(nd_service_obj);

    service_start_time = get_system_monotonic_time();
    main_thread_pid = getpid();
    atexit (bagheera_exit);
    end_of_session_atomic = false;
    bagheera_service_exiting = false;
    string base_path = "";
    int i=0;

    printf("Initializing logger\n");
    bool status_log = nd_log_init( log_dir.c_str() );
    if(status_log == false) {
        printf("Unable to initialize logger for bagheera service:: Exiting from main");
        nd_service_obj->send_err_msg(SM_E_NDC_LOG_INIT_FAIL, NDService::UNUSED_ERR_AUX_CODE, "unable to init logger :: Exiting from main");
    }
#ifdef ROUTE_LOGS
    route_logs( log_dir.c_str() );
#endif
    nd_device_obj_init();
    config_init_from_factory();
    LOG_C(TAG,"#### Starting: ND Central ####");
    if( argc >= 3 ) {
        base_path = argv[1];
        ND_INPUT_PATH = argv[2];
    }
    else {
        LOG_C(TAG,"Need 2 Arguments for path for recording and for analytics to run, Exiting");
        return -1;
    }

    if(ND_INPUT_PATH == "") {
        LOG_C(TAG,"ND_INPUT_PATH argument is not valid, Exiting");
        return -1;
    }

    // Checks if base path exist. Else creates one. If failed to do so exit from here
    if (!file_mkdir(base_path, 0777)) {
        LOG_E(TAG, "Failed to create base path to record video files. Exiting..");
        nd_service_obj->send_err_msg(SM_E_NDC_RECORDING_PATH_FAIL, -1, "Failed to create recording path. Exiting");
        sleep (DELAY_NO_FILES_FOLDER);
        return -1;
    }

    // Checks if saveMP4 path exist. Else create one.
    if (!file_mkdir(UPL_BASE_PATH, 0777)) {
        LOG_E(TAG, "Failed to create saveMP4 path for uploader");
        nd_service_obj->send_err_msg(SM_E_NDC_UPLOAD_PATH_FAIL, -1, "Failed to create saveMP4 path for uploader");
    }

    video_encryption_config_init();
    nd_vm_configure();

    //Check free space, sleep for 15 mins and exit service if criteria not met.
    string emmc_path = nd_device_obj -> get_external_eMMC_phy_mount_path();
    int64_t fspace = file_getfreespace(emmc_path);
    LOG_I(TAG, "Free space on EMMC path %s: %lld", emmc_path.c_str(), fspace);

    if (file_is_present(bagheera_reboot_token_file) == true) {
        LOG_E(TAG, "%s is already present. ndcentral must have a crash and start", bagheera_reboot_token_file.c_str());
        first_after_boot = false;
    } else {
        file_touch(bagheera_reboot_token_file, true);
    }
    if (fspace < MIN_FREE_SPACE)
    {
        svc_util_trigger_cleanup();
        LOG_E (TAG, "free space %lld is less than %lld, triggered svc cleanup. Exiting from main", fspace, MIN_FREE_SPACE);
        nd_service_obj->send_err_msg(SM_E_NDC_LOW_EMMC, fspace, "Low EMMC, exiting");
        int msg_ids =0;
        generic_msg_t m;
        if(first_after_boot){
            if( false == send_msg( (generic_msg_t *)&m, (msg_type_t)SVC_PACIFY_START, sizeof(m), get_msgq_name(), "Q_SVC",  msg_ids++ ) ) {
                LOG_I(TAG, "Send message SVC_PACIFY_START to svc failed. ");
            }
        }

        sleep ( DELAY_MAIN_EXIT );
        return 1;
    }

    //startup_logs();

    //For debugging camera crash
#ifdef BAGHEERA2
    LOG_I (TAG,"System Uptime: %lld seconds", (get_system_monotonic_time() ) / 1000);
#elif KRAIT
    struct sysinfo info;
    if (!sysinfo (&info))
    {
        LOG_I (TAG,"System Uptime: %ld seconds",info.uptime);
    }
    else
    {
        LOG_E (TAG, "Failed to get sysinfo with errno %d",errno);
    }
#endif
    //Initialize Message Queue
    bool flush = true;
    ctx.msg_q = nd_msgq_t::get_msgq(Q_NAME, nd_msgq_t::ND_MSGQ_SERVER, flush);
    if (ctx.msg_q == NULL) {
        LOG_C(TAG,"Could not initialize message queue, Exiting");
        return -1;
    }

    //Initialize copy_or_move_file Message Queue
    bool flush_cmf = true;
    ctx.cmf_msg_q = nd_msgq_t::get_msgq(QNAME_CMF, nd_msgq_t::ND_MSGQ_SERVER, flush_cmf);
    if (ctx.cmf_msg_q == NULL) {
        LOG_C(TAG,"Could not initialize copy_or_move_file message queue, Exiting");
        return -1;
    }

    pthread_t copy_or_move_files_th;
    if (!pthread_create (&copy_or_move_files_th, NULL, copy_or_move_files_thread, NULL)) {
        LOG_I(TAG, "Thread created for copy_or_move_files");
    } else {
        LOG_E(TAG, "failed to launch copy_or_move_files_thread");
        return -1;
    }
#ifdef AUTOMATION
    if (IsDTSEnabled())
    {
        DriveSimulation::drive_simulation_enabled = true;
        ctx.Q_DTS_IMU_MSG = nd_msgq_t::get_msgq("Q_DTS_IMU_MSG", nd_msgq_t::ND_MSGQ_SERVER, flush);
        LOG_I(TAG, "DTS is enabled, so starting dts_imu_msg_loop and receiveData_IMU threads");
        thread start_dts_th(DriveSimulation::start_dts_imu_msg_loop);
        thread receiveData_IMU_loop_th(DriveSimulation::receiveData_IMU);
        start_dts_th.detach();
        receiveData_IMU_loop_th.detach();
    }
#endif
    //Initialize gps values
    pthread_mutex_lock(&rt_gps_mutex);
    ctx.saved_gps = def_gps;
    pthread_mutex_unlock(&rt_gps_mutex);
    double lat = 91.0, lon = 181.0;
    if (read_last_known_valid_gps_data(lat,lon)) {
        pthread_mutex_lock(&rt_gps_mutex);
        ctx.saved_gps.latitude = lat;
        ctx.saved_gps.longitude = lon;
        pthread_mutex_unlock(&rt_gps_mutex);
        LOG_I(TAG, "Read last known valid gps data: lat %f, lon %f", ctx.saved_gps.latitude, ctx.saved_gps.longitude);
    }
    //Initialize health monitoring
    svc_util_init(Q_NAME,0);

    // Read external camera config
    read_ext_camera_config();

    //Decide whether LED will be used to notify GPS valdity
    //or privacy mode
    set_led_functionality();

    // check if data product low fps implementation is enabled
    get_dp_enabled();

    // check if event access preview feature is enabled
    get_ea_enabled();

    get_ld_enabled();

    // gets the audio_enable from ini files
    get_audio_enable();
#ifdef BAGHEERA2
    // trace logs control
    trace_logs_config();
    trace_logs_control();
#endif
    string db_path_file = nd_device_obj->get_db_base_path() + "/" + DBFILE_NAME;
    string db_path_file_camera_crash = nd_device_obj->get_db_base_path() + "/" + DBFILE_NAME_CAMERA_CRASH;

    // Create/Open DB for camera crash
    if (nd_open_db(db_path_file_camera_crash, &db_handle_camera_crash) == false) {
        db_handle_camera_crash = NULL;
        LOG_E(TAG, "failure in open DB for camera crash");
        nd_service_obj->send_err_msg(SM_E_NDC_OPEN_DB_FAIL, 0, "open_DB for camera crash failed" );
    }

    if (nd_open_db(db_path_file, &db_handle) == false) {
        db_handle = NULL;
        LOG_E(TAG, "failure in open DB");
        nd_service_obj->send_err_msg(SM_E_NDC_OPEN_DB_FAIL, 0, "open_DB failed" );
    }

    string schema = "CREATE TABLE GENPROP(" \
            "INDEXID INTEGER PRIMARY KEY  AUTOINCREMENT," \
            "PROPERTY       TEXT     NOT NULL," \
            "DATA           TEXT     NOT NULL," \
            "TIME           BIGINT   DEFAULT 0," \
            "EPOCHTIME      BIGINT   DEFAULT 0);" ;

    if(db_handle != NULL) {
        if (nd_create_table_db(db_handle, schema) == false) {
        LOG_E(TAG, "Failed to create_table_db");
                nd_service_obj->send_err_msg(SM_E_NDC_CREATE_DB_FAIL, 0, "create_table_DB failed" );
        }
    }

    LOG_I(TAG, "success in create_table_db");

    string schema_camera_crash = "CREATE TABLE CAMERA_CRASH_DB(" \
            "INDEXID INTEGER PRIMARY KEY  AUTOINCREMENT," \
            "PROPERTY       TEXT     NOT NULL," \
            "DATA           TEXT     NOT NULL," \
            "TIME           BIGINT   DEFAULT 0," \
            "EPOCHTIME      BIGINT   DEFAULT 0);" ;

    if (db_handle_camera_crash != NULL) {
        if (nd_create_table_db(db_handle_camera_crash, schema_camera_crash) == false) {
            LOG_E(TAG, "Failed to create_table_db for camera crash");
            nd_service_obj->send_err_msg(SM_E_NDC_CREATE_DB_FAIL, 0, "create_table_DB for camera crash failed" );
        }
    }

    LOG_I(TAG, "success in create_table_db for camera crash");
    if( init_config() == false ) {
        LOG_C(TAG,"Error initing config");
        return false;
    }

#ifdef BAGHEERA2
    // Create/Open DB for side camera crash info
    string schema_side_cam_crash = "CREATE TABLE SIDE_CAM_CRASH_INFO(" \
            "INDEXID INTEGER PRIMARY KEY  AUTOINCREMENT," \
            "PROPERTY       TEXT     NOT NULL," \
            "DATA           TEXT     NOT NULL," \
            "TIME           BIGINT   DEFAULT 0," \
            "EPOCHTIME      BIGINT   DEFAULT 0);" ;

    if (db_handle_camera_crash != NULL) {
        if (nd_create_table_db(db_handle_camera_crash, schema_side_cam_crash) == false) {
            LOG_E(TAG, "Failed to create_table_db for side camera crash info");
            nd_service_obj->send_err_msg(SM_E_NDC_CREATE_DB_FAIL, 0, "create_table_DB for side camera crash info failed" );
        }
    }

    LOG_I(TAG, "success in create_table_db for side camera crash info");
#endif
    bool val_overridden = false;

    ctx.lpw_no_record = (ctx.bagheera_config->getConfig("power",
                                                        "lpw_no_record","false" ,
                                                        true, val_overridden) == "true");
    LOG_I(TAG, "ctx.lpw_no_record %d val_overridden: %d ", ctx.lpw_no_record, val_overridden);

    string pub_rt_session_id_str =
            ctx.bagheera_config->getConfig("can_alerts","enabled","0" , true, val_overridden);
    string_to_integer(pub_rt_session_id_str, pub_rt_session_id);

    read_iosix_config(is_iosix_enabled);

    //Set ignition_override based on the crank level when service starts.
    //This will help in setting correct ignition_override value
    string post_ignition_analytics_secs_str =
            ctx.bagheera_config->getConfig("streaming","post_ignition_analytics_secs","0" , true, val_overridden);
    string_to_integer(post_ignition_analytics_secs_str, ctx.post_ignition_analytics_secs);

    string post_ignition_analytics_secs_outward_str =
            ctx.bagheera_config->getConfig("streaming","post_ignition_analytics_secs_outward","0" , true, val_overridden);
    string_to_integer(post_ignition_analytics_secs_outward_str, ctx.post_ignition_analytics_secs_outward);

    ctx.crank_level_RT_thread = nd_device_obj->get_crank_level();
    // If Wake up reason supported then get the wake up reason.
    string reason_str = "";
    if (nd_device_obj->get_reset_wake_reason(pow_on_off_reason, reason_str) == false) {
        LOG_E(TAG, "get_reset_wake_reason failed");
    }
    LOG_I(TAG, "pow_on_off_reason %d, reason_str %s", pow_on_off_reason, reason_str.c_str());

    if (ctx.lpw_no_record) {
        bool is_lpw_enabled = file_is_present(lpw_no_record_persistent_file);
        ctx.previous_lpw_no_record = is_lpw_enabled ? true : false;
        lpw_state_t lpw_status = get_status_from_sysfs_source(eLPW_STAT);
        if (lpw_status == lpw_state_t::eLPW_ON) {
            if (is_lpw_enabled) {
                LOG_I(TAG, "Already in LPW no record case");
            } else {
                LOG_I(TAG, "LPW no record case, creating persistent file");
                file_touch(lpw_no_record_persistent_file);
            }
        } else if (lpw_status == lpw_state_t::eLPW_OFF) {
            if (is_lpw_enabled) {
                LOG_I(TAG, "LPW no record case exiting");
                file_delete(lpw_no_record_persistent_file);
            }
        } else {
            if (is_lpw_enabled) {
                if (ctx.crank_level_RT_thread == CRANK_HIGH) {
                    LOG_I(TAG, "lpw_status coming from sysfs is %d, CRANK is high, so exiting LPW no record", lpw_status);
                    file_delete(lpw_no_record_persistent_file);
                } else if (!((pow_on_off_reason & WAKE_ON_IGN_MASK) ||
                           (pow_on_off_reason & WAKE_ON_MOT_IMU_MASK))) {
                    LOG_I(TAG, "CRANK is low, and there is no wakeup due to IGN or IMU, assuming it as LPW no record case continuing");
                }
            } else if (ctx.crank_level_RT_thread == CRANK_LOW) {
                if (!((pow_on_off_reason & WAKE_ON_IGN_MASK) ||
                      (pow_on_off_reason & WAKE_ON_MOT_IMU_MASK))) {
                    LOG_I(TAG, "CRANK is low, and there is no wakeup due to IGN or IMU, assuming it as LPW no record case");
                    file_touch(lpw_no_record_persistent_file);
                }
            }
        }
    }

    LOG_I(TAG, "crank_level_RT_thread %d ctx.post_ignition_analytics_secs %d, ctx.post_ignition_analytics_secs_outward = %d",
                ctx.crank_level_RT_thread, ctx.post_ignition_analytics_secs, ctx.post_ignition_analytics_secs_outward);

    if (power_crank_levels_t::CRANK_HIGH == ctx.crank_level_RT_thread) {
        LOG_I (TAG, "setting ignition_override to true");
        ctx.ignition_override = true;
        ctx.ignition_status = IGNITION_STATUS_ON;
        ctx.send_rt_frames_till_time = INT_64_MAX;
        ctx.send_rt_frames_till_time_outward = INT_64_MAX;
    } else {
        LOG_I (TAG, "setting ignition_override to false");
        ctx.ignition_override = false;
        ctx.ignition_status = (file_is_present(lpw_no_record_persistent_file)) ? IGNITION_STATUS_LPW : IGNITION_STATUS_OFF;
        ctx.send_rt_frames_till_time = 0;
        ctx.send_rt_frames_till_time_outward = 0;
    }
    read_dms_connection_status_file((int&)ctx.is_dms_connected);
    write_ignition_status_file(ctx.ignition_status);
    ctx.dis_update_ts = get_system_time();

    init_config_privacy();

    if (!ctx.privacy_params.enhanced_privacy && !ctx.off_duty_privacy && !ctx.geofence_privacy &&
           ctx.privacy_params.privacy_activate_params.speed_based_privacy)
        register_with_speed_for_privacy();
    if (!ctx.privacy_params.enhanced_privacy && !ctx.off_duty_privacy && !ctx.geofence_privacy &&
           ctx.privacy_params.privacy_deactivate_params.speed_based_privacy)
        register_with_speed_for_speed();

    prop_data_t udid_entry;
    ctx.udid_string = "";
    if (get_property_DB("udid", &udid_entry, db_handle)) {
        ctx.udid_string = udid_entry.value;
    } else{
        if ((set_property_DB("udid", to_string(0))) == false) {
            nd_service_obj->send_err_msg(SM_E_NDC_SET_PROP_DB_FAIL, NDService::UNUSED_ERR_AUX_CODE, "set_property_DB failed" );
            LOG_E(TAG, "set_property_DB failed for udid");
        }
        ctx.udid_string = "0";
    }
    LOG_I(TAG, "ctx.udid_string is %s", ctx.udid_string.c_str());

    prop_data_t sc_entry;
    ctx.sessionCount_string = "";
    if (get_property_DB("sessionCount", &sc_entry)) {
        ctx.sessionCount_string = sc_entry.value;
    } else{
        if ((set_property_DB("sessionCount", to_string(0))) == false) {
            nd_service_obj->send_err_msg(SM_E_NDC_SET_PROP_DB_FAIL, NDService::UNUSED_ERR_AUX_CODE, "set_property_DB failed" );
            LOG_E(TAG, "set_property_DB failed for sessionCount");
        }
        ctx.sessionCount_string = "0";
    }
    LOG_I(TAG, "ctx.sessionCount_string is %s", ctx.sessionCount_string.c_str());

    // read extended atrribute config
    use_extended_attributes = read_extended_attr_config();

    // Send one fixed-size message per file
    std::vector<std::string> files_vec;
    if (!get_files(base_path, files_vec)) {
        LOG_E(TAG, "Failed to get file details from given path %s", base_path.c_str());
    }
    // Ensure CSV files are sent first so that global privacy state is set before processing video/audio files
    std::stable_partition(files_vec.begin(), files_vec.end(),
        [](const std::string &f) { return f.find("partial.csv") != std::string::npos; });

    for (const auto& file : files_vec) {
        ndc_move_partial_files_t single_file_msg;
        single_file_msg.type = MOVE_PARTIAL_FILES;
        single_file_msg.len = sizeof(ndc_move_partial_files_t);
        nd_strncpy(single_file_msg.folder_name, base_path.c_str(), sizeof(single_file_msg.folder_name));
        nd_strncpy(single_file_msg.file_name, file.c_str(), sizeof(single_file_msg.file_name));

        nd_msgq_t::nd_msg_t mpf_msg((char *)&single_file_msg, sizeof(single_file_msg), false);
        if (ctx.cmf_msg_q->send(mpf_msg, nd_msgq_t::ND_MSG_MED) == false) {
            LOG_E( TAG, "Cannot send message to: %s ", QNAME_CMF.c_str());
        }
    }

#ifdef BAGHEERA2
    int num_cameras = CAMERA_POSITION_MAXIMUM;
#elif KRAIT
    int num_cameras = DEVICE_CAMERA_POSITION_MAX;
#endif
    for (int i = 0; i < num_cameras; i++) {
        stringstream property_str;
        property_str << "CAM" << i << "_CRASH_COUNT";
        prop_data_t entry;

        if (db_handle_camera_crash != NULL) {
            if (get_property_DB(property_str.str(), &entry, db_handle_camera_crash, CAMERA_CRASH_DB_TABLE)) {
                string_to_integer(entry.value, cam_crash_count[i]);
                if (entry.monotonic_time > get_system_monotonic_time()) {
                    if ((set_property_DB(property_str.str(), to_string(0), db_handle_camera_crash, CAMERA_CRASH_DB_TABLE)) == false) {
                        nd_service_obj->send_err_msg(SM_E_NDC_SET_PROP_DB_FAIL, i, "set_property_DB failed for camera_crash count" );
                        LOG_E(TAG, "set_property_DB failed for cam %d", i);
                    } else {
                        LOG_I(TAG,"Resetting cam_crash_count[%d] to 0", i);
                        cam_crash_count[i] = 0;
                    }
                }
            } else {
                if ((set_property_DB(property_str.str(), to_string(0), db_handle_camera_crash, CAMERA_CRASH_DB_TABLE)) == false) {
                    nd_service_obj->send_err_msg(SM_E_NDC_SET_PROP_DB_FAIL, i, "set_property_DB failed for camera crash count" );
                    LOG_E(TAG, "set_property_DB failed for cam %d", i);
                } else {
                    LOG_I(TAG,"Resetting cam_crash_count[%d] to 0", i);
                    cam_crash_count[i] = 0;
                }
            }
        }
    }

#ifdef BAGHEERA2
    if (first_after_boot) {
        if (db_handle_camera_crash != NULL) {

            std::string delete_records_cmd = "DELETE FROM SIDE_CAM_CRASH_INFO;";
            if (!nd_exec_cmd_db(db_handle_camera_crash, delete_records_cmd, nullptr, nullptr, true)) {
                LOG_E(TAG, "Failed to delete all records from SIDE_CAM_CRASH_INFO");
            }
            else {
                LOG_I(TAG, "All records deleted from SIDE_CAM_CRASH_INFO");

                std::string reset_sequence_cmd =
                    "DELETE FROM sqlite_sequence WHERE name='SIDE_CAM_CRASH_INFO';";
                if (!nd_exec_cmd_db(db_handle_camera_crash, reset_sequence_cmd, nullptr, nullptr, true)) {
                    LOG_E(TAG, "Failed to reset AUTOINCREMENT counter for SIDE_CAM_CRASH_INFO");
                }
                else {
                    LOG_I(TAG, "AUTOINCREMENT counter reset for SIDE_CAM_CRASH_INFO");
                }
            }
        }
        else {
            LOG_E(TAG, "Database handle is NULL, cannot perform cleanup");
        }
    }

#endif

#ifdef BAGHEERA2
    int timeout_value = 30000000; //30s expressed in microseconds
    int waiting_time = 0;
    while ((file_is_present("/dev/shm/nd_files_c/cam_rec_service_started") == false) && (waiting_time < timeout_value)) {
        LOG_I(TAG, "Camera record service is not yet started, let's wait........");
        usleep(50000);
        waiting_time += 50000;
    }

    LOG_I(TAG, "Camera record service is started, we can proceed with Bagheera service startup");

    if (first_after_boot == false) {
        LOG_I(TAG, "Bagheera service is restarted after it got crashed, send message to cam_rec service");
        send_bagheera_restart_msg_to_cam_rec_service(Q_NAME);
    }
#endif

    if( init_modules(base_path) == false ) {
        LOG_C(TAG,"Init modules failed, Exiting");
        return -1;
    }
    // Enable all sensors after init is successful
    enable_all_sensors();

    // Previously, keep_alive was sent before init_modules(), so in case of continuous init_modules() failures,
    // ndcentral sent keep_alive every time and the device did not reboot. Now, keep_alive is sent after init_modules(),
    // so in case of continuous init_modules() failures, keep_alive will not be sent and the device will reboot.
    svc_util_send_keepalive();

    prop_data_t rtc_entry;
    ctx.rtcValid_string =  "1" ;
    if(get_property_DB("rtcValidTime", &rtc_entry)) {
        ctx.rtcValidTime_string = rtc_entry.value;
        int64_t savedRtcTime, currTime = get_system_time() ;
        string_to_int64(ctx.rtcValidTime_string, savedRtcTime);
        if( ( currTime < savedRtcTime ) || ( currTime > ( savedRtcTime + month_in_seconds * 1000 ) ) ) {
            ctx.rtcValid_string =  "0" ;

        }
    }
    else{
        ctx.rtcValidTime_string = to_string(ctx.dis_update_ts);
        if((set_property_DB("rtcValidTime", ctx.rtcValidTime_string )) == false) {
            nd_service_obj->send_err_msg(SM_E_NDC_SET_PROP_DB_FAIL, NDService::UNUSED_ERR_AUX_CODE, "set_property_DB failed" );
            LOG_E(TAG, "set_property_DB failed for rtcValidTime");
        }

    }
    LOG_I(TAG, "ctx.rtcValidTime_string is %s", ctx.rtcValidTime_string.c_str());

    ctx.supercap_status = false;


    if (false == send_drv_login_query())
        LOG_E (TAG,"Failed to send DRV_LOGIN_QUERY");

    //Send an initial message to START_PIPELINE
    //Later will be send by MONITOR process
    ndc_cam_msg_t start_cam_msg;
    start_cam_msg.type = START_CAMERA;
    start_cam_msg.len = sizeof( ndc_cam_msg_t );
    start_cam_msg.cam_num = 0xffffffff;
    LOG_I(TAG, "cam_start_msg->cam_num %x", start_cam_msg.cam_num);
    nd_msgq_t::nd_msg_t msg((char *)&start_cam_msg, sizeof(start_cam_msg), false);
    ctx.msg_q->send(msg, nd_msgq_t::ND_MSG_MED);

    if("true" == ctx.bagheera_config->getConfig("camera","sync_files","true", true, val_overridden)) {
        pthread_t sync_files_th;
        if (!pthread_create (&sync_files_th, NULL, sync_files, NULL)) {
            LOG_I (TAG, "Thread created for file sync");
        }
    } else {
        LOG_I(TAG, "file sync is disabled in config");
    }

#ifdef KRAIT
    obd_data_ptr = (ndmbmsg_obd_adc_data_t *)malloc(sizeof(ndmbmsg_obd_adc_data_t));
    if(NULL == obd_data_ptr) {
        LOG_E(TAG,"Memory allcation failed for obd_data_ptr with return:%d",obd_data_ptr);
    }
    memset(obd_data_ptr,0,sizeof(ndmbmsg_obd_adc_data_t));

    obd_adc_thread_info_t *tinfo;
    tinfo = (obd_adc_thread_info_t *)malloc(sizeof(obd_adc_thread_info_t));
    if (tinfo == NULL) {
        LOG_E(TAG,"Memory allcation failed for obc adc thread");
    }
    memset(tinfo,0,sizeof(obd_adc_thread_info_t));
    int thread_ret= pthread_create(&tinfo->obd_thread_id, NULL, &obd_adc_subs_funcptr , NULL);
    if(0 != thread_ret) {
        LOG_E(TAG, "pthread create failed by error: %d", thread_ret);
    }
    pthread_t adc_update_th;
    if (!pthread_create (&adc_update_th, NULL, adc_update_main, NULL)) {
        LOG_I (TAG, "Thread created for adc_update_main");
    }
    else {
        LOG_E(TAG, "failed to launch adc_update thread");
    }
#endif
    if("true" == ctx.nd_config_analytics->getConfig("low_latency_alert_notification","enabled","false", true, val_overridden)) {
        pthread_t lla_th;
        if (!pthread_create (&lla_th, NULL, lla_main, NULL)) {
            LOG_I (TAG, "Thread created for lla_main");
        }
        else {
            LOG_E(TAG, "failed to launch lla_main thread");
        }
    } else {
        LOG_I(TAG, "lla is disabled in nd_config_analytics");
    }

    ctx.dis_update_ts = get_system_time();

    // set default network info to be sent in obs
    set_default_network_info();

    // create audio playback for live streaming socket
    init_live_streaming_audio_files();
    create_audio_socket();

    // Check if live streaming is enabled
    check_live_streaming_feature_enabled();

    // Check and initialize if user initiated audio alert feature is enabled
    initialize_settings_for_user_triggered_alerts();

    // read QR scan config info from bagheera config
    init_qr_scan();

    //subscribing for data from obd service
    /*if(check_obd_service_configuration()){
        std::string OBD_DATA_SUB = "NDMB_OBD_SERVICE";
        NDMBClient msg_client(OBD_DATA_SUB);
        bool res=msg_client.subscribe(TOPIC_OBD_DATA, ndmb_obddata_cb, obd_data_retry_count, obd_data_retry_time);
        if(res == false){
            LOG_E(TAG,"Failed to subscribe to OBD data");
        }
        std::string OBD_ENG_SUB = "NDMB_OBD_ENG_SERVICE";
        NDMBClient msg_client_es(OBD_ENG_SUB);
        res=msg_client_es.subscribe(TOPIC_ENGINE_STATUS, ndmb_engine_status_cb, obd_data_retry_count, obd_data_retry_time);
        if(res == false){
            LOG_E(TAG,"Failed to subscribe to Engine status data");
        }
        if(check_fuel_report_configuration()){
            std::string OBD_FR_SUB = "NDMB_OBD_FR_SERVICE";
            NDMBClient msg_client_fr(OBD_FR_SUB);
            res=msg_client_fr.subscribe(TOPIC_FUEL_REPORT_DATA, ndmb_fuelreport_cb, obd_data_retry_count, obd_data_retry_time);
            if(res == false){
                LOG_E(TAG,"Failed to subscribe to Fuel report data");
            }
        }
        if(check_idling_report_configuration()){
            std::string OBD_IR_SUB = "NDMB_OBD_IR_SERVICE";
            NDMBClient msg_client_ir(OBD_IR_SUB);
            res=msg_client_ir.subscribe(TOPIC_IDLING_REPORT_DATA, ndmb_idlingreport_cb, obd_data_retry_count, obd_data_retry_time);
            if(res == false){
                LOG_E(TAG,"Failed to subscribe to Idling report data");
            }
        }
    }*/

    audio_monitor_util_init(); // Create audio monitor thread

    //Get into a message loop
    msg_loop();     //Get blocked here

    audio_monitor_util_deinit(); // Join audio monitor thread

    if( deinit_modules() == false ) {
        LOG_C(TAG,"Exiting");
        return -1;
    }

    LOG_C(TAG,"Exiting: gracefully");
    nd_service_obj->release_service_obj();

    nd_close_db(db_handle);
    db_handle = NULL;
    nd_close_db(db_handle_camera_crash);
    db_handle_camera_crash = NULL;

    return 0;
}
