/* Copyright (C) 2016 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Arun V <arun.vj@netradyne.com>, October 2016
 */

#ifndef ND_CENTRAL_H
#define ND_CENTRAL_H

#include "metadata_buffer.h"

#include <array>
#include <queue>
#include <tuple>

#include <imu.h>
#include <gps.h>
#include <ublox.h>
#include <obd.h>
#include <audio_record.h>
#include <genmeta.h>
#include <speaker.h>
#include <config_parser.h>
#include <MediaRecorder.h>
#include <nd_msgq.h>
#include <nd_msg_utils.h>
#include <storage_utils.h>
#include <sdcard_utils.h>
#include <nd_signal_utils.h>

#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <photodiode.h>
#include <stdlib.h>

#include <nd_msg_types.h>
#include <nd_factory.h>
#include <nd_shared_mem_utils.h>

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

#define MD5SUM_TEMP

#define DEVICE_CONFIG_INI "/home/ubuntu/config/deviceconfig.ini"
#define ND_DEVICE_INI "/home/ubuntu/.nddevice/nddevice.ini"
#define BAGHEERACONFIG_INI "/home/ubuntu/.nddevice/latest/bagheera_config.ini"
#define ND_CONFIG_ANALYTICS "/home/ubuntu/.nddevice/latest/nd_config.ini"
#ifdef BAGHEERA2
#define CAMERA_OVERRIDE_INI "/home/ubuntu/config/cam_override.ini"
#elif KRAIT
#define CAMERA_OVERRIDE_INI "/data/nd_files/config/cam_override.ini"
#endif

static const int FNAME_SIZE=128;

static const string   Q_NAME = "q_nd_central";
static const string   Q_APM  = "q_apm";

static const string video_file_extn = ".mp4";
static const string audio_file_extn = ".aac";

static const int64_t INT_64_MAX = 0x7fffffffffffffff;


static const string video_metadata_extn = "metadata.txt";
static const float invalid_lat = 91.0f;
static const float invalid_long = 181.0f;

#define MIN_FNAME_LEN 8

//for setting bit_pos'th bit in num
void set_bit(int& num, int bit_pos);

//for clearing bit_pos'th bit in num
void clear_bit(int& num, int bit_pos);

//for checking if a particular bit is set or not
bool is_bit_set(int num, int bit_pos);

static const string log_dir = "/home/ubuntu/.nddevice/log/ndcentral";
static const string common_log_dir = "/home/ubuntu/.nddevice/log";

//regular privacy will cover all cases except enhanced and off_duty
enum privacy_type_t {
    REGULAR,
    ENHANCED,
    OFF_DUTY
};

enum ignition_types_t {
    IGNITION_STATUS_OFF,
    IGNITION_STATUS_ON,
    IGNITION_STATUS_LPW
};

enum dms_connection_status_t {
    DMS_CONNECTION_STATUS_IGNITION_OFF = 1000,
    DMS_CONNECTION_STATUS_IGNITION_LPW = 2000,
};

typedef struct privacy_mode_activate_params {
    bool speed_based_privacy = true;            //Decides if privacy status changes based on speed or not
    int threshold_speed = 0;                    //speed threshold in mph after which privacy will be activated(if speed_based is true)
    int threshold_time = 30;                    //speed threshold along with this much time in sec, after which privacy will be activated(if speed_based is true)
    bool ignition_based_privacy = true;         //if true, enters to privacy on ign low comes out on ign high
    int post_ignition_off_duration = 0;         //after this much duration in sec of ignition off, privacy will be activated(if ignition_based is true)
    bool button_based_privacy = false;          //button long press required for privacy ON, in ignition off condition
    int long_press_duration_ms = 5000;          //button long press duration in msec
    bool transition_audio_feedback = false;     //if this is true, audio alert will be played when privacy transition happens from disable to enable
    string transition_audio_alert_file_regular; //transition audio alert filname for regular privacy
    string transition_audio_alert_file_enhanced;//transition audio alert filname for enhanced privacy
    string transition_audio_alert_file_offduty; //transition audio alert filname for off-duty privacy
}privacy_mode_activate_params_t;

typedef struct privacy_mode_deactivate_params {
    bool speed_based_privacy = true;            //Decides if privacy status changes based on speed or not
    int threshold_speed = 5;                    //speed threshold in mph after which privacy will be deactivated(if speed_based is true)
    int threshold_time = 5;                     //speed threshold along with this much time in sec, after which privacy will be deactivated(if speed_based is true)
    bool ignition_based_privacy = true;         //if true, enters to privacy on ign low comes out on ign high
    int post_ignition_on_duration = 0;          //after this much duration in sec of ignition on, privacy will be deactivated(if ignition_based is true)
    bool transition_audio_feedback = false;     //if this is true, audio alert will be played when privacy transition happens from enable to disable
    string transition_audio_alert_file_regular; //transition audio alert filname for regular privacy
    string transition_audio_alert_file_offduty; //transition audio alert filname for off-duty privacy
}privacy_mode_deactivate_params_t;

typedef struct privacy_mode_params {
    bool cam_privacy[DEVICE_CAMERA_POSITION_MAX] = {false, true, false, false};  //Individual camera privacy
    bool ext_cam_privacy = false;                                                //all external cameras privacy
    bool driveri_audio_privacy = true;                                           //driveri audio privacy(if it is true audio recording will be disabled)
    bool ext_cam_audio_privacy = false;                                          //external camera audio privacy
    bool save_user_alert_video = false;                                          //if this is true, driver initiated alert will upload irrespective of privacy(applicable only to inward)
    bool gps_privacy = false;                                                    //gps tracking will be disabled if it is true
    bool off_duty_mode = false;                                                  //personal privacy feature
    string inward_led_color = "red";                                             //decides inward led color(can be red, green or purple)
    bool default_privacy = true;                                                 //Default speed privacy status used by ndcentral until BTFV service enables/disables privacy
    bool enhanced_privacy = true;                                                //if this is true don't save inward video even when privacy is off
    bool upload_video[DEVICE_CAMERA_POSITION_MAX] = {true, false, true, true};   //upload of individual camera to IDMS will be disabled if this is false
    bool upload_video_ext_cam = true;                                            //upload of external cameras to IDMS will be disabled if this is false
    privacy_mode_activate_params_t privacy_activate_params;
    privacy_mode_deactivate_params_t privacy_deactivate_params;
}privacy_mode_params_t;

typedef enum {
    REASON_NO_PRIVACY,   // 0
    REASON_SPEED,        // 1
    REASON_IGNITION,     // 2
    REASON_BUTTON_BASED, // 3
    REASON_OFFDUTY,      // 4
    REASON_ENHANCED,     // 5  // This will only be used to represent the enhanced privacy mode for blackouting in the final privacy mode states
    REASON_GEOFENCE,     // 6
} privacy_reason_t;

struct nd_central_ctx {

    MediaRecorder   *media_recorder[CAMERA_POSITION_MAXIMUM];

    // native camera config is the list of camera related config params
    native_camera_config_t native_cam_config[CAMERA_POSITION_MAXIMUM];

    int gps_counter;
    Gps             *gps;
    Ublox           *ublox;
    Imu             *imu;
    Photodiode      *photodiode;
    Audio           *audio;
    Genmeta         *genmeta;
    Config_parser   *device_config;
    Config_parser   *nd_config;
    Config_parser   *bagheera_config;
    Config_parser   *nd_config_analytics;
    Gps::gps_data_t saved_gps;
    Ublox::ublox_gps_data_t saved_ublox_gps;
    string          base_path_cam0;
    string          base_path_cam123;
    string          fname[CAMERA_POSITION_MAXIMUM];
    string          presVideoName = "";
    string          prevVideoName = "";
    string          nextVideoName = "";
    int64_t         firstframe_time[CAMERA_POSITION_MAXIMUM];
    int64_t         gps_start_time;
    volatile bool   pipeline_enabled;
    int             session_number = 0;
    bool            session_flipflop = false;
    bool            ignition_override = true;
    nd_msgq_t       *msg_q;
    nd_msgq_t       *cmf_msg_q;
#ifdef AUTOMATION
    nd_msgq_t       *Q_DTS_IMU_MSG;
#endif

    const string circular_buffer_q_name = "q_circular_buffer";
    nd_msgq_t       *circular_buffer_msg_q=NULL;

    const string obd_message_q_name = "q_obd_app";
    pid_t   obd_pid;
    bool privacy_mode_led = false;
    bool ublox_enabled = false;
    bool ublox_led_indication = true;
    bool flash_ublox_led = false;
    //To maintain flipflop buffers
    meta_buff_t     meta_buff[2];

    int64_t camStartTime[CAMERA_POSITION_MAXIMUM][2];
    int64_t camPtsStartTime[CAMERA_POSITION_MAXIMUM][2];
    int out_meta_count = 0;
    int session_cnt[CAMERA_POSITION_MAXIMUM] = {0,0,0,0,0,0,0,0,0};
    int session_cnt_ld[CAMERA_POSITION_MAXIMUM] = {0,0,0,0,0,0,0,0,0};
    int64_t camStartTime_ld[CAMERA_POSITION_MAXIMUM][2];
    int64_t camPtsStartTime_ld[CAMERA_POSITION_MAXIMUM][2];

    int session_cnt_dp[1] = {0};
    int64_t camStartTime_dp[1][2];
    int64_t camPtsStartTime_dp[1][2];

    // Led blinking on user alert
    int led_blinking_time_left;
    int const led_blinking_durarion = 5 ;
    bool is_led_blinking = false;

    volatile int dis_status = 0;
    int64_t dis_update_ts;
    string dis_uid_string = "";
    int idle_mode_RT_thread;

    privacy_mode_params_t privacy_params;
    queue<tuple<float, int64_t, float>> speed_samples;  // speed, timestamp, accuracy

    bool off_duty_privacy;
    bool geofence_privacy;
    bool fused_privacy;
    int inward_privacy_state;
    int crank_level;
    power_crank_levels_t crank_level_RT_thread;
    int64_t send_rt_frames_till_time;
    int post_ignition_analytics_secs;
    int64_t send_rt_frames_till_time_outward;
    int post_ignition_analytics_secs_outward;

    // RT related varialbes
    // IMU RT
    bool enable_rt_inertial;
    bool enable_rt_gps;
    bool enable_rt_gps_geo_fence;
    void *zmq_context_imu = NULL;
    void *zmq_context_gps = NULL;
    void *zmq_context_gps_geo_fence = NULL;
    void *zmq_publisher_imu = NULL;
    void *zmq_publisher_gps = NULL;
    void *zmq_publisher_gps_geo_fence = NULL;
    // CAM RT
    realtime_camera_config_t rt_config[CAMERA_POSITION_MAXIMUM];
    void *zmq_context[CAMERA_POSITION_MAXIMUM];
    void *zmq_full_frame_context[DEVICE_CAMERA_POSITION_MAX];
    void *zmq_publisher[CAMERA_POSITION_MAXIMUM];
    void *zmq_full_frame_publisher[DEVICE_CAMERA_POSITION_MAX];
    int yuv_frame_count[CAMERA_POSITION_MAXIMUM]= {0,0,0,0,0,0,0,0,0};
    int full_frame_count[DEVICE_CAMERA_POSITION_MAX]= {0,0,0,0};
    int sessio_drop_message[CAMERA_POSITION_MAXIMUM]= {0,0,0,0,0,0,0,0,0};

    string sessionid_rt[CAMERA_POSITION_MAXIMUM];

    // Live streaming audio
    void *zmq_context_audio = NULL;
    void *zmq_publisher_audio = NULL;
    string live_stream_outward_start_file = "";
    string live_stream_outward_end_file = "";
    string live_stream_inward_start_file = "";
    string live_stream_inward_end_file = "";
    string dual_stream_start_file = "";
    string dual_stream_end_file = "";
    bool live_stream_audio_notification = false;

    //User initiated alerts - button 0 and button 1
    struct UserAlertAudioCfg {
        static constexpr uint16_t no_of_buttons_ = 5;
        std::array<std::string, no_of_buttons_> audio_file_;
        std::array<bool, no_of_buttons_> enable_status_ = {false, false, false, false, false};
    };

    UserAlertAudioCfg user_alert_cfg;

    bool ext_cam_feature_enabled = false;
    bool ext_cam_enabled[DEVICE_CAMERA_POSITION_MAX] = {false, false, false, false};
    int ext_cam_channel_num[DEVICE_CAMERA_POSITION_MAX] = {0, 0, 0, 0};
    bool ext_cam_audio_enable[DEVICE_CAMERA_POSITION_MAX] = {false, false, false, false};
    int ext_cam_framerate[DEVICE_CAMERA_POSITION_MAX] = {30, 30, 30, 30};
    int64_t cam_session_start_time[CAMERA_POSITION_MAXIMUM][2];
    int64_t cam_session_stop_time[CAMERA_POSITION_MAXIMUM][2];
    int irled_level; // IRLED Brightness level
    bool supercap_status; // true -> power is disconnected
    bool lpw_no_record = false;
    bool previous_lpw_no_record = false;
    string udid_string = "-1";
    string sessionCount_string = "-1";
    string rtcValidTime_string;
    string rtcValid_string;
    string rtc_jump_from_string= "0";
    string rtc_jump_to_string = "0";
    std::ofstream fstream_partial_meta_file ;
    string meta_csv_partial_path ;
    string next_meta_csv_partial_path ;

    bool hdmaps_mode_enabled = false;
    bool imu_data = false;
    bool ir_led_status;

    bool is_camrec_zmqpub_created = false;
    bool is_camrec_zmqpubdms_created = false;
    bool stop_inward_cam_zmqsub = false;
    bool stop_dms_cam_zmqsub = false;
    bool is_dms_connected = false;
    int ignition_status;

    bool is_driverlogin_qr_enabled = false;
    int qr_code_scan_status = 0;

    int qr_codes_detected[2] = {0, 0};
    int qr_codes_decoded[2] = {0, 0};
    int qr_codes_mismatched[2] = {0, 0};
};

#define GSTREAMER_NAME_LENGTH_MAX (256)

typedef struct {
    GENERIC_MSG
    int      smb_id;
    int64_t  uid;
    int      frame_cnt;
    uint64_t timestamp;
} shm_frame_header;

typedef struct {
    uint64_t pts;
    int64_t uid;
    int smb_id;
} cam_RT_metadata;

#ifdef DMS_CAMERA_SUPPORTED
typedef struct {
    uint64_t pts;
    NvBufferParamsEx paramsEx;
} dmscam_RT_metadata;
#endif

enum ndc_msg_type_t {
    START_CAMERA=PRIVATE_MSG,
    STOP_CAMERA,
    START_META,
    STOP_DUMP_META,
    PHOTODIODE_CALL_BACK,
    RESTART_CAMERA,
    COPY_OR_MOVE_FILE,
    MOVE_PARTIAL_FILES,
    QUIT
};

struct ndc_generic_msg_t {
    ndc_msg_type_t type;
    int len;
};

struct ndc_cam_msg_t {
    ndc_msg_type_t type;
    int len;

    int cam_num;
};

struct ndc_cam_restart_msg_t {
    ndc_msg_type_t type;
    int len;

    int cam_num;
};

struct ndc_start_meta_msg_t {
    ndc_msg_type_t type;
    int len;
    int cam_pos;
    uint64_t epoch_time;
    uint64_t raw_time;
    char f_name[FNAME_SIZE];
};

struct ndc_stop_meta_msg_t {
    ndc_msg_type_t type;

    int len;
    int cam_num;
    int flipflop;
    uint64_t raw_time;
    uint64_t epoch_time;
    uint64_t pts_time;

    char f_name[FNAME_SIZE];
};

struct ndc_copy_or_move_file_msg_t {
    ndc_msg_type_t type;

    int len;
    char *fname;
    int cam_num;
    int flipflop;
    uint64_t epoch_time;
    uint64_t raw_time;
    uint64_t pts_time;
    bool is_ld;
};

struct ndc_photodiode_cb_msg_t {
    ndc_msg_type_t type;
    int len;
    int pt_status;
};

struct ndc_move_partial_files_t
{
    ndc_msg_type_t type;

    int len;
    char folder_name[FNAME_SIZE];
    char file_name[FNAME_SIZE];
};

#endif
