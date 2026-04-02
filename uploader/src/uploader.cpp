#include <bits/stdc++.h>
#include <sys/file.h>
#include <sys/time.h>
#include <sys/stat.h>
#include "nd_msgq.h"
#include "nd_msg_types.h"
#include "nd_msg_utils.h"
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <queue>
#include "config_parser.h"
#include <string>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <jansson.h>
#include <stdexcept>
#include <sqlite3.h>
#include <log.h>
#include <dirent.h>
#include <zipper.h>
#include <unzipper.h>
#include <curl/curl.h>
#include <linux/errno.h>
#include <storage_utils.h>
#include <nd_file_utils.h>
#include <nd_auth_utils.h>
#include "service_utils.h"
#include <nd_net_utils.h>
#include <boost/interprocess/streams/vectorstream.hpp>
#include <nd_auth_openssl.h>
#include <nd_time.h>
#include <system_utils.h>
#include <nd_ext_cam_utils.h>
#include <nd_task.h>
#include <nd_factory.h>
#include <nd_messenger.h>
#include "health_stats_utils.h"
#include "rental_utils.h"
#include "data_recording.h"
//#include <nd_cb_utils.h>
#include <sdcard_utils.h>
#include <nd_cb_utils.h>
#include <utils.h>
#include <nd_prop_utils.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <nd_utils.h>
#include "vod_health.h"
#include "ffmpeg_utils.h"
#include "ndmb/nd_msg_interface.h"
#include "ndmb/nd_mbclient.h"
#include "nd_timer.h" 
#include "ea_images_utils.h"

using namespace std;
using namespace zipper;
rgb_err video_rgb_analyser(string mp4_filename, int ch_num);
#define Q_NAME "UniUpload"
#define THREAD_TAG_LEN 16
#define ROUTE_LOGS
#define EXT_CAM_OFFSET 3
#define Q_NAME_TO_UPL "Q_TO_UPL"
#define Q_NAME_TO_CB "Q_TO_CB"
#define DEFAULT_VALUE_REC_VID_ENABLED 1
#define DEFAULT_VALUE_UPL_VID_ENABLED 1
#define OTHER_FPS 15

NDService *nd_service_obj = NULL; //nd service object, to detect critical Errors which will be send to Health stats and cloud 
ND_DeviceFactory *nd_device_obj = NULL;
nd_msgq_t *msg_q_cb_to_upl;
nd_msgq_t *msg_q_upl_to_cb;

static const string BAGHEERACONFIG_INI = "/home/ubuntu/.nddevice/latest/bagheera_config.ini";
static bool save_ext_camera_files_in_dhub = false;
static const string Q_POWERMON = "q_power_monitor";
static const string Q_EXT_CAM = "EXT_CAM";
static const string NOT_APPLICABLE = "NA";

static const int ND_REOPERATE_MAX_TIMEOUT = 20;
static const int DEVICE_CAMERA_POSITION_MAX = 4;
static const int MAX_EXT_CAMERAS = 4;
static bool ext_cam_enabled[MAX_EXT_CAMERAS] = {false, false, false, false};
static int ext_cam_framerate[MAX_EXT_CAMERAS] = {30, 30, 30, 30};
static bool ext_cam_audio_enable[MAX_EXT_CAMERAS] = {false, false, false, false};
static int time_zone = -800;
static int EXT_CAM_FILES_VOD_PROCESS_DELAY = 10;
static int MAX_EXT_VOD_RETRY_COUNT = 2;
static int MAX_WAIT_TIME_VOD_FILE_PULL = 400;
static int CONSECUTIVE_VOD_UPLOAD_FAILURE_COUNT = 0; //tracks continuous failure counts for /upload/video/ call
const int file_size_threshold_for_cam_malfunction = 512*1024; // 0.5 MB

static const int MINUTE_TO_MILLISECS = 60000;
static const int VOD_EXPIRED = 30 /*days*/ * 24 /*hour*/ * 60 /* min */; // 30 days in mins - device uptime
static const int MAX_VOD_Q_SIZE = 1000; //device will not accept more than this number of vod requests at a time
static int VOD_TIMEOUT = 72 * 60; // 72 hours in minutes - device uptime
static bool THREAD_FETCH_EXT_VOD_CREATED = false;

static const long curl_conn_timeout_secs = 120L;
static const long curl_max_timeout_secs = 1200L;
static long curl_conn_timeout_alert_secs = 90L;
static long curl_max_timeout_alert_secs = 120L;
static long curl_conn_timeout_lla_secs = 5L;
static long curl_max_timeout_lla_secs = 10L;
static int lla_retry_limit = 3;
static const int LLA_RETRY_SLEEP = 5;
static const int CB_QUERY_SLEEP_MICRO_SEC = 1000 * MAX_DELAY_FOR_CB_RES_IN_MS;

static const string curl_retry_count = "0";
static const string event_processed = "Event data processed";
static const string logs_saved = "Device-Logs saved";
static const string vod_processed = "true";
bool rgb_analysis = false;

//arunvj: make these static
const char *TAG="UPL";
static const char *DRP="DRP";
static const char *TAG_THREAD_P_LARGE ="PL";
static const char *TAG_THREAD_P_SMALL ="PS";
static const char *TAG_THREAD_LOG_UPL ="TH_LOG_UPL";
static const char *TAG_THREAD_UPLOADER_DATA_UPLOAD_PENDING_STATUS ="TH_UPLOADER_DATA_UPLOAD_PENDING_STATUS";
static const char *TAG_THREAD_FETCH_EXT_VOD ="TH_FETCH_EXT_VOD";
static const char *TAG_THREAD_VOD_ELAPSED_TIME ="TH_VOD_ELAPSED_TIME";
static const char *TAG_THREAD_P_LLA ="PLLA";
static const char *TAG_THREAD_P_LLA_UP ="PLLA_U";

static const string CRITICAL_LOG_PATH = "/home/ubuntu/.nddevice/log/archive/critical/";
static const string NON_CRITICAL_LOG_PATH = "/home/ubuntu/.nddevice/log/archive/non_critical/";
static const string SYS_LOG_PATH = "/var/log/";
static const string SYS_LOG_PATH_ARCHIVED = "/var/log/nd_archive/syslog/";

static string UPLOADER_DB_PATH = "";
static string EA_DB_PATH = "";
static string CIRC_BUFF_PATH = "";
static string ALERTS_PATH = "";
static string EXT_CAM_FILE_PATH = "";
static string old_path = "";
string ALERTS_PATH_DIR = "";
string ALERTS_PATH_DIR_PHYSMNT = "";
string MOUNT_SRC = "";
string CB_DB_PATH = "";
string sdcard_img_path = "";
string SIGN_CROP_OUTPUT_PATH = "";
string oldest_drp_retained_file = "";

#if defined(BAGHEERA2)
#define NO_SDCARD
static const string RAMFS_PATH = "/dev/shm/nd_files_c/";
static const string ND_DATA_PATH = "/home/ubuntu/.nddevice";
static const char* VOD_LIST_CSV_FILE = "/dev/shm/nd_files_c/vod_list.csv";
static const char* VOD_LIST_7Z_FILE = "/dev/shm/nd_files_c/vod_list.7z";
#else
static const string RAMFS_PATH = "/dev/shm";
static const string ND_DATA_PATH = "/data/nd_files/state_files";
static const char* VOD_LIST_CSV_FILE = "/dev/shm/vod_list.csv";
static const char* VOD_LIST_7Z_FILE = "/dev/shm/vod_list.7z";
#endif

static const string OBS_ZIP_PATH = ND_DATA_PATH + "/observations.zip";
static const string OBS_7Z_PATH = ND_DATA_PATH + "/observations.7z";
static const string SIGN_CROPS_7Z_PATH = ND_DATA_PATH + "/sign_crops/";
const string EA_IMGS_ZIPS_PATH = ND_DATA_PATH + "/ea_zips/";
string EA_IMGS_PATH = "";
static const string ld_extn = ".ld.mp4";
static const string dp_extn = ".dp.mp4";
static const string EVENT_CODE_HIGH_G = "0.0.0";
static const string EVENT_CODE_MOD_G = "0.1.0";
static const string EVENT_CODE_LOW_G = "0.4.0";

//VOD FAILURE REASONS
static const string VOD_FAIL_REASON_EXHAUSTED_MAX_RETRY = "Exhausted max retry";
static const string VOD_FAIL_REASON_UNAVAILABLE = "Video not available";
static const string VOD_FAIL_REASON_EMPTY_FILE = "Video file is empty";
static const string VOD_FAIL_REASON_DATA_RECORDING_DISABLED = "Data recording is disabled";
static const string VOD_FAIL_REASON_REQ_VOD_NOT_MP4 = "Requested vod file is not a mp4";
static const string VOD_FAIL_REASON_DIR_ERR = "Failed to split folder and file names";

static const string VOD_FAIL_REASON_VOD_PATH_ERROR = "Failed to split folder and file names ";
static const string VOD_FAIL_REASON_FS_ERROR_FOLDER_CREATION = "Could not create output folder for file conversion";
static const string VOD_FAIL_REASON_VIDEO_DECRYPTION_FAILED = "Video file decryption failed";
static const string VOD_FAIL_REASON_JSON_CREATION_FAILED = "JSON creation failed for VOD";
static const string VOD_FAIL_REASON_EXT_VOD_TIMEOUT = "External video could not be fetched. Timed out";
static const string VOD_FAIL_REASON_REC_PRIVACY = "Record privacy is enabled";
static const string VOD_FAIL_REASON_UPL_PRIVACY = "Upload privacy is enabled";
static const string VOD_FAIL_REASON_DRP = "DRP";
static const string VOD_FAIL_REASON_DRP_UNKNOWN_TS = "DRP - Unknown timestamp";
static const string VOD_FAIL_REASON_CB_QUERY_FAILED = "CB query failed";
static const string VOD_FAIL_REASON_EXHAUSTED_MAX_RETRY_CB_ERROR = "Exhausted max retry - CB Failure";


typedef sqlite3 *uploader_db_handle_t;
static const string VOD_ERR_MSG = "VOD Not Available @";
void* handle_req_upload( void *pq );
bool is_vod_req_timeout(req_upload_msg_t *msg);
bool vod_expired(req_upload_msg_t *msg);
void add_to_vod_upload_Q(req_upload_msg_t *msg);
void add_to_vod_fetch_Q(req_upload_msg_t *msg);
bool update_cloud_vod_failure(req_upload_msg_t *msg);
uploader_db_handle_t db_handle = NULL;
uploader_db_handle_t ea_db_handle = NULL;
mutex uploader_data_upload_pend_mtx;
mutex upl_cb_query_mtx;
condition_variable uploader_data_upload_pend_cv;
static bool drp_enabled = false ;
static bool syslog_enabled = false;
bool curl_init_once_flag = false;
static int64_t drp_clock_hours = 72;
static int64_t oldest_drp_retained_file_sc = -1;
std::unique_ptr<nd::utils::TimerTick> drp_tick_;
static constexpr uint64_t cb_broadcast_interval_ = 60U; // 60 seconds for TimerTick

// Linear backoff multipliers
static const int CB_DELAY_MULTIPLIER = 100; // 100 seconds

// Config for extended attributes
static bool extended_attr_enabled = false;

enum upload_priority_t {
    UL_PRIORITY_HIGH = 0,
    UL_PRIORITY_MID  = 5,
    UL_PRIORITY_LOW  = 10,

    UL_PRIORITY_P1 = 1, //VOD related
    UL_PRIORITY_P2 = 2,
    UL_PRIORITY_P3 = 3,
    UL_PRIORITY_P4 = 4,
    UL_PRIORITY_P5 = 5,
    UL_PRIORITY_P6 = 6,
    UL_PRIORITY_P7 = 7,
    UL_PRIORITY_P8 = 8,
    UL_PRIORITY_P9 = 9,
    UL_PRIORITY_P10 = 10,
    UL_PRIORITY_P11 = 11,
    UL_PRIORITY_P12 = 12,
    UL_PRIORITY_P13 = 13,
    UL_PRIORITY_P14 = 14,
    UL_PRIORITY_P15 = 15,
    UL_PRIORITY_P16 = 16,
    UL_PRIORITY_P17 = 17,
    UL_PRIORITY_P18 = 18,
    UL_PRIORITY_P19 = 19,
    UL_PRIORITY_P20 = 20,
    UL_PRIORITY_VOD_FAILURE = 21, //VOD related - internal
    UL_PRIORITY_VOD_CB_DELAY = 22, //VOD related - internal
    UL_PRIORITY_LOWEST
};

enum upload_max_retry_t {
    UL_RETRY_TIMEOUT = 999,
    UL_RETRY_MAX = 10,
    UL_RETRY_MID  = 6,
    UL_RETRY_MIN  = 1,
};

enum upload_states {
    U_NEW = 0,
    U_UPLOADING,
    U_UPLOAD_FAILED,
};

enum upload_log_type {
    UL_CRITICAL_LOG = 0,
    UL_CRITICAL_LOG_TIME_RANGE,
    UL_NON_CRITICAL_LOG,
    UL_SYS_LOG,
};

enum ext_vod_req_status_t {
    EXT_VOD_ST_NEW = 0,
    EXT_VOD_ST_REQ_SENT,
    EXT_VOD_ST_ACK,
    EXT_VOD_ST_DONE,
    EXT_VOD_ST_DELETE
};

struct upload_class_t{
    upload_max_retry_t retry_limit;
    upload_priority_t priority;
};


string upload_history_status_msg;
int upload_history_status_code;
string lla_status_msg;
int lla_status_code;

static int MAX_CONVERT_FILE_FORMAT_TIMEOUT = 60;

upload_class_t get_upload_class(int level, bool is_vod = false);

struct compare : public std::binary_function<req_upload_msg_t*, req_upload_msg_t*, bool>
{
    bool operator()(const req_upload_msg_t* a, const req_upload_msg_t* b) const{
        upload_class_t c1, c2;
        if(a->msg_type == REQ_UPLOAD_VOD && b->msg_type == REQ_UPLOAD_VOD){
            //VOD has a different set of priority. Need to handle it separately
            c1 = get_upload_class(a->level, true /* is_vod */);
            c2 = get_upload_class(b->level, true /* is_vod */);

            if(c1.priority == c2.priority){
                c1 = get_upload_class(a->req_priority, true /* is_vod */);
                c2 = get_upload_class(b->req_priority, true /* is_vod */);
            }
        }
        else {
            c1 = get_upload_class(a->level);
            c2 = get_upload_class(b->level);
        }

        if(c1.priority == c2.priority){
            return a->request_time > b->request_time;
        }
        return c1.priority > c2.priority;
    }
};

uint64_t get_epoch(){
    struct timeval tv;
    struct timezone tz;
    gettimeofday(&tv ,&tz);
    uint64_t time = tv.tv_sec*(uint64_t)1000000+tv.tv_usec;
    return time;
}

struct multipart_curl_response_t {
    int64_t start_time;
    int64_t end_time;
    string api_resp_txt;
    bool status;
};

#ifdef USE_UPLOADER_TEST
#include "uploader_test_utils.h"
#endif


bool check_file_status(string video);
bool transcoding_in_progress(string video_file_name, circular_buffer_transcode_status_t tc_status);
bool check_retry_request(priority_queue<req_upload_msg_t*, vector<req_upload_msg_t*>, compare> *pq, req_upload_msg_t *msg, pthread_mutex_t qlock );
void handle_vod_failure(req_upload_msg_t *msg, priority_queue<req_upload_msg_t*, vector<req_upload_msg_t*>, compare> *pq);
void clean_up( req_upload_msg_t *msg );
bool init_server_params( void );
int upload_event(uploader_db_handle_t db_handle, req_upload_msg_t *msg);
int upload_logs(req_upload_msg_t *msg, upload_log_type log_type);
string get_vod_failure_reason_by_id(upl_to_cb_query_result_t failure_code, bool &file_unavailable);
cb_response_to_upl_query_t get_upload_status_from_cb(const string fname,
        const upload_file_type_t file_type, const unsigned int query_id);
int upload_vod_list(const string job_id);
int upload_vod(req_upload_msg_t *msg, bool trim_vod, const string rgb_status);
int prepare_upload_sign_crops(req_upload_msg_t *msg);
int upload_sign_crops(const string sign_crop_sevenz_path, const string drp, const string jobid, const string status, const int64_t time_stamp, const string error_msg);
bool send_log_upl_completion_msg(req_upload_msg_t *msg_req, string client_id, int upload_log_status);
uploader_db_handle_t create_db (string path);
uploader_db_handle_t create_ea_db (string path);
bool insert_db (uploader_db_handle_t handle, req_upload_msg_t *upload_req, int status, string md5sum );
bool insert_db_vod (uploader_db_handle_t handle, req_upload_msg_t *upload_req, bool is_ext_video);
bool is_vod_req_available(uploader_db_handle_t handle, req_upload_msg_t *upload_req);
bool update_upload_status (uploader_db_handle_t handle, req_upload_msg_t *upload_req, int status );
bool delete_upload_request (uploader_db_handle_t handle, req_upload_msg_t *upload_req );
bool delete_vod_upload_request (uploader_db_handle_t handle, req_upload_msg_t *upload_req);
void get_failed_uploads (uploader_db_handle_t handle, vector<req_upload_msg_t*> &upload_list);
void get_pending_vods(uploader_db_handle_t handle, vector<req_upload_msg_t*> &vod_list);
bool print_db(uploader_db_handle_t);
bool shutdown_check_for_dhub_offline_and_pending_ext_cam_vods(uploader_db_handle_t handle, int);
int get_vod_elapsed_time (uploader_db_handle_t handle, req_upload_msg_t *upload_req);
int get_pending_vod_count(uploader_db_handle_t handle);
int get_total_vod_req_count(uploader_db_handle_t handle);
bool is_ext_vod_fetched(uploader_db_handle_t handle, req_upload_msg_t *upload_req);
bool is_vod_req_failed(uploader_db_handle_t handle, req_upload_msg_t *upload_req);
string get_ext_vod_path(uploader_db_handle_t handle, req_upload_msg_t *upload_req);
bool is_external_video(string video);
bool mark_vod_failed(req_upload_msg_t *msg, bool is_failed, string failure_reason, bool is_unavailable);
bool update_vod_failure_details(uploader_db_handle_t handle, req_upload_msg_t *upload_req, bool vod_failure, string failure_reason, bool is_unavailable);
bool update_vod_failure_status (uploader_db_handle_t handle, req_upload_msg_t *upload_req, bool vod_failure);
bool update_fetch_ext_video_status (uploader_db_handle_t handle, req_upload_msg_t *upload_req, bool fetched_ext_video, string new_fname);
bool update_vod_priority (uploader_db_handle_t handle, req_upload_msg_t *upload_req);
bool update_vod_elapsed_time (uploader_db_handle_t handle, int elapsed_min);
bool update_vod_retry_count (uploader_db_handle_t handle, req_upload_msg_t *upload_req);
bool is_vod_priority_changed(uploader_db_handle_t handle, req_upload_msg_t *upload_req);
bool mark_vod_cancelled (uploader_db_handle_t handle, req_upload_msg_t *upload_req);
bool update_vod_failure_reason (uploader_db_handle_t handle, req_upload_msg_t *upload_req, string failure_reason);
bool delete_cancelled_vod_requests (uploader_db_handle_t handle);
bool is_vod_cancelled(uploader_db_handle_t handle, req_upload_msg_t *upload_req);
bool mark_vod_unavailable (uploader_db_handle_t handle, req_upload_msg_t *upload_req);
bool dump_vod_list_csv(uploader_db_handle_t handle, const char* csv_file_path,  bool &is_file_empty);
bool is_vod_unavailable(uploader_db_handle_t handle, req_upload_msg_t *upload_req);
string get_vod_failure_reason (uploader_db_handle_t handle, req_upload_msg_t *upload_req);
bool check_ext_vod_req_present(uploader_db_handle_t handle);
int get_ext_vod_req_status(uploader_db_handle_t handle,  req_upload_msg_t *upload_req);
string get_rgb_status(uploader_db_handle_t handle, req_upload_msg_t *upload_req);
void set_ext_vod_req_status(uploader_db_handle_t handle, req_upload_msg_t *upload_req, int ext_vod_req_status);
bool get_vod_req_by_id_and_fname(uploader_db_handle_t handle, string video, string vod_id, req_upload_msg_t *req );
void update_ext_vod_stats(uploader_db_handle_t handle, req_upload_msg_t *upload_req, int ext_vod_req_status,
        int is_fetched, int not_available, int is_failed, string failure_reason, string rgb_status);

bool get_vod_rank(uploader_db_handle_t handle, req_upload_msg_t *upload_req, int &rank);
int copy_ext_vod(req_upload_msg_t *msg);
void clone_message_received(req_upload_msg_t *msg, req_upload_msg_t *new_msg);
bool convert_file_format(void *args);
bool ffmpeg_trim_video (string inpfname, string outfname, int start_time, int end_time, int &start_frame_idx, int &end_frame_idx, int &offset, int &duration);
string check_filetype(string filename);
void send_obs_gen_message_healthstats(string meta_fname, int64_t upload_add, int64_t upload_del, string status, string reason);
pthread_mutex_t qlock;
pthread_mutex_t qlock_long;
pthread_mutex_t qlock_misc;
pthread_mutex_t qlock_ext_vod;
pthread_mutex_t qlock_lla;
pthread_mutex_t qlock_drp;

bool is_obs_req_present = false;
bool is_ea_imgs_req_present = false; 
string device_id, cloud_server, server_url, api_version, device_type, curr_version, curr_state, session_id, curr_mount_src;
bool mount_sdcard;
string uploader_socket_path, uploader_socket_topic;
int counter;
int vod_pend_msg_counter = 0;
int igni_status = -1;
static const string ND_DEVICE_REL_PATH = "/home/ubuntu/.nddevice";
static const string LOG_DIR = ND_DEVICE_REL_PATH+"/log/" + "unifieduploader";
static const string ND_INPUT_PATH = "/home/iriscli/ND_INPUT";
static const int CALL_SUCCESS = 0;
static const int CALL_FAILED = -1;
static const int CALL_INVALID = -2;
static const int SD_CARD_MOUNT_FAILED = -3;
static const int CALL_DEFER = -4;
static const int CALL_DEFER_FETCH_MDVR = -6;
static const int CALL_CANCELLED = -8;
static const int CALL_FAILED_CB_DELAY = -9;

static const int MAX_FAILURE_RETRY_COUNT = 6;

// LQ file will be created instantaneously so changed sleep time from 120 secs to 5
static const int CALL_DEFER_SLEEP = 5;
static const int CALL_DEFER_FETCH_MDVR_SLEEP = 15;
static const int IGN_OFF_VOD_RETRY_SLEEP = 10;
static const int OBS_FILE_COUNT = 10;
static const int OBS_FILE_COUNT_MIN = 1;
static const int EA_IMGS_BATCH_COUNT = 10;
static const int EA_IMGS_FILE_COUNT_MIN = 1;
static const int OBS_FILE_LIMIT = 10000;
static const int OBS_FOLDER_SIZE = 250*1024*1024;
static const int TWENTY_FIVE_KB = 25*1024; 
const int EA_IMGS_DB_ENTRY_LIMIT = 24000; // 100 hours * 60 mins * 2 cams * 2(extra margin)
static const string TRIM_VOD_OUT = "/home/iriscli/saveMP4/trim_vod_out/";
static const string INT_BUFF_PATH = "/home/iriscli/internal_buff/";
static const string INT_COMPLETE_OBS_PATH = "/home/ubuntu/.nddevice/observations/";
static const string MP4_EXT = ".mp4";

static const string NO_LOGS = "no_logs";
static const int BACKOFF_BASE_RETRY_SECS = 5;
static const int MDVR_EMPTY_FILE_MAX_RETRY_COUNT = 10;
static const int VOD_ELAPSED_TIME_COUNT_INTERVAL = 3; //3 minutes
static int mdvr_empty_file_retry_counter = 0;
static int cb_query_id = 0;
priority_queue<req_upload_msg_t*, vector<req_upload_msg_t*>, compare> pq_ext_vod;
priority_queue<req_upload_msg_t*, vector<req_upload_msg_t*>, compare> pq_long;

string exec_cmd(const char* cmd) {
    char buffer[128];
    string result = "";
    string stream(cmd);
    stream.append(" 2>&1");
    FILE* pipe = popen(stream.c_str(), "r");
    if (!pipe) return "";
    try {
        while (!feof(pipe)) {
            if (fgets(buffer, 128, pipe) != NULL)
                result += buffer;
        }
    } catch (...) { }
    pclose(pipe);
    return result;
}

bool check_process(string pname){
    string command = "ps -ef | grep \""+ pname + "\" | grep -v grep";
    string output = exec_cmd(command.c_str());
    return !output.empty();
}

#if 0
string get_md5sum_of_file(string path){
    EVP_MD_CTX mdctx;
    const EVP_MD *md;
    unsigned char output[EVP_MAX_MD_SIZE];
    int i;
    unsigned int output_len;
    unsigned char data[1024];
    int bytes;
    /* Initialize digests table */
    OpenSSL_add_all_digests();
    /* You can pass the name of another algorithm supported by your version of OpenSSL here */
    /* For instance, MD2, MD4, SHA1, RIPEMD160 etc. Check the OpenSSL documentation for details */
    md = EVP_get_digestbyname("MD5");
    if(!md) {
        LOG_I(TAG,"Unable to init MD5 digest");
    }
    FILE *in_file = fopen (path.c_str(), "rb");
    if (in_file == NULL) {
        LOG_I(TAG,"Unable to open file for md5sum %s", path.c_str());
        return "";
    }

    EVP_MD_CTX_init(&mdctx);
    EVP_DigestInit_ex(&mdctx, md, NULL);
    /* to add more data to hash, place additional calls to EVP_DigestUpdate here */
    while ((bytes = fread (data, 1, 1024, in_file)) != 0)
        EVP_DigestUpdate(&mdctx, data, bytes);
    EVP_DigestFinal_ex(&mdctx, output, &output_len);
    EVP_MD_CTX_cleanup(&mdctx);
    fclose (in_file);
    stringstream ss;
    for (int i=0; i<output_len; i++) {
        ss << hex << setw(2) << setfill('0') << (int) output[i];
    }
    return ss.str();
}
#endif


bool init_server_params(){
    string cloud_config_path = ND_DEVICE_REL_PATH+ "/latest/cloudconfig.ini";
    string nddevice_path = ND_DEVICE_REL_PATH + "/nddevice.ini";
    string device_config_path = "/home/ubuntu/config/deviceconfig.ini";
    string ndcore_config_path = ND_DEVICE_REL_PATH + "/latest/nd_core_common.ini";
    // string cloud_config_path = "/home/karna/Documents/Dev/sample/nd_core_utils/test/cloudconfig.ini";
    // string nddevice_path = "/home/karna/Documents/Dev/sample/nd_core_utils/test/nddevice.ini";
    // string device_config_path = "/home/karna/Documents/Dev/sample/nd_core_utils/test/deviceconfig.ini";

    Config_parser cloud_config_parser(cloud_config_path);
    Config_parser nddevice_parser(nddevice_path);
    Config_parser device_config_parser(device_config_path);
    Config_parser bagheera_config_parser(BAGHEERACONFIG_INI);
    Config_parser ndcore_config_parser(ndcore_config_path);

    if (bagheera_config_parser.getParseStatus() != true)
    {
        LOG_E(TAG, "Unable to parse %s", BAGHEERACONFIG_INI.c_str());
    }
    else
    {
        bool is_val_overridden = false;
        bool override_val = true;
        string curl_conn_timeout_alert_str = "";
        string curl_max_timeout_alert_str = "";
        if((curl_conn_timeout_alert_str = bagheera_config_parser.getConfig("uploader_settings", "conn_timeout_alert", "", override_val, is_val_overridden)) == "")
        {
            LOG_I (TAG, "conn timeout for alert will be default %ld", curl_conn_timeout_alert_secs);
        }
        else
        {
            int64_t curl_conn_timeout_alert_int;
            if (string_to_int64 (curl_conn_timeout_alert_str, curl_conn_timeout_alert_int))
            {
                if(curl_conn_timeout_alert_int >=60 && curl_conn_timeout_alert_int <=120)
                {
                    curl_conn_timeout_alert_secs = curl_conn_timeout_alert_int;
                }
                else
                {
                    LOG_I (TAG, "value for conn timeout via config is not expected %lld. Falling back to orig", curl_conn_timeout_alert_int);
                }
            }
            LOG_I (TAG, "conn timeout for alert based on config will be %ld", curl_conn_timeout_alert_secs);
        }
        if((curl_max_timeout_alert_str = bagheera_config_parser.getConfig("uploader_settings", "max_timeout_alert", "", override_val, is_val_overridden)) == "")
        {
            LOG_I (TAG, "max timeout for alert will be default %ld", curl_max_timeout_alert_secs);
        }
        else
        {
            int64_t curl_max_timeout_alert_int;
            if (string_to_int64 (curl_max_timeout_alert_str, curl_max_timeout_alert_int))
            {
                if(curl_max_timeout_alert_int >=100 && curl_max_timeout_alert_int <=200)
                {
                    curl_max_timeout_alert_secs = curl_max_timeout_alert_int;
                }
                else
                {
                    LOG_I (TAG, "value for max timeout via config is not expected %lld. Falling back to orig", curl_max_timeout_alert_int);
                }
            }
            LOG_I (TAG, "max timeout for alert based on config will be %ld", curl_max_timeout_alert_secs);
        }
        string lla_retry_limit_str = "";
        if((lla_retry_limit_str = bagheera_config_parser.getConfig("uploader_settings", "lla_retry_limit", "", override_val, is_val_overridden)) == "")
        {
            LOG_I (TAG, "retry limit for lla will be default %d", lla_retry_limit);
        }
        else
        {
            int64_t lla_retry_limit_int;
            if (string_to_int64 (lla_retry_limit_str, lla_retry_limit_int))
            {
                if(lla_retry_limit_int > 0 && lla_retry_limit_int <=20)
                {
                    lla_retry_limit = lla_retry_limit_int;
                }
                else
                {
                    LOG_I (TAG, "value for lla retry limit via config is not expected %d. Falling back to orig", lla_retry_limit);
                }
            }
            LOG_I (TAG, "retry limit for lla based on config will be %d", lla_retry_limit);
        }
        string vod_timeout_str = "";
        if((vod_timeout_str = bagheera_config_parser.getConfig("uploader_settings", "vod_timeout_hrs", "", override_val, is_val_overridden)) == "")
        {
            LOG_I (TAG, "retry limit for vod timeout will be default %d mins", VOD_TIMEOUT);
        }
        else
        {
            int vod_timeout_int;
            if (string_to_integer (vod_timeout_str, vod_timeout_int))
            {
                if(vod_timeout_int > 0)
                {
                    VOD_TIMEOUT = vod_timeout_int * 60; //mins
                }
                else
                {
                    LOG_I (TAG, "Unexpected config value vod_timeout_hrs:%s. Falling back to default: %d hrs", vod_timeout_str.c_str(), VOD_TIMEOUT/60);
                }
            }
            LOG_I (TAG, "retry limit for vod timeout based on config will be %d mins", VOD_TIMEOUT);
       }
       string syslog_enabled_default_str = "false";
       string syslog_enabled_str = bagheera_config_parser.getConfig("log", "enable_syslog", syslog_enabled_default_str, override_val, is_val_overridden);
       if (syslog_enabled_str == "true")
       {
           syslog_enabled = true;
           LOG_I(TAG, "syslog_enabled: true");
       }
       else
       {
           syslog_enabled = false;
           LOG_I(TAG, "syslog_enabled: false");
       }
   }
   if (cloud_config_parser.getParseStatus() && nddevice_parser.getParseStatus() && device_config_parser.getParseStatus()){

        device_id = device_config_parser.getConfig("identity","deviceId","");
        if( device_id == "" ) {
                device_id = device_config_parser.getConfig("identity","deviceid","");
        }

        cloud_server = cloud_config_parser.getConfig("cloud","server","prod");
        server_url = cloud_config_parser.getConfig(cloud_server,"ota","");
        api_version = cloud_config_parser.getConfig("cloud","ota-version","");

        device_type = device_config_parser.getConfig("identity","deviceType","");
        if( device_type == "" ) {
            device_type = device_config_parser.getConfig("identity","devicetype","");
        }

        curr_version = nddevice_parser.getConfig("version","nddevice","");
        curr_state = nddevice_parser.getConfig("version","state","");

        session_id = device_config_parser.getConfig("identity","sessionId","");
        if( session_id == "" ) {
            session_id = device_config_parser.getConfig("identity","sessionid","");
        }

#if defined(KRAIT) || defined(BAGHEERA2)
        mount_sdcard = ("true" == bagheera_config_parser.getConfig("sdcard","mount","false"));
        if (false == mount_sdcard){
            curr_mount_src = nd_device_obj->get_external_eMMC_dev_node();
        } else {
            curr_mount_src = nd_device_obj->get_external_eMMC_dev_node();
        }
#endif
        return true;
    }
    else{
        LOG_E(TAG,"Parsing of config files failed. Exiting!!");
        return false;
    }
}

int get_camera_id_from_filename(string filename) {

    // extracting filename if complete path is given
    filename = filename.substr(filename.find_last_of("/\\")+1);
    string camera_id_str = filename.substr(0,filename.find_first_of("_"));
    int camera_id;
    if(!string_to_integer(camera_id_str, camera_id)) {
        LOG_E(TAG, "Failed to get camera id from filename: %s", filename.c_str());
        camera_id = -1;
    }
    return camera_id;
}

void get_files( const char* path, vector< pair<long int, string> > &file_list, const char* extension)
{
   bool g_ignore_hidden = true;
   DIR* dir_file = opendir( path );
   if ( dir_file )
   {
      struct dirent* h_file;
      while (( h_file = readdir( dir_file )) != NULL )
      {
         if ( !strcmp( h_file->d_name, "."  )) continue;
         if ( !strcmp( h_file->d_name, ".." )) continue;

         // in linux hidden files all start with '.'
         if ( g_ignore_hidden && ( h_file->d_name[0] == '.' )) continue;

         // dirFile.name is the name of the file.
         if ( strstr( h_file->d_name, extension )){
            string file_path;
            file_path = string() + path + h_file->d_name;
            file_list.push_back(make_pair(get_file_creation_time(file_path.c_str()), file_path));
         }
      }
      closedir( dir_file );
   }
}

string get_driver_image_for_obs(string obs_path){
    // TODO: Add failure checks
    int r_index_sep = obs_path.rfind("/")+1;
    int r_index_sep_end = obs_path.rfind("_y")+2;
    string obs_name = obs_path.substr(r_index_sep, r_index_sep_end-r_index_sep);
    string driver_image_name = obs_name + ".jpg";
    string driver_image_path = INT_COMPLETE_OBS_PATH + driver_image_name;
    return driver_image_path;
}

void check_delete_driver_image_for_obs(string obs_path){
    string driver_image_path = get_driver_image_for_obs(obs_path);
    file_delete(driver_image_path);
}

void remove_files_from_disk(vector< pair<long int, string> > &file_list){
    for(unsigned i = 0; i<file_list.size(); i++) {
        pair<long int, string> p = file_list[i];
        file_delete(p.second);
        if(p.second.find("json")!= string::npos || p.second.find("zip")!= string::npos){
            send_obs_gen_message_healthstats(p.second, -1, get_system_time(), "pass", "file added to zip, deleting");
            check_delete_driver_image_for_obs(p.second);
        }
    }
}

/* holder for curl fetch */
struct curl_fetch_st {
    char *payload;
    size_t size;
};

/* callback for curl fetch */
size_t curl_callback (void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;                             /* calculate buffer size */
    struct curl_fetch_st *p = (struct curl_fetch_st *) userp;   /* cast pointer to fetch struct */

    /* expand buffer */
    p->payload = (char *) realloc(p->payload, p->size + realsize + 1);

    /* check buffer */
    if (p->payload == NULL) {
      /* this isn't good */
      LOG_I(TAG, "ERROR: Failed to expand buffer in curl_callback");
      /* free buffer */
      free(p->payload);
      /* return */
      return -1;
    }

    /* copy contents to buffer */
    memcpy(&(p->payload[p->size]), contents, realsize);

    /* set new buffer size */
    p->size += realsize;

    /* ensure null termination */
    p->payload[p->size] = 0;

    /* return size */
    return realsize;
}

/* fetch and return url body via curl */
CURLcode curl_fetch_url(CURL *ch, const char *url, struct curl_fetch_st *fetch) {
    CURLcode rcode;                   /* curl result code */

    /* init payload */
    fetch->payload = (char *) calloc(1, sizeof(fetch->payload));

    /* check payload */
    if (fetch->payload == NULL) {
        /* log error */
        LOG_I(TAG, "ERROR: Failed to allocate payload in curl_fetch_url");
        /* return error */
        return CURLE_FAILED_INIT;
    }

    /* init size */
    fetch->size = 0;

    /* set url to fetch */
    curl_easy_setopt(ch, CURLOPT_URL, url);

    /* set calback function */
    curl_easy_setopt(ch, CURLOPT_WRITEFUNCTION, curl_callback);

    /* pass fetch struct pointer */
    curl_easy_setopt(ch, CURLOPT_WRITEDATA, (void *) fetch);

    /* set default user agent */
    curl_easy_setopt(ch, CURLOPT_USERAGENT, "libcurl-agent/1.0");

    /* Suppress libcurl's internal SIGPIPE/SIGALRM manipulation in multi-threaded use.
     * Guard: only when AsynchDNS is present (c-ares or threaded resolver); without it
     * libcurl uses alarm() for DNS timeouts and CURLOPT_NOSIGNAL would disable that,
     * causing DNS to block indefinitely regardless of CURLOPT_CONNECTTIMEOUT. */
    static const bool curl_async_dns =
        (curl_version_info(CURLVERSION_NOW)->features & CURL_VERSION_ASYNCHDNS) != 0;
    if (curl_async_dns) {
        curl_easy_setopt(ch, CURLOPT_NOSIGNAL, 1L);
    } else {
        LOG_W(TAG, "WARN: libcurl built without AsynchDNS - CURLOPT_NOSIGNAL not set");
    }

    /* set timeout */
    if(strstr(url, "eventdata") != NULL || strstr(url, "observations") != NULL){
        curl_easy_setopt(ch, CURLOPT_CONNECTTIMEOUT, curl_conn_timeout_alert_secs);
        curl_easy_setopt(ch, CURLOPT_TIMEOUT, curl_max_timeout_alert_secs);
    }
    else if(strstr(url, "lla") != NULL){
        curl_easy_setopt(ch, CURLOPT_CONNECTTIMEOUT, curl_conn_timeout_lla_secs);
        curl_easy_setopt(ch, CURLOPT_TIMEOUT, curl_max_timeout_lla_secs);
    }
    else{
        curl_easy_setopt(ch, CURLOPT_CONNECTTIMEOUT, curl_conn_timeout_secs);
        curl_easy_setopt(ch, CURLOPT_TIMEOUT, curl_max_timeout_secs);
    }

    // Specifying cerificate path explicitly; as libcurl is not picking certs from the default path
#ifdef CA_CERT_PATH
    curl_easy_setopt(ch, CURLOPT_CAPATH, "/etc/ssl/certs");
#endif

    /* enable location redirects */
    curl_easy_setopt(ch, CURLOPT_FOLLOWLOCATION, 1);

    /* set maximum allowed redirects */
    curl_easy_setopt(ch, CURLOPT_MAXREDIRS, 1);

    /* fetch the url */
    rcode = curl_easy_perform(ch);

    /* return */
    return rcode;
}

int prepare_multipart_form_curl_request(const char *url, char *payload, const char *path, multipart_curl_response_t &multipart_curl_response) {
    int ret = CALL_FAILED;
    int64_t start_time;
    int64_t end_time;
    string api_response;
    do {
        bool event_data = false;
        bool lla = false;
        CURL *ch;                                               /* curl handle */
        CURLcode rcode;                                         /* curl result code */
        struct curl_fetch_st curl_fetch;                        /* curl fetch struct */
        struct curl_fetch_st *cf = &curl_fetch;                 /* pointer to fetch struct */
        struct curl_slist *headers = NULL;                      /* http headers to send with request */
        struct curl_httppost *formpost=NULL;
        struct curl_httppost *lastptr=NULL;

        if(curl_init_once_flag == false) {
            curl_global_init(CURL_GLOBAL_ALL);
        }

        /* init curl handle */
        if ((ch = curl_easy_init()) == NULL) {
            /* log error */
            LOG_I(TAG, "ERROR: Failed to create curl handle in fetch_session");
            /* return error */
            api_response = "DEVICE_ERROR: Failed to create curl handle";
            break;
        }

        string header_device_key = "";
        bool header_status = get_auth_header(header_device_key);
        if(!header_status) {
            LOG_E(TAG, "Corrupted jwt, Not connecting to cloud !");
            api_response = "DEVICE_ERROR: Corrupted jwt, Not connecting to cloud !";
            break;
        }
        string header_device_type = "X-DeviceType: " + device_type;
        string header_device_id = "X-DeviceId: " + device_id;
        

        /* set content type */
        headers = curl_slist_append(headers, "Content-Type: multipart/form-data");
        headers = curl_slist_append(headers, header_device_type.c_str());
        headers = curl_slist_append(headers, header_device_id.c_str());
        headers = curl_slist_append(headers, header_device_key.c_str());

        /* set curl options */
        if(strstr(url, "eventdata") != NULL){
            event_data = true;
            curl_formadd(&formpost,
                        &lastptr,
                        CURLFORM_COPYNAME, "fileName",
                        CURLFORM_COPYCONTENTS, payload,
                        CURLFORM_END);

            if(strstr(path, "zip") != NULL){
                curl_formadd(&formpost,
                            &lastptr,
                            CURLFORM_COPYNAME, "dataZip",
                            CURLFORM_FILE, path,
                            CURLFORM_END);
            }
            else{
                curl_formadd(&formpost,
                            &lastptr,
                            CURLFORM_COPYNAME, "data",
                            CURLFORM_FILE, path,
                            CURLFORM_END);
            }
        }else {
            if(strstr(url, "lla") != NULL) {
                lla = true;
            }
            curl_formadd(&formpost,
                        &lastptr,
                        CURLFORM_COPYNAME, "data",
                        CURLFORM_COPYCONTENTS, payload,
                        CURLFORM_END);

            if(path != NULL){
                curl_formadd(&formpost,
                            &lastptr,
                            CURLFORM_COPYNAME, "file",
                            CURLFORM_FILE, path,
                            CURLFORM_END);
            }
        }

        curl_easy_setopt(ch, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(ch, CURLOPT_HTTPPOST, formpost);

        LOG_I(TAG, "Calling service: %s", url);
        /* fetch page and capture return code */
        start_time = get_system_time();
        rcode = curl_fetch_url(ch, url, cf);
        end_time = get_system_time();

        if(event_data) {
            upload_history_status_code = rcode;
        }

        if(lla) {
            lla_status_code = rcode;
        }
        /* cleanup curl handle */
        curl_easy_cleanup(ch);

        /* then cleanup the formpost chain */
        curl_formfree(formpost);

        /* free headers */
        curl_slist_free_all(headers);

        /* check return code */
        if (rcode != CURLE_OK || cf->size < 1) {
            /* log error */
            LOG_I(TAG, "ERROR: Failed to fetch url (%s) - curl said: %s",
                url, curl_easy_strerror(rcode));

            if(event_data) {
                upload_history_status_msg = curl_easy_strerror(rcode);
            }

            if(lla) {
                lla_status_msg = curl_easy_strerror(rcode);;
            }


            /* return error */
            api_response = "DEVICE_ERROR: Failed to fetch url";
            break;
        }

        /* check payload */
        if (cf->payload != NULL) {
            /* print result */
            LOG_I(TAG, "CURL Returned: \n%s\n", cf->payload);

            api_response = cf->payload;
            upload_history_status_msg = cf->payload;
            upload_history_status_code = rcode;

            string response(cf->payload);
            if(response.find(vod_processed) != string::npos ||
                    response.find(event_processed) != string::npos ||
                    response.find(logs_saved) != string::npos){
                if(event_data) {
                    upload_history_status_msg = response;
                }

                if(lla) {
                    lla_status_msg = response;
                }
                /* free payload */
                free(cf->payload);
                ret = CALL_SUCCESS;
                break;
            }
            else if(response.find(RESPONSE_JWT_HEADER_MISSING) != string::npos ||
                    response.find(RESPONSE_JWT_SIG_INVALID) != string::npos ||
                    response.find(RESPONSE_JWT_ALG_INVALID) != string::npos) {
                notify_key_corruption();
            }
            free(cf->payload);
        } else {
            /* return */
            string str_msg = "Failed to populate payload";
            LOG_E(TAG, str_msg.c_str() );

            if(event_data) {
                upload_history_status_msg = str_msg;
            }

            if(lla) {
                lla_status_msg = str_msg;
            }

            nd_service_obj->send_err_msg(SM_E_UPLD_CURL_PAYLOAD_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
            api_response = str_msg;
            api_response = "Failed to populate payload";
            break;
        }
        string str_msg = "Not a valid data in payload";

        if(event_data) {
            upload_history_status_msg = str_msg;
        }

        if(lla) {
            lla_status_msg = str_msg;
        }

        LOG_E(TAG, str_msg.c_str() );
        nd_service_obj->send_err_msg(SM_E_UPLD_CURL_PAYLOAD_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
    } while (false);

    multipart_curl_response.start_time = start_time;
    multipart_curl_response.end_time = end_time;
    multipart_curl_response.api_resp_txt = api_response;
    multipart_curl_response.status = (ret == CALL_SUCCESS);



    return ret;
}

string check_observations_complete(string filename, string md5sum_from_obs_file){
    string new_md5sum = "_" + md5sum_from_obs_file;
    size_t pos = 0;
    if((pos = filename.find(new_md5sum, pos)) != string::npos){
        filename.replace(pos, new_md5sum.length(), "");
    } else{
        filename = "";
    }
    return filename;
}

bool check_create_dir(string path){
    struct stat statbuf;
    int isDir = 0;
    if (stat(path.c_str(), &statbuf) != -1) {
       if (S_ISDIR(statbuf.st_mode)) {
          isDir = 1;
          LOG_I(TAG,"Folder exists. Not creating %s", path.c_str());
          return true;
       }
       else{
            int remove_status = remove(path.c_str());
            LOG_I(TAG, "Removing junk file %s : %d", path.c_str(), remove_status);
       }
    }
    else{
        int status = -1;
        int temp_umask = umask(0);
        status = mkdir(path.c_str(), S_IRWXU | S_IRWXG | S_IRWXO);
        umask(temp_umask);
        if(status == 0){
            LOG_I(TAG,"Created folder %s", path.c_str());
            return true;
        }
    }
    return false;
}

bool is_path_healthy(string path){
    struct stat info;
    string fname = path + "temp_file.txt";
    bool path_access = stat(path.c_str(), &info) == 0;
    if(path_access && file_touch(fname)){
        return file_delete(fname);
    }
    return false;
}

int get_pending_observations_size(){
    vector< pair<long int, string> > file_list;
    string observations_path = INT_COMPLETE_OBS_PATH;
    
    //check if sdcard path is accessible check inertial_obs path
    if( !is_path_healthy(observations_path) ) {
        string str_msg = "File read error. Possible case of internal bad blocks!!";
        nd_service_obj->send_err_msg(SM_E_UPLD_FILE_READ_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
    }
    get_files(observations_path.c_str(), file_list, ".json");
    get_files(observations_path.c_str(), file_list, ".zip");
    
    LOG_I(TAG, "pending obs size: %d", file_list.size());
    return file_list.size();
}

void cleanup_old_observations_quota(vector< pair<long int, string> > &file_list){
    int total_size = 0;
    LOG_I(TAG,"checking observation files older than a day to recover disk space");
    for(unsigned i = 0; i<file_list.size(); i++) {
        pair<long int, string> p = file_list[i];
        int obs_file_size = file_size(p.second);
        bool removed = false;
        if((total_size + obs_file_size) >= OBS_FOLDER_SIZE || i > OBS_FILE_LIMIT){
            removed = file_delete(p.second);
            if(p.second.find("json")!= string::npos || p.second.find("zip")!= string::npos){
                send_obs_gen_message_healthstats(p.second, -1, get_system_time(), "fail", "buffer crossed");
                check_delete_driver_image_for_obs(p.second);
            }
        }
        if(!removed){
            total_size = total_size + obs_file_size;
        }
    }
}

void cleanup_old_observations(){
    vector< pair<long int, string> > file_list;
    string observations_path = INT_COMPLETE_OBS_PATH;
    get_files(observations_path.c_str(), file_list, ".json");
    get_files(observations_path.c_str(), file_list, ".zip");
    sort(file_list.rbegin(), file_list.rend());
    vector< pair<long int, string> > file_list_jpg;
    // Done to sort jpgs individually so deletion will clear jpgs first
    get_files(observations_path.c_str(), file_list_jpg, ".jpg");
    sort(file_list_jpg.begin(), file_list_jpg.end());
    LOG_I(TAG, "json vector size: %d", file_list.size());
    LOG_I(TAG, "jpeg vector size: %d", file_list_jpg.size());
    file_list.reserve(file_list.size()+file_list_jpg.size());
    file_list.insert(file_list.end(),file_list_jpg.begin(),file_list_jpg.end());
    cleanup_old_observations_quota(file_list);
}

static int get_hdmaps_mode () {
    Config_parser c(BAGHEERACONFIG_INI);
    bool val_overridden = false;
    int hdmaps_mode_enabled = 1;

    if (!c.getParseStatus ()) {
        LOG_E (TAG, "Error parsing bagheera_config");
        return hdmaps_mode_enabled;
    }
    if("true" == c.getConfig("hdmaps_mode","enable","false", true, val_overridden)) {
        hdmaps_mode_enabled = 1;
        LOG_I (TAG, "HDMaps mode enabled");
    }
    else {
        hdmaps_mode_enabled = 0;
    }

    return hdmaps_mode_enabled;
}

char* create_log_upload_header(string zip_path, bool no_logs = false) {
    char* req_params = NULL;
    json_t *root = json_object();
    string zip_checksum = "";

    string auth_key = "";
    string auth_value = "";
    bool header_status = get_auth_key_and_value(auth_key, auth_value);
    if(!header_status) {
        LOG_E(TAG, "Corrupted jwt, Not connecting to cloud !");
        json_decref(root);
        return NULL;
    }

    if(!no_logs) {
        if(calculate_md5sum(zip_path, zip_checksum)) {
            json_object_set_new( root, "checksum", json_string(zip_checksum.c_str()));
        }
        else {
            LOG_E(TAG, "Failed to get checksum for file %s", zip_path.c_str());
        }
    }

    json_object_set_new( root, "device_id", json_string(device_id.c_str()) );
    json_object_set_new( root, "deviceversion", json_string(curr_version.c_str()) );
    json_object_set_new( root, auth_key.c_str(), json_string(auth_value.c_str()) );
    json_object_set_new( root, "session_id", json_string(session_id.c_str()) );
    json_object_set_new( root, "type", json_string(device_type.c_str()) );

    req_params = json_dumps(root, 0);
    if(req_params == NULL){
        LOG_E(TAG,"log upload header creation failed.");
    }
    json_decref(root);
    return req_params;
}

char* create_obs_upload_header(string zip_path){
    char* req_params = NULL;
    json_t *root = json_object();
    string zip_checksum = "";
    if(!calculate_md5sum(zip_path, zip_checksum)) {
        LOG_E(TAG, "Failed to get checksum for file %s", zip_path.c_str());
    }
    string auth_key = "";
    string auth_value = "";
    bool header_status = get_auth_key_and_value(auth_key, auth_value);
    if(!header_status) {
        LOG_E(TAG, "Corrupted jwt, Not connecting to cloud !");
        json_decref(root);
        return NULL;
    }

    int hdmaps_mode = get_hdmaps_mode();
    json_object_set_new( root, "device_id", json_string(device_id.c_str()) );
    json_object_set_new( root, "deviceversion", json_string(curr_version.c_str()) );
    json_object_set_new( root, "checksum", json_string(zip_checksum.c_str()));
    json_object_set_new( root, "type", json_string(device_type.c_str()) );
    json_object_set_new( root, auth_key.c_str(), json_string(auth_value.c_str()) );
    json_object_set_new( root, "session_id", json_string(session_id.c_str()) );
    json_object_set_new( root, "isAllData", json_string("false") );
    json_object_set_new( root, "hdmaps_mode", json_integer(hdmaps_mode) );
    req_params = json_dumps(root, 0);
    if(req_params == NULL){
        LOG_E(TAG,"obs upload header creation failed.");
    }
    json_decref(root);
    return req_params;
}

char* create_sign_crop_upload_payload(string sign_crop_sevenz_filename, string drp, string jobid, string status, int64_t timestamp, string error_msg){
    char* req_params = NULL;
    json_t *root = json_object();
    if (root == NULL) {
        LOG_E(TAG, "Failed to create json object for sign crop upload payload.");
        return NULL;
    }

    json_object_set_new( root, "jobId", json_string(jobid.c_str()) );
    json_object_set_new( root, "filename", json_string(sign_crop_sevenz_filename.c_str()) );
    json_object_set_new( root, "status", json_string(status.c_str()) );
    json_object_set_new( root, "drp", json_string(drp.c_str()) );
    json_object_set_new( root, "timestamp", json_integer((json_int_t)timestamp) );
    json_object_set_new( root, "error", json_string(error_msg.c_str()) );
        
    req_params = json_dumps(root, 0);
    if(req_params == NULL){
        LOG_E(TAG,"sign crops upload payload creation failed.");
    }

    json_decref(root);
    
    return req_params;
}

bool get_filename_from_path(string filename, string& filename_no_path){
    size_t pos = filename.find_last_of ('/');
    if(pos != string::npos) {
        filename_no_path = filename.substr(pos+1);
        return true;
    }
    return false;
}

void remove_unoperated_obs(){
    string command_del_all = "rm -rf /home/ubuntu/.nddevice/unoperated_obs/*";
    LOG_I(TAG, "delete all from folder command %s", command_del_all.c_str());
    system(command_del_all.c_str());
}


bool prepare_obs_for_zip(string source, string dest, string& dest_filename_json){
    // Decrypt a file in internal & Unzip file in memory
    dest_filename_json = "";
    string dest_folder = "";
    string dest_filename = "";
    string md5sum_obs = "";
    if(!get_folder_file_names(dest, dest_folder, dest_filename)){
        LOG_E(TAG, "Failed to extract folder file names. Discarding obs %s", source.c_str());
        return false;
    }
    if(!check_create_dir(dest_folder)){
        LOG_E(TAG, "Failed to create folder. Discarding obs %s", source.c_str());
        return false;
    }
    if (ND_AUTH_SUCCESS != nd_file_reoperate_to_file(source.c_str(), dest.c_str())){
        LOG_E(TAG, "Failed to reoperate obs file");
        return false;
    }
    if(!calculate_md5sum(dest, md5sum_obs)){
        LOG_E(TAG, "Failed to get checksum for file %s", dest.c_str());
        string str_msg = "File read error. Possible case of internal bad blocks!!";
        nd_service_obj->send_err_msg(SM_E_UPLD_FILE_READ_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
        dest_filename_json = "";
        return false;
    }
    LOG_I(TAG, "md5sum: %s", md5sum_obs.c_str());
    string new_name = check_observations_complete(dest_filename, md5sum_obs);
    if(new_name.length() <=0){
        dest_filename_json = "";
        return false;
    }
    if (!remove_extension_from_session(new_name, dest_filename_json)){
        LOG_E(TAG, "Invalid filename. Discarding obs %s", dest_filename.c_str());
        dest_filename_json = "";
        return false;
    }
    dest_filename_json = dest_filename_json + ".json";
    LOG_I(TAG, "dest_filename_json: %s", dest_filename_json.c_str());

    std::map<string, string> alternative_name_map = { {"summary_LD.json", dest_filename_json} };
    try{
        Unzipper unzipper(dest);
        bool unzip_status = unzipper.extract(dest_folder, alternative_name_map);
        if(!unzip_status){
            LOG_E(TAG, "Unzip failed. Discarding obs %s", dest_filename.c_str());
            dest_filename_json = "";
            unzipper.close();
            return false;
        }
        unzipper.close();
    }
    catch (const std::runtime_error& re) {
        LOG_E(TAG, "Zipper exception!! File read error: %s", re.what());
        nd_service_obj->send_err_msg(SM_E_UPLD_ZIP_READ_FAIL, NDService::UNUSED_ERR_AUX_CODE, "Zipper exception!! File read error");
        dest_filename_json = "";
        return false;
    }
    catch(...){
        LOG_E(TAG, "Zipper exception!! error: Unknown. Check syslog!!");
        nd_service_obj->send_err_msg(SM_E_UPLD_ZIP_READ_FAIL, NDService::UNUSED_ERR_AUX_CODE, "Zipper exception!! error: Unknown. Check syslog!!" );
        dest_filename_json = "";
        return false;
    }
    dest_filename_json = dest_folder + "/" + dest_filename_json;
    file_fd_sync(dest_filename_json);
    return true;
}

bool check_duplicate_obs(vector<string> &file_list, string file_name){
    for (auto i = file_list.begin(); i != file_list.end(); ++i){
        string processed_obs = *i;
        if (processed_obs.find(file_name) != std::string::npos) {
            return true;
        }
    }
    return false;
}

void send_obs_gen_message_healthstats(string meta_fname, int64_t upload_add, int64_t upload_del, string status, string reason)
{
    json_t *root = json_object();
    json_t *observation = json_object();
    json_t *upload_info = json_object();
    string folder = "", fname = "", session = "";
    char* req_params = NULL;
    if(!get_folder_file_names(meta_fname, folder, fname)){
        LOG_E(TAG, "Failed to get folder and file names from given path");
    }
    if(!get_session_name_from_string(fname, session)){
        LOG_E(TAG, "Invalid filename: %s",fname.c_str());
    }
    json_object_set_new( root, "session", json_string(session.c_str()) );
    if(upload_add != -1){
        json_object_set_new( upload_info, "upload_add", json_integer(upload_add) );
    }
    if(upload_del != -1){
        json_object_set_new( upload_info, "upload_del", json_integer(upload_del) );
    }
    json_object_set_new( upload_info, "status", json_string(status.c_str()) );
    json_object_set_new( upload_info, "reason", json_string(reason.c_str()) );
    json_object_set_new( observation, "upload_info", upload_info );
    json_object_set_new( root, "observation", observation );
    req_params = json_dumps(root, 0);
    if(req_params == NULL){
        LOG_E(TAG,"JSON creation failed for HS obs gen message");
        json_decref(root);
        return;
    }
    int length = strlen(req_params);
    if(!nd_service_obj->send_msg_healthstats(req_params, length)) {
        LOG_E(TAG,"Failed to send HS obs gen message");
    }
    json_decref(root);
    free(req_params);
}

bool create_observations_zip(vector< pair<long int, string> > &file_list, string obs_zip_path){
    int zip_file_count = 0;
    int total_json_size = 0;
    bool zip_created = false;
    // TODO: cleanup previous uncleaned state of decrypted files
    string unoperated_obs_dest_path = "/home/ubuntu/.nddevice/unoperated_obs/";
    remove_unoperated_obs();
    try{
        // Reversed sorting of the obs batch s.t new file can be picked in case of duplicate obs
        sort(file_list.rbegin(), file_list.rend());
        vector<string> processed_obs_list;
        vector<string> processed_driver_img_list;
        bool zip_add_status = false;
        stringstream sevenz_cmd_stream;
        string sevenz_arg = "7za a ";
        string response_sz="";
        sevenz_cmd_stream <<  sevenz_arg << obs_zip_path;


        vector< pair<long int, string> >::iterator iter;
        iter=file_list.begin();
        while(iter != file_list.end()) {
            pair<long int, string> p = *iter;
            string md5sum_from_obs_file = "";
            string new_name = "";
            string obs_input_no_path = "";
            if(!get_filename_from_path(p.second, obs_input_no_path)){
                string reason = "skipping file as cannot extract name from path";
                LOG_I(TAG, "%s: %s",reason.c_str(), p.second.c_str());
                iter = file_list.erase(iter);
                file_delete(p.second);
                send_obs_gen_message_healthstats(p.second, -1, get_system_time(), "fail", reason);
                continue;
            }
            string unoperated_obs_dest = unoperated_obs_dest_path + obs_input_no_path;
            bool zip_extraction = false;

            //check with CB for uploadable status. Call prepare_obs_for_zip() only if uploadable
            // CB check is required for DRP not privacy.
            string vid_file = obs_input_no_path.substr(0, obs_input_no_path.find("_y_")) ;;
            vid_file = vid_file + "_y.mp4" ;
            cb_response_to_upl_query_t cb_response = get_upload_status_from_cb(vid_file, UPLOAD_FILE_TYPE_OBSERVATION, cb_query_id++);
            if(cb_response.upload == true) {
                zip_extraction = prepare_obs_for_zip(p.second, unoperated_obs_dest, new_name);
            }
            // If CB is not ready, reason will be CB_UPLOAD_CHECK_QUERY_FAILED
            // If CB DB is not synced, reason will be CB_FILE_AVAILABLE_BUT_NOT_IN_DB, CB needs the DB for DRP check as well
            else if(cb_response.reason == CB_UPLOAD_CHECK_QUERY_FAILED || cb_response.reason == CB_FILE_AVAILABLE_BUT_NOT_IN_DB) {
                LOG_W(TAG, "Not calling prepare_obs_for_zip() as CB not ready");
                iter = file_list.erase(iter);
                continue;
            }
            else {
                LOG_W(TAG, "Not calling prepare_obs_for_zip() based on after checking with CB. obs_file: %s, upload: %d, reason: %d",
                        obs_input_no_path.c_str(), cb_response.upload, cb_response.reason);
            }

            if(!zip_extraction){
                string reason = "skipping file as vectorstream couldn't be created";
                LOG_I(TAG, "%s: %s", reason.c_str(), p.second.c_str());
                iter = file_list.erase(iter);
                file_delete(p.second);
                send_obs_gen_message_healthstats(p.second, -1, get_system_time(), "fail", reason);
                continue;
            }
            // To check if files have md5sum & compare if the file is copied successfully, else delete it.
            if(new_name.length() <=0){
                string reason = "skipping file as md5sum not matched";
                LOG_I(TAG, "%s: %s",reason.c_str(), p.second.c_str());
                iter = file_list.erase(iter);
                file_delete(p.second);
                send_obs_gen_message_healthstats(p.second, -1, get_system_time(), "fail", reason);
                continue;
            }
            // TODO: check for correct file names after 7z is created

            bool duplicate = check_duplicate_obs(processed_obs_list, new_name);
            std::ifstream obs_fs(new_name);
            bool obs_file_accessible = obs_fs.good();
            obs_fs.close();

            if(!duplicate && obs_file_accessible){
                LOG_I(TAG,"adding obs json zip cmd: %s",new_name.c_str());

                sevenz_cmd_stream << " " << new_name;

                processed_obs_list.push_back(new_name);
                total_json_size += get_file_size(new_name);
            }
            else{
                string reason = "duplicate obs or zip add failure";
                LOG_E(TAG,"Obs status: %s, Duplicate: %d, Accessible %d, deleting file...", reason.c_str(), duplicate, obs_file_accessible);
                iter = file_list.erase(iter);
                nd_service_obj->send_err_msg(SM_E_UPLD_ZIP_READ_FAIL, NDService::UNUSED_ERR_AUX_CODE, "7z error");
                file_delete(new_name);
                file_delete(p.second);
                send_obs_gen_message_healthstats(p.second, -1, get_system_time(), "fail", reason);
                continue;
            }
            string driver_image_path = get_driver_image_for_obs(new_name);
            if (get_file_size(driver_image_path) > 0){
                std::ifstream driver_img_fs(driver_image_path);
                bool driver_img_file_accessible = driver_img_fs.good();
                driver_img_fs.close();
                if(driver_img_file_accessible){
                    LOG_I(TAG,"adding driver image to zip cmd: %s",driver_image_path.c_str());
                    sevenz_cmd_stream << " " << driver_image_path;
                    zip_file_count++;
                    processed_driver_img_list.push_back(driver_image_path);
                }
            }
            zip_file_count++;
            iter++;
        }
        LOG_I(TAG,"Creating observations zip file at once!");
        string sevenz_cmd = sevenz_cmd_stream.str();
        LOG_I(TAG, "Obs 7z cmd: %s", sevenz_cmd.c_str());
        printf("Obs 7z cmd: %s", sevenz_cmd.c_str());

        uint64_t obs_archive_starttime = get_system_time();
        uint64_t obs_archive_endtime = 0;
        int resp_code = -1;

        if(zip_file_count==0){
            file_delete(obs_zip_path);
            LOG_I(TAG,"no files added to zip. Deleting!");
            LOG_E(TAG, "Obs 7z not created");
            nd_service_obj->send_err_msg(SM_E_UPLD_ZIP_READ_FAIL, NDService::UNUSED_ERR_AUX_CODE, "Obs 7z not created");
        } else {
            zip_add_status = system_execute_with_resp_and_resp_code("7z obs", sevenz_cmd, response_sz, resp_code);

            obs_archive_endtime = get_system_time();

            LOG_I(TAG, "7z resp: status:%d, message: %s", zip_add_status, response_sz.c_str());

            if(zip_add_status || resp_code%256 == 0){
                //send HS msg for obs pass
                for (const std::string& file : processed_obs_list) {
                    usleep(20000);  // Sleep for 20 milliseconds (20,000 microseconds)
                    LOG_I(TAG,"Sending obs archive pass to HS: %s",file.c_str());
                    send_obs_gen_message_healthstats(file, get_system_time(), -1, "pass", "file added to zip");
                }
            }
            else {
                const string reported_reason = "Obs 7z creation failed";
                const string hs_reason = "zip add failure";

                LOG_E(TAG, reported_reason.c_str());
                nd_service_obj->send_err_msg(SM_E_UPLD_ZIP_READ_FAIL, NDService::UNUSED_ERR_AUX_CODE, reported_reason);

                //delete processed_obs_list
                for (const std::string& file : processed_obs_list) {
                    LOG_E(TAG,"Deleting JSON file %s", file.c_str());
                    file_delete(file);
                }
                //delete processed_driver_img_list
                for (const std::string& file : processed_driver_img_list) {
                    LOG_E(TAG,"Deleting img file %s", file.c_str());
                    file_delete(file);
                }
                //send HS msg for obs failure
                for (const std::pair<long int, const string>& p : file_list) {
                    usleep(20000);  // Sleep for 20 milliseconds (20,000 microseconds)
                    LOG_E(TAG,"Sending obs archive failure to HS %s", p.second.c_str());
                    send_obs_gen_message_healthstats(p.second, -1, get_system_time(), "fail", hs_reason);
                }
            }
        }
        zip_created = file_is_present(obs_zip_path);
        if(zip_created){
            LOG_I(TAG,"Obs zip file created!");
            LOG_I(TAG,"Archived obs size:  %0.2f kB, Time taken: %llu ms, File count: %d, Total JSON size %0.2f kB",
                    (float)(get_file_size(obs_zip_path) / 1000),
                    (obs_archive_endtime - obs_archive_starttime),
                    zip_file_count,
                    (float)(total_json_size/1000));
            file_fd_sync(obs_zip_path);
        }
        //remove the obs file in both the cases: successfully added to zip or or failed to zip
        remove_files_from_disk(file_list);
        remove_unoperated_obs();
    }
    catch (const std::runtime_error& re) {
        LOG_E(TAG, "Zipper exception!! Internal read error: %s", re.what());
        nd_service_obj->send_err_msg(SM_E_UPLD_ZIP_READ_FAIL, NDService::UNUSED_ERR_AUX_CODE, "Zipper exception!! Internal read error" );
    }
    catch(...){
        LOG_E(TAG, "Zipper exception!! error: Unknown. Check syslog!!");
        nd_service_obj->send_err_msg(SM_E_UPLD_ZIP_READ_FAIL, NDService::UNUSED_ERR_AUX_CODE, "Zipper exception!! error: Unknown. Check syslog!!" );
    }
    return zip_created;
}

// Creates a 7z file containing all the requested and available sign crops
bool create_sign_crops_zip(vector<string> &file_list, string sign_crop_sevenz_path, string& error_msg){
    bool zip_created = false;

    try{
        if (file_list.size() == 0) {
            LOG_I(TAG, "No sign crops to add to zip! No zip created");
            return false;
        }
        
        // Prepare the full 7z command
        stringstream sevenz_cmd_stream;
        sevenz_cmd_stream << "7za a " << sign_crop_sevenz_path;

        // Add all file paths to the 7z command
        for (const auto& sign_crop_filename : file_list) {
            string sign_crop_file_path = SIGN_CROP_OUTPUT_PATH + sign_crop_filename;
            sevenz_cmd_stream << " " << sign_crop_file_path;
        }

        string sevenz_cmd = sevenz_cmd_stream.str();
        LOG_I(TAG, "7z cmd: %s", sevenz_cmd.c_str());

        // Execute the 7z command
        string response_sz = "";
        bool zip_add_status = system_execute_with_resp("7z sign crops", sevenz_cmd, response_sz);
        LOG_I(TAG, "7z resp: status:%d, message: %s", zip_add_status, response_sz.c_str());

         // Check the 7z response for any errors related to specific files
        if (!zip_add_status) {
            error_msg = "zip add failure";
            LOG_E(TAG, "7z command encountered errors: %s", response_sz.c_str());
            nd_service_obj->send_err_msg(SM_E_UPLD_ZIP_READ_FAIL, NDService::UNUSED_ERR_AUX_CODE, "7z encountered errors while creating zip.");
        }

        zip_created = file_is_present(sign_crop_sevenz_path);
    }
    catch (const std::runtime_error& re) {
        LOG_E(TAG, "Zipper exception!! Internal read error: %s", re.what());
        nd_service_obj->send_err_msg(SM_E_UPLD_ZIP_READ_FAIL, NDService::UNUSED_ERR_AUX_CODE, "Zipper exception sign crop!! Internal read error" );
    }
    catch(...){
        LOG_E(TAG, "Zipper exception!! error: Unknown. Check syslog!!");
        nd_service_obj->send_err_msg(SM_E_UPLD_ZIP_READ_FAIL, NDService::UNUSED_ERR_AUX_CODE, "Zipper exception sign crop!! error: Unknown. Check syslog!!" );
    }
    return zip_created;
}

bool create_observations_batch(string observations_path, vector< pair<long int, string> > &file_list){
    get_files(observations_path.c_str(), file_list, ".json");
    get_files(observations_path.c_str(), file_list, ".zip");
    sort(file_list.begin(), file_list.end());
    if(file_list.size() > OBS_FILE_COUNT){
        file_list.erase(file_list.begin()+OBS_FILE_COUNT, file_list.end());
    }
    return file_list.size()>0;
}

int upload_observations(string obs_zip_path){
    char* req_params = NULL;
    string end_point = server_url+"/"+api_version+"/upload/observations";
    int obs_upload_status = -1;
    if(!file_is_present(obs_zip_path)){
        LOG_E(TAG,"Obs zip not present. Reached here, something wrong!");
        return obs_upload_status;
    }
    req_params = create_obs_upload_header(obs_zip_path);
    if(req_params == NULL){
        LOG_E(TAG,"JSON creation failed for Observations");
        return 1;
    }
    LOG_I(TAG,"Uploading observations... ");
    LOG_I(TAG,"Observations size: %d", file_size(obs_zip_path));
    multipart_curl_response_t multipart_curl_response;
    obs_upload_status = prepare_multipart_form_curl_request(end_point.c_str(), req_params, obs_zip_path.c_str(), multipart_curl_response);
    LOG_I(TAG,"Time taken for obs upload call: %lld ms", multipart_curl_response.end_time - multipart_curl_response.start_time);
    if(obs_upload_status == CALL_SUCCESS){
        int remove_status = file_delete(obs_zip_path);
        LOG_I(TAG,"File uploaded. Deleting %d",remove_status);
    }
    free(req_params);
    return obs_upload_status;
}

int prepare_upload_observations(req_upload_msg_t *msg){
    vector< pair<long int, string> > file_list;
    string observations_path = INT_COMPLETE_OBS_PATH;
    string obs_zip_path = OBS_ZIP_PATH;
    string obs_sevenz_path = OBS_7Z_PATH;
    cleanup_old_observations();
    int obs_upload_status = CALL_INVALID;
    bool new_created_zip = false;
    obs_zip_path = file_is_present(obs_zip_path) ? obs_zip_path : obs_sevenz_path;

    LOG_I(TAG,"retry_count %d", msg->retry_count);
    if(!file_is_present(obs_zip_path)){
        data_record_status_db data_recording_status;

        if(!get_data_record_status_db(data_recording_status)) {
            LOG_E(TAG, "handle_data_record_status: get_data_record_status failed for DB value");
        }
        if(!create_observations_batch(observations_path, file_list)){
            // check obs temp folder before declaring failure
            string str_msg = "not creating observations.zip as there are no observation files...";
            if(data_recording_status.enabled == true)
            {
                LOG_E(TAG, str_msg.c_str() );
                nd_service_obj->send_err_msg(SM_E_UPLD_NO_FILE, NDService::UNUSED_ERR_AUX_CODE, str_msg );
            }
            return obs_upload_status;
        }
        new_created_zip = create_observations_zip(file_list, obs_zip_path);
        if(!new_created_zip){
            obs_upload_status = CALL_FAILED;
            return obs_upload_status;
        }
    }
    else{
        LOG_I(TAG,"Uploading old observations zip file!");
    }
    // TODO: Give 7z or zip to upload
    obs_upload_status = upload_observations(obs_zip_path);

    return obs_upload_status;
}

int upload_ea_imgs(vector<ea_cache_file_data_t>& ea_imgs_cache, bool& zip_present, string& ea_imgs_zip_path_to_upload){
    // test code
    // string temp_server = "https://idms-qa8.netradyne.com/restserver/api";
    // string end_point = temp_server+"/"+api_version+"/upload/images/device";
    
    string end_point = server_url+"/"+api_version+"/upload/images/device";
    int ea_imgs_upload_status = CALL_FAILED;

    char* payload_params = create_ea_zip_upload_payload(ea_imgs_cache);
    if(payload_params == NULL){
        LOG_E(TAG,"JSON creation failed for EA images");
        return ea_imgs_upload_status;
    }

    multipart_curl_response_t multipart_curl_response;
    if(zip_present){
        LOG_I(TAG,"Uploading EA images zip of size: %0.2f kB...", (float)(get_file_size(ea_imgs_zip_path_to_upload) / 1000.0));
        ea_imgs_upload_status = prepare_multipart_form_curl_request(end_point.c_str(), payload_params, ea_imgs_zip_path_to_upload.c_str(), multipart_curl_response);
    }else{
        LOG_I(TAG,"Uploading just the EA payload as zip is not available");
        ea_imgs_upload_status = prepare_multipart_form_curl_request(end_point.c_str(), payload_params, NULL, multipart_curl_response);
    }

    LOG_I(TAG,"Time taken for EA images upload call: %lld ms", multipart_curl_response.end_time - multipart_curl_response.start_time);
    
    free(payload_params);
    return ea_imgs_upload_status;
}

int prepare_upload_ea_images(req_upload_msg_t *msg){

    vector<ea_cache_file_data_t> ea_imgs_cache;

    bool query_status = false; 

    query_status = get_db_ea_imgs_in_batch(ea_db_handle, EA_IMGS_BATCH_COUNT, ea_imgs_cache);
    if(!query_status){
        string str_msg = "EA imgs get from table failed";
        LOG_E(TAG, str_msg.c_str());
        nd_service_obj->send_err_msg(SM_E_UPLD_EA_IMGS_DB_GET_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg);
        return CALL_FAILED;
    }

    int size = ea_imgs_cache.size();
    if(size == 0){
        LOG_I(TAG, "No EA images to upload in UPLOADER_EA_IMGS table. Exiting!");
        return CALL_CANCELLED;
    }

    string unoperated_ea_img_dest_path = "/home/ubuntu/.nddevice/unoperated_ea/";
    if(!check_create_dir(unoperated_ea_img_dest_path)){
        string reason = "check_create_dir failed for unoperated_ea_img_dest_path";
        LOG_E(TAG, "%s: %s", reason.c_str(), unoperated_ea_img_dest_path.c_str());
        nd_service_obj->send_err_msg(SM_E_UPLD_EA_UNOPERATED_PATH_CREATION_FAIL, NDService::UNUSED_ERR_AUX_CODE, reason);
        return CALL_FAILED;
    }

    LOG_I(TAG,"retry_count %d", msg->retry_count); 
    
    pair<bool, string> zip_details = create_ea_imgs_zip(ea_imgs_cache, unoperated_ea_img_dest_path);
    bool zip_present = zip_details.first;
    string ea_imgs_zip_path_to_upload = zip_details.second; //path of latest zip file

    int ea_imgs_upload_status = upload_ea_imgs(ea_imgs_cache, zip_present, ea_imgs_zip_path_to_upload);
    if(ea_imgs_upload_status == CALL_SUCCESS){
        LOG_I(TAG, "EA images zip file upload succeeded!");
        send_ea_zip_details_to_hs(ea_imgs_cache, ea_imgs_zip_path_to_upload);

        delete_files_from_disk(ea_imgs_cache);

        query_status = delete_db_ea_imgs_in_batch(ea_db_handle, ea_imgs_cache);
        if(query_status){
            LOG_I(TAG, "Deleted EA images after upload from table");
        }else{
            string str_msg = "EA imgs deletion from table failed";
            LOG_E(TAG, str_msg.c_str());
            nd_service_obj->send_err_msg(SM_E_UPLD_EA_IMGS_DB_DELETE_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg);
        }
    }else{
        LOG_I(TAG, "EA images zip file upload didn't succeed");
    }

    if(zip_present){
        int remove_status = file_delete(ea_imgs_zip_path_to_upload);
        LOG_I(TAG, "Deleting ea zip file! remove_status:  %d",remove_status);
    }
    ea_imgs_zip_path_to_upload = ""; // Reset the zip path after upload
    return ea_imgs_upload_status; 
}

void write_to_file(string fname, string message){
    ofstream myfile (fname.c_str());
    if(myfile.is_open()){
        myfile << message;
        myfile.close();
    }
    if(chmod(fname.c_str(), S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH) == -1) {
        LOG_E(TAG, "Failed to change file permissions :: %s" , fname.c_str());
    }
    file_fd_sync(fname);
}

void send_alert_upload_info_healthstats(string session_name, uint64_t upload_add, int64_t upload_del) {
    json_t *root = json_object();
    char* req_params = NULL;
    json_t *alert_info = json_object();
    json_object_set_new( root, "session", json_string(session_name.c_str()) );
    
    
    if (upload_add != -1) {
        json_object_set_new( alert_info, "uploadadd", json_integer(upload_add) );
    }
    if (upload_del != -1) {
        json_object_set_new( alert_info, "uploaddel", json_integer(upload_del));
    }

    json_object_set_new( root, "alert_info", alert_info );
    req_params = json_dumps(root, 0);
    if(req_params == NULL){
        LOG_E(TAG,"JSON creation failed for HS upload info message");
        json_decref(root);
        return;
    }
    LOG_I(TAG, "sending msg to hs-: %s", req_params);
    int length = strlen(req_params);
    nd_service_obj->send_msg_healthstats(req_params, length);
    json_decref(root);
    free(req_params);
}

void send_lla_info_history_healthstats(string video_file_name, string uuid, int64_t starttime, int64_t endtime, int status, int retry_count) {


    json_t *root = json_object();
    char* req_params = NULL;
    json_t *alert_info = json_object();
    //json_t *array = json_array();
    json_t *element = json_object();

    stringstream ss;
    ss << "lla_" << uuid << "_" << retry_count;
    string lla_key = ss.str();
    LOG_I(TAG, "lla_key = %s", lla_key.c_str());

    string session_name = "";
    if(!get_session_name_from_string(video_file_name, session_name)){
        LOG_E(TAG, "Invalid filename: %s",video_file_name.c_str());
    }

    json_object_set_new( root, "session", json_string(session_name.c_str()) );

    json_object_set_new( element, "uploadstart",  json_integer(starttime));
    json_object_set_new( element, "uploadend",  json_integer(endtime));

    if(status == CALL_SUCCESS) {
        json_object_set_new( element, "status", json_string("pass") );
    }else{
        json_object_set_new( element, "status", json_string("fail") );
    }

    json_object_set_new( element, "status_message", json_string(lla_status_msg.c_str()) );
    json_object_set_new( element, "status_code", json_integer(lla_status_code) );
    json_object_set_new( element, "retry_count", json_integer(retry_count) );

    //json_array_append_new(array, element);

    json_object_set_new( alert_info, lla_key.c_str(), element );
    json_object_set_new( root, "alert_info", alert_info );
    req_params = json_dumps(root, 0);
    if(req_params == NULL){
        LOG_E(TAG,"JSON creation failed for HS lla history message");
        json_decref(root);
        return;
    }
    LOG_I(TAG, "sending msg to hs--: %s", req_params);
    int length = strlen(req_params);
    nd_service_obj->send_msg_healthstats(req_params, length);
    json_decref(root);
    free(req_params);
}

void send_lla_add_del_history_healthstats(string video_file_name, string uuid, int64_t lla_add, int64_t lla_del, int retry_count) {

    json_t *root = json_object();
    char* req_params = NULL;
    json_t *alert_info = json_object();
    json_t *element = json_object();


    stringstream ss;
    ss << "lla_" << uuid << "_" << retry_count;
    string lla_key = ss.str();
    LOG_I(TAG, "lla_key= %s", lla_key.c_str());


    string session_name = "";
    if(!get_session_name_from_string(video_file_name, session_name)){
        LOG_E(TAG, "Invalid filename: %s",video_file_name.c_str());
    }
    json_object_set_new( root, "session", json_string(session_name.c_str()) );

    if(lla_add != -1) {
        json_object_set_new( element, "uploadadd", json_integer(lla_add) );
    }
    if(lla_del != -1) {
        json_object_set_new( element, "uploaddel", json_integer(lla_del) );
    }

    json_object_set_new( alert_info, lla_key.c_str(), element );
    json_object_set_new( root, "alert_info", alert_info );
    req_params = json_dumps(root, 0);
    if(req_params == NULL){
        LOG_E(TAG,"JSON creation failed for HS lla add-del message");
        json_decref(root);
        return;
    }
    LOG_I(TAG, "sending msg to hs--: %s", req_params);
    int length = strlen(req_params);
    nd_service_obj->send_msg_healthstats(req_params, length);
    json_decref(root);
    free(req_params);
}


void send_alert_info_upload_history_healthstats(string video_file_name, int64_t starttime, int64_t endtime, int status, int retry_count) {
    json_t *root = json_object();
    char* req_params = NULL;
    json_t *alert_info = json_object();
    json_t *element = json_object();

    stringstream ss;
    ss << retry_count;
    string retry_count_str = ss.str();
    string key_str = "upload_history_" + retry_count_str;
    const char* upload_history_key = key_str.c_str();
    LOG_I(TAG, "upload_history_key= %s", upload_history_key);

    string session_name = "";
    if(!get_session_name_from_string(video_file_name, session_name)){
        LOG_E(TAG, "Invalid filename: %s",video_file_name.c_str());
    }
    json_object_set_new( root, "session", json_string(session_name.c_str()) );

    json_object_set_new( element, "uploadstart",  json_integer(starttime));
    json_object_set_new( element, "uploadend",  json_integer(endtime));

    if(status == CALL_SUCCESS){
        json_object_set_new( element, "status", json_string("pass") );
    }else{
        json_object_set_new( element, "status", json_string("fail") );
    }

    json_object_set_new( element, "status_message", json_string(upload_history_status_msg.c_str()) );
    json_object_set_new( element, "status_code", json_integer(upload_history_status_code) );
    json_object_set_new( element, "retry_count", json_integer(retry_count) );

    json_object_set_new( alert_info, upload_history_key, element );
    json_object_set_new( root, "alert_info", alert_info );
    req_params = json_dumps(root, 0);
    if(req_params == NULL){
        LOG_E(TAG,"JSON creation failed for HS upload_history message");
        json_decref(root);
        return;
    }
    LOG_I(TAG, "sending msg to hs--: %s", req_params);
    int length = strlen(req_params);
    nd_service_obj->send_msg_healthstats(req_params, length);
    json_decref(root);
    free(req_params);
}

void send_upload_event_message_healthstats(string video_file_name, int64_t starttime, int64_t endtime, int status){
    json_t *root = json_object();
    char* req_params = NULL;
    json_t *upload_info = json_object();
    size_t mp4_offset = video_file_name.find(MP4_EXT);
    video_file_name = video_file_name.replace(mp4_offset, MP4_EXT.length(), "");
    json_object_set_new( root, "session", json_string(video_file_name.c_str()) );
    json_object_set_new( upload_info, "starttime", json_integer(starttime) );
    json_object_set_new( upload_info, "endtime", json_integer(endtime));
    if(status == CALL_SUCCESS){
        json_object_set_new( upload_info, "result", json_string("pass") );
    }else{
        json_object_set_new( upload_info, "result", json_string("fail") );
    }
    stringstream ss;
    ss << status;
    json_object_set_new( upload_info, "reason", json_string(ss.str().c_str()) );
    json_object_set_new( root, "upload_info", upload_info );
    req_params = json_dumps(root, 0);
    if(req_params == NULL){
        LOG_E(TAG,"JSON creation failed for HS upload_event message");
        json_decref(root);
        return;
    }
    LOG_I(TAG, "sending msg to hs: %s", req_params);
    int length = strlen(req_params);
    nd_service_obj->send_msg_healthstats(req_params, length);
    json_decref(root);
    free(req_params);
}

uint64_t get_log_timestamp(string log_zip_name) {
    uint64_t ts = 0;
    string ts_str;
    size_t pos_end = log_zip_name.find('_');


    if(pos_end != string::npos) {
        ts_str = log_zip_name.substr(0, pos_end);
        try {
            ts = std::stoll(ts_str) / 1000;
        }
        catch (std::exception& ex) {
            LOG_E(TAG, "get_log_timestamp :: %s", ex.what());
        }
    }
    return ts;
}


void get_logs_in_time_range(string path, vector< pair<long int, string> > &log_list_with_timestamp,
        int start_sec, int end_sec) {
    vector<string> log_list;

    if(!get_files(path, log_list)) {
        LOG_E(TAG, "Error getting log files");
        return;
    }
    for( vector<string>::iterator iter= log_list.begin(), end = log_list.end(); iter!= end; iter++ ) {
        uint64_t ts = get_log_timestamp(*iter);
        if(ts >= start_sec && ts <= end_sec) {
            log_list_with_timestamp.push_back(make_pair(ts, path + *iter));
        }
    }

}

int upload_log(string log_zip_name, upload_log_type log_type, req_upload_msg_t *msg, string job_id, bool no_logs = false) {
    char* req_params = NULL;
    int log_upload_status = -1;

    string end_point = server_url+"/"+api_version+"/upload/logs";
    stringstream query_params;

    switch(log_type) {
        case UL_CRITICAL_LOG_TIME_RANGE:
            query_params << "/?jobId=" << job_id << (no_logs ? "&jobStatus=No-Pending-Logs" : "&jobStatus=Active");
            end_point += query_params.str();
            break;
        case UL_NON_CRITICAL_LOG:
            query_params << "/?ping_id=" << msg->req_id << "&type=detailed";
            end_point += query_params.str();
            break;
        case UL_SYS_LOG:
            query_params << "/?ping_id=" << msg->req_id << "&type=system";
            end_point += query_params.str();
            break;
    }

    if(no_logs & UL_CRITICAL_LOG_TIME_RANGE == log_type){
        LOG_I(TAG,"No logs to upload, updating cloud...");
        req_params = create_log_upload_header(log_zip_name, no_logs);
        if(req_params == NULL){
            LOG_E(TAG,"request param creation failed for logs");
            return -1;
        }
        multipart_curl_response_t multipart_curl_response;
        log_upload_status = prepare_multipart_form_curl_request(end_point.c_str(), req_params, NULL, multipart_curl_response);
        LOG_I(TAG,"time taken for log upload time range call: %lld ms", multipart_curl_response.end_time - multipart_curl_response.start_time);
        free(req_params);
        return log_upload_status;
    }

    if(!file_is_present(log_zip_name)) {
        LOG_E(TAG,"log zip %s not found, something wrong!", log_zip_name.c_str());
        return log_upload_status;
    }

    req_params = create_log_upload_header(log_zip_name);
    if(req_params == NULL){
        LOG_E(TAG,"request param creation failed for logs");
        return -1;
    }

    float log_size = (float) get_file_size(log_zip_name) / 1000;

    string file_name(log_zip_name);
    size_t pos = file_name.find_last_of('/');
    if (pos != string::npos && pos + 1 < file_name.size())
    {
        file_name = file_name.substr(pos + 1);
    }
    LOG_I(TAG, "Uploading logs...  file name: %s size: %0.2fkB", file_name.c_str(), log_size);
    multipart_curl_response_t multipart_curl_response;
    log_upload_status = prepare_multipart_form_curl_request(end_point.c_str(), req_params, log_zip_name.c_str(), multipart_curl_response);
    // start time - end yime units
    LOG_I(TAG,"time taken for log upload call: %lld ms", multipart_curl_response.end_time - multipart_curl_response.start_time);
    free(req_params);

    return log_upload_status;
}

void check_and_add_additional_log(const char* path, vector< pair<long int, string> > &file_list,
        const char* extension, int start_sec, int end_sec, const char* pattern) {
    vector< pair<long int, string> > temp_list;
    get_files_by_modified_time_range_pattern(path, temp_list, extension, start_sec, end_sec, pattern);
    int additional_file_pos = -1;
    long int file_modified_time = 0;
    for(unsigned i = 0; i<temp_list.size(); i++) {
        pair<long int, string> p = temp_list[i];
        if(file_modified_time == 0 || file_modified_time > p.first) {
            additional_file_pos = i;
            file_modified_time = p.first;
        }
    }
    if(additional_file_pos != -1) {
        file_list.push_back(temp_list[additional_file_pos]);
    }

}

void get_critical_log_upload_details(req_upload_msg_t *msg,
        int& upl_start_sec, int& upl_end_sec, string& job_id) {

    json_t *root = NULL;
    json_error_t error;
    root = json_loads(msg->fname, 0, &error);
    if(root == NULL){
        LOG_E(TAG,"get_start_end_time_for_critical_log_upload:: json_loads failed!");
        LOG_E(TAG,"error: on line %d: %s", error.line, error.text);
        return;
    }

    json_t *start_time = json_object_get(root, "start_time");
    if( start_time == NULL ) {
        LOG_E(TAG, "start_time section not found in log upl msg");
        json_decref(root);
        return;
    }
    json_t *end_time = json_object_get(root, "end_time");
    if( end_time == NULL ) {
        LOG_E(TAG, "end_time section not found in log upl msg");
        json_decref(root);
        return;
    }

    json_t *job_id_json = json_object_get(root, "job_id");
    if( job_id_json == NULL ) {
        LOG_E(TAG, "job_id section not found in log upl msg");
        json_decref(root);
        return;
    }

    upl_start_sec = json_integer_value(start_time) / 1000; //convert from ms to sec
    upl_end_sec = json_integer_value(end_time) / 1000; //convert from ms to sec
    job_id = json_string_value(job_id_json);

    json_decref(root);
}


int upload_logs(req_upload_msg_t *msg, upload_log_type log_type) {
    //prepare list of 7zs & zips to upload
    vector< pair<long int, string> > file_list;
    string log_dir = "";

    //for non-critical logs, extend range by 10 mins to include all logs from requested time range
    int non_critical_end_sec = msg->end_sec + 600 ;

    //syslogs are rotated every 24 hrs or 100 MB or device reboot, So we need to include one more file from next 24 hrs
    int syslog_end_sec = msg->end_sec + (24 * 60 * 60);

    string job_id = "";
    int upl_start_sec = 0, upl_end_sec = 0;

    switch(log_type) {
        case UL_CRITICAL_LOG:
            log_dir = msg->json_fname;
            log_dir +=  "/";
            get_files(log_dir.c_str(), file_list, ".7z");
            get_files(log_dir.c_str(), file_list, ".zip");
            if(syslog_enabled)
            {
                get_files(SYS_LOG_PATH_ARCHIVED.c_str(), file_list, ".gz");
            }
            break;

        case UL_CRITICAL_LOG_TIME_RANGE:
            //get the start and end time for critical log upload
            get_critical_log_upload_details(msg, upl_start_sec, upl_end_sec, job_id);

            //get the critical logs for requested time rage
            get_logs_in_time_range(CRITICAL_LOG_PATH, file_list, upl_start_sec, upl_end_sec);
            LOG_I(TAG, "Job %s : Found %d critical log zips to upload between eopch %d and %d",
                    job_id.c_str(), file_list.size(), upl_start_sec, upl_end_sec);
            if(syslog_enabled)
            {
                int file_count_before = file_list.size();
                get_logs_in_time_range(SYS_LOG_PATH_ARCHIVED, file_list, upl_start_sec, upl_end_sec);
                LOG_I(TAG, "Job %s : Found %d syslog zips to upload between epoch %d and %d",
                    job_id.c_str(), file_list.size()-file_count_before, upl_start_sec, upl_end_sec);
            }

            break;

        case UL_NON_CRITICAL_LOG:
            //get the non-critical logs for requested time rage
            get_logs_in_time_range(NON_CRITICAL_LOG_PATH, file_list, msg->start_sec, non_critical_end_sec);
            LOG_I(TAG, "Found %d non-critical log zips to upload", file_list.size());
            break;

        case UL_SYS_LOG:
            if(syslog_enabled)
            {
                log_dir = SYS_LOG_PATH_ARCHIVED; // if syslog is enabled in config then upload from the archived path
            }
            else
            {
                log_dir = SYS_LOG_PATH; // if syslog is not enabled in config then upload from the default path
            }
            get_files_by_modified_time_range_pattern(log_dir.c_str(), file_list, ".gz", msg->start_sec, msg->end_sec, "syslog");

            //Add one file from next 24 hrs time range
            check_and_add_additional_log(log_dir.c_str(), file_list, ".gz", msg->end_sec, syslog_end_sec, "syslog");

            break;
    }

    int status = CALL_SUCCESS;

    if (file_list.size() == 0 && (log_type == UL_NON_CRITICAL_LOG || log_type == UL_SYS_LOG)) {
        //If nothing to upload, send "err" to cloud as response
        send_log_upl_completion_msg(msg, msg->client_id, CALL_INVALID);
        return status;
    }

    if (file_list.size() == 0 && (log_type == UL_CRITICAL_LOG_TIME_RANGE)) {

        //If no pending logs to upload, send job_id and jobStatus = No-Pending-Logs to cloud as response
        status = upload_log(NO_LOGS, log_type, msg, job_id, true);

        if(status == CALL_SUCCESS) {
            LOG_I(TAG, "No pending log to upload, notified cloud");
        }
        else {
            LOG_E(TAG, "No pending log to upload, falied to notify cloud");
        }
        return status;
    }

    //iterate through each file and upload
    for(unsigned i = 0; i<file_list.size(); i++) {
        pair<long int, string> p = file_list[i];
        if(p.second.find("7z")!= string::npos || p.second.find("zip")!= string::npos ||
                p.second.find("gz")!= string::npos){

            int upload_status = upload_log(p.second, log_type, msg, job_id);

            if(upload_status == CALL_SUCCESS) {
                LOG_I(TAG, "Log uploaded");
                file_delete(p.second);
            }
            else {
                status = upload_status; //return unsuccessful if any of the uploads fail
                LOG_I(TAG, "Log upload failed. status: %d", upload_status);
            }

            sleep(1); //sleep for 1 sec between uploads
        }
    }

    if(status == CALL_SUCCESS && (log_type == UL_NON_CRITICAL_LOG || log_type == UL_SYS_LOG)) {
        send_log_upl_completion_msg(msg, msg->client_id, CALL_SUCCESS);
    }

    return status;
}

int upload_sign_crops(const string sign_crop_sevenz_path, const string drp, const string jobid, const string status, const int64_t time_stamp, const string error_msg){
    char* req_params = NULL;
    string end_point = server_url+"/"+api_version+"/upload/dp/images/job/" + jobid; 
    int sign_crop_upload_status = CALL_FAILED;

    string sign_crop_sevenz_filename = "";
    get_filename_from_path(sign_crop_sevenz_path, sign_crop_sevenz_filename);
    req_params = create_sign_crop_upload_payload(sign_crop_sevenz_filename, drp, jobid, status, time_stamp, error_msg);
    if(req_params == NULL){
        LOG_E(TAG,"JSON Payload creation failed for sign crops");
        return CALL_INVALID;
    }

    LOG_I(TAG,"Uploading sign crops... ");
    LOG_I(TAG,"Sign crops size: %d", file_size(sign_crop_sevenz_path));

    multipart_curl_response_t multipart_curl_response;
    if (status == "not-available") {
        sign_crop_upload_status = prepare_multipart_form_curl_request(end_point.c_str(), req_params, NULL, multipart_curl_response);
    }
    else{
        sign_crop_upload_status = prepare_multipart_form_curl_request(end_point.c_str(), req_params, sign_crop_sevenz_path.c_str(), multipart_curl_response);
    }
    LOG_I(TAG,"time taken for event data upload call: %lld ms", multipart_curl_response.end_time - multipart_curl_response.start_time);

    free(req_params);
    return sign_crop_upload_status;
}

void remove_leading_trailing_spaces (string &s)
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

/*
Example of how retrieve_sign_crop_list works:

Input:
    file_name_part1 = "7865b86a09ed4f68b091cdd89d36904d.jpg,7865b86a09ed4f68b091cdd89d36904e.jpg,7865b86a09ed4f68b091cdd89d36904f.jpg,7865b86a09ed4f68b091cdd89d369050.jpg,7865b86a09ed4f68b091cdd89d369051.jpg,7865b86a09ed4f68b091cdd89d369052.jpg,7865b86a09ed4f68b091cdd89d369053.jpg"
    file_name_part2 = "7865b86a09ed4f68b091cdd89d369054.jpg,7865b86a09ed4f68b091cdd89d369055.jpg,5865b86a09dd4f68b091cbd89d36904c,2024-10-10"
    
    Here, file_name_part1 contains the first 7 filenames, and file_name_part2 contains the next 2 filenames, followed by the jobId and drp.

Output:
    sign_crop_name_list = [
        "7865b86a09ed4f68b091cdd89d36904d.jpg", "7865b86a09ed4f68b091cdd89d36904e.jpg", "7865b86a09ed4f68b091cdd89d36904f.jpg",
        "7865b86a09ed4f68b091cdd89d369050.jpg", "7865b86a09ed4f68b091cdd89d369051.jpg", "7865b86a09ed4f68b091cdd89d369052.jpg",
        "7865b86a09ed4f68b091cdd89d369053.jpg", "7865b86a09ed4f68b091cdd89d369054.jpg", "7865b86a09ed4f68b091cdd89d369055.jpg",
        "5865b86a09dd4f68b091cbd89d36904c", "2024-10-10"
    ]
*/

void retrieve_sign_crop_list(vector<string>& sign_crop_name_list, char* file_name_part1, char* file_name_part2){
    string file_name_part1_str(file_name_part1);
    string file_name_part2_str(file_name_part2);

    remove_leading_trailing_spaces(file_name_part1_str);
    remove_leading_trailing_spaces(file_name_part2_str);

    string sign_crop_upload_list = file_name_part1_str;
    if (sign_crop_upload_list.length() > 0) {
        sign_crop_upload_list += ",";
    }
    sign_crop_upload_list += file_name_part2_str;

    // Split by comma and store in sign_crop_name_list
    sign_crop_name_list = split_by_delim(sign_crop_upload_list, ",");
}

bool is_file_readable(const string& file_path) {
    ifstream file_stream(file_path);
    return file_stream.good();
}

void populate_sign_crop_upload_list(vector<string>& sign_crop_upload_list, vector<string>& sign_crop_name_list){
    unordered_set<string> requested_set(sign_crop_name_list.begin(), sign_crop_name_list.end()); // Set for fast lookup of requested files
    vector<pair<long int,string>>files_in_output_path;
    get_files(SIGN_CROP_OUTPUT_PATH.c_str(), files_in_output_path, ".jpg");

    const size_t jpg_len = strlen(".jpg");
    const size_t y_len = strlen("_y.");

    for (const auto& file : files_in_output_path) {
        string full_file_path = file.second;
        size_t full_file_path_size = full_file_path.size();

        // File name should be like 0_trip0047_part00379d_91.0000_181.0000_0.0_1730706284748_y.046cc76602074a348c279bdc45e17d42.jpg , <session_name>.<uuid>.jpg here uuid is without hyphens
        // File suffix should be like 046cc76602074a348c279bdc45e17d42.jpg <UUID>.jpg here UUID is without hyphens
        
        // Ensure the file name ends with ".jpg"
        if (full_file_path_size >= jpg_len && full_file_path.substr(full_file_path_size - jpg_len) == ".jpg") {
            string file_name = full_file_path.substr(full_file_path.rfind("/") + 1);
            
            // Locate "_y." in the filename to extract the UUID
            size_t pos = file_name.find("_y.");
            if (pos != string::npos && pos + y_len < file_name.size()) {
                string file_suffix = file_name.substr(pos + y_len); // Extract UUID (portion after "_y.")
                
                // Check if this file is in the requested set and is accessible
                if (requested_set.count(file_suffix) > 0) {
                    if (is_file_readable(full_file_path)) {
                        sign_crop_upload_list.push_back(file_name);
                    } else {
                        LOG_E(TAG, "Sign crop file not present or not readable: %s", file_name.c_str());
                        file_delete(full_file_path);
                    }
                }
            } else {
                LOG_E(TAG, "Invalid file format or missing '_y.': %s", full_file_path.c_str());
            }
        } else {
            LOG_E(TAG, "Invalid file extension or length: %s", full_file_path.c_str());
        }
    }
}

/// handles zip creation and call to cloud
int prepare_upload_sign_crops(req_upload_msg_t *msg){
    vector<string> sign_crop_name_list; 
    retrieve_sign_crop_list(sign_crop_name_list, msg->fname, msg->json_fname);
    string drp = "";
    string jobId = "";
    int64_t timeStamp = 0;

    int sign_crop_upload_status = CALL_INVALID;
    string status = "not-available";
    if(sign_crop_name_list.size() == 0){
        LOG_E(TAG, "No sign crops, jobId and drp message!");
        return CALL_INVALID;
    }
    else{
        string timeStamp_str = sign_crop_name_list.back();
        if(string_to_int64(timeStamp_str, timeStamp) == false){
            LOG_E(TAG, "Failed to get timestamp from uploader message: %s", timeStamp_str.c_str());
        }
        sign_crop_name_list.pop_back();
        drp = sign_crop_name_list.back();
        sign_crop_name_list.pop_back();
        jobId = sign_crop_name_list.back();
        sign_crop_name_list.pop_back();
    }

    vector<string> sign_crop_upload_list;
    populate_sign_crop_upload_list(sign_crop_upload_list, sign_crop_name_list);

    if(sign_crop_upload_list.size() > 0){
        status = "available";
    }
     
    LOG_I(TAG,"sign crop retry_count %d", msg->retry_count);
    string sign_crop_sevenz_path = SIGN_CROPS_7Z_PATH + jobId + ".7z";
    string error_msg = ""; 

    vector<pair<long int,string>>previous_zips;
    get_files(SIGN_CROPS_7Z_PATH.c_str(), previous_zips, ".7z");
    for(auto zip: previous_zips){
        LOG_I(TAG, "Deleting previous sign crop zip: %s", zip.second.c_str());
        file_delete(zip.second);
    }
    bool zip_created = create_sign_crops_zip(sign_crop_upload_list, sign_crop_sevenz_path, error_msg);
    if(!zip_created){
        LOG_E(TAG, "New Sign crop 7z not created");
        status = "not-available";
    }

    file_fd_sync(sign_crop_sevenz_path);
    
    sign_crop_upload_status = upload_sign_crops(sign_crop_sevenz_path, drp, jobId, status, timeStamp, error_msg);
    if (sign_crop_upload_status == CALL_SUCCESS) {
        for (const auto& file_name : sign_crop_upload_list) {
            string path = SIGN_CROP_OUTPUT_PATH + file_name;
            LOG_I(TAG, "Deleting sign crop: %s", path.c_str());
            file_delete(path);
        }    
    }

    file_delete(sign_crop_sevenz_path);
    file_fd_sync(sign_crop_sevenz_path);

    return sign_crop_upload_status;
} 

int upload_event(uploader_db_handle_t db_handle, req_upload_msg_t *msg){
    string folder = msg->json_fname;
    string alert_folder_name = folder.substr(folder.rfind("/")+1);
    string state_file_name = ND_INPUT_PATH + "/" + alert_folder_name + ".STATE";
    string video_file_name = alert_folder_name + ".mp4";
    write_to_file(state_file_name, "UPLOADING_STATE");
    string summary_created = "";
    string summary_json_file = "";
	int summary_upload_status = -1;
	
	
#ifdef NO_SDCARD
    if(get_mount_status(nd_device_obj->get_external_eMMC_phy_mount_path(),curr_mount_src) !=MOUNTED) {
#else
    if(sdcard_get_mount_status(ALERTS_PATH) != SDCARD_MOUNTED) {
#endif
	    msg->level = ALERT_LOW;
	    summary_upload_status = SD_CARD_MOUNT_FAILED;
	    string str_msg = "Sdcard not ready. Ignoring";
	    LOG_I(TAG, str_msg.c_str());
	    nd_service_obj->send_err_msg(SM_E_UPLD_SD_CARD_MOUNT_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
	    return summary_upload_status;
	}
	
#ifdef BAGHEERA
    if(strstr(msg->fname, "zip") != NULL){
        summary_json_file = INT_BUFF_PATH + "/" + alert_folder_name + "_summary.json.zip";
        summary_created = RAMFS_PATH + "/"  + alert_folder_name + "_summary.json.zip";
    }else{
        summary_json_file = INT_BUFF_PATH + "/" + alert_folder_name + "_summary.json";
        summary_created = RAMFS_PATH + "/"  + alert_folder_name + "_summary.json";
    }
	if( get_file_size(summary_json_file) <= 0 ) {
		LOG_I(TAG, "Cannot access alert file %s in internal buffer, check in sdcard", summary_json_file.c_str());
		if(strstr(msg->fname, "zip") != NULL) {
            summary_json_file = ALERTS_PATH + "/" + alert_folder_name + "_summary.json.zip";
        	summary_created = RAMFS_PATH + "/" + alert_folder_name + "_summary.json.zip";
        } else {
            summary_json_file = ALERTS_PATH + "/" + alert_folder_name + "_summary.json";
        	summary_created = RAMFS_PATH + "/" + alert_folder_name + "_summary.json";
        }
	}
#else
    if(strstr(msg->fname, "zip") != NULL){
        summary_json_file = ALERTS_PATH + "/" + alert_folder_name + "_summary.json.zip";

        if(!(get_file_size(summary_json_file) > 0)) {
            summary_json_file = old_path + alert_folder_name + "_summary.json.zip"; 
        }
        summary_created = "/dev/shm/" + alert_folder_name + "_summary.json.zip";
    }else{
        summary_json_file = ALERTS_PATH + "/" + alert_folder_name + "_summary.json";
        if(!(get_file_size(summary_json_file) > 0)) {
            summary_json_file = old_path + alert_folder_name + "_summary.json"; 
        }
        summary_created = "/dev/shm/" + alert_folder_name + "_summary.json";

    }
#endif

	// If alert file not found in internal/external memory
    if( get_file_size( summary_json_file) <= 0 ){
        string str_msg = "Cannot access alert file " + summary_json_file + ". Ignoring!!";
        LOG_E(TAG, str_msg.c_str() );
        nd_service_obj->send_err_msg(SM_E_UPLD_NO_FILE, NDService::UNUSED_ERR_AUX_CODE, str_msg );   // TBD Can not Access is same as No File ?
        summary_upload_status = CALL_INVALID;
        delete_upload_request(db_handle, msg);
        return summary_upload_status;
    }

    //TODO: To do md5sum comparison of file
    

    //Decrypt file
    //TODO: Upload directly using decrypted buffer.
    if(ND_AUTH_SUCCESS != nd_file_reoperate_to_file(summary_json_file.c_str(), summary_created.c_str())){
        LOG_E(TAG, "Failed to reoperate file. Discarding file");
        summary_upload_status = CALL_INVALID;
        if(!file_delete(summary_created)){
            LOG_E(TAG, "Failed to delete partial unoperated alert: %s", summary_created.c_str());
        }
        delete_upload_request(db_handle, msg);
        return summary_upload_status;
    }
    // For testing
    // string summary_created = msg->fname;

    string end_point = server_url+"/"+api_version+"/upload/eventdata";
    LOG_I(TAG,"Request time %llu",msg->request_time);
    update_upload_status(db_handle, msg, U_UPLOADING);
    char *video_file_name_arr = strdup(video_file_name.c_str());

    LOG_I(TAG,"Summary file size: %d name:\t%s", get_file_size(summary_created), summary_created.c_str());

    collect_signal_info(alert_folder_name, msg->retry_count+1, true);

    int64_t starttime = get_system_time();
    multipart_curl_response_t multipart_curl_response;
    summary_upload_status = prepare_multipart_form_curl_request(end_point.c_str(), video_file_name_arr, summary_created.c_str(), multipart_curl_response);
    LOG_I(TAG,"time taken for event data upload call: %lld ms", multipart_curl_response.end_time - multipart_curl_response.start_time);
    int64_t endtime = get_system_time();

    //stop polling for signal info
    stop_signal_info_polling(alert_folder_name);

    send_alert_info_upload_history_healthstats(video_file_name, starttime, endtime, summary_upload_status, msg->retry_count);

    send_upload_event_message_healthstats(video_file_name, starttime, endtime, summary_upload_status);
    if(!file_delete(summary_created)){
        LOG_E(TAG, "Failed to delete unoperated alert: %s", summary_created.c_str());
    }
    if(summary_upload_status == CALL_SUCCESS){
        delete_upload_request(db_handle, msg);
        write_to_file(state_file_name, "DELETE_STATE");
        LOG_I(TAG,"Upload successful for %s", video_file_name.c_str());

        string session_name = "";
        if(!get_session_name_from_string(video_file_name, session_name)){
            LOG_E(TAG, "Invalid filename: %s",video_file_name.c_str());
        }
        send_alert_upload_info_healthstats(session_name, -1, endtime);

        if(check_process("deleteMetaData")){
            LOG_I(TAG,"Deleter is running");
        }else{
            LOG_I(TAG,"Deleter Engine is not running. Starting Now");
            int deleter_status = system("nohup $ND_DEVICE_REL_PATH/latest/deleteMetaData &");
            LOG_I(TAG,"Started Deleter: %d",(deleter_status==0));
        }
    }else{
        update_upload_status(db_handle, msg, U_UPLOAD_FAILED);
        write_to_file(state_file_name, "UPLOAD_FAILED_STATE");
        LOG_I(TAG,"Upload failed for %s", video_file_name.c_str());
    }
    free(video_file_name_arr);
    return summary_upload_status;
}

string check_file_exists_for_upload(string filename, int& quality) {
    bool hqVideo = false, lqVideo = false ;
    string ret = "not-available" ;
    if(quality == DPQ){
        filename += dp_extn;
        if(get_file_size(filename) > 0) {
            ret =  "dp-available";
        }
        return ret;
    }
    if(quality == HIGHQ){
        if(get_file_size(filename) > 0) {
            ret =  "available";
            LOG_I(TAG,"checking file status1: %s, quality %d", ret.c_str(), quality);
        }
        else if(get_file_size(filename + ld_extn) > 0) {
            ret =  "ld-available";
            quality = LOWQ ;
            LOG_I(TAG,"checking file status2: %s, quality %d", ret.c_str(), quality);
        }
    }
    else if(quality == LOWQ){
        if(get_file_size(filename + ld_extn) > 0) {
            ret =  "ld-available";
            LOG_I(TAG,"checking file status3: %s, quality %d", ret.c_str(), quality);
        }
        else if(get_file_size(filename) > 0) {
            ret =  "available";
            quality = HIGHQ ;
            LOG_I(TAG,"checking file status4: %s, quality %d", ret.c_str(), quality);
        }
    }
    else {
        ret = check_file_exists(filename);
        if(ret == "available"){
            quality = HIGHQ ;
            LOG_I(TAG,"checking file status5: %s, quality %d", ret.c_str(), quality);
        }
        else if(ret == "ld-available"){
            quality = LOWQ ;
            LOG_I(TAG,"checking file status6: %s, quality %d", ret.c_str(), quality);
        }
    }
    if(ret == "not-available"){
        quality = NO_Q ;
        LOG_I(TAG,"checking file status7: %s, quality %d", ret.c_str(), quality);
    }

    return ret;
}

bool send_dhub_query_fail_msg(int cam_num)
{
    dhub_query_fail_msg_t dhub_query_fail;
    dhub_query_fail.cam_num = cam_num;
    return send_msg((generic_msg_t*)&dhub_query_fail, UPDATE_QUERY_FAIL_COUNT, sizeof(dhub_query_fail), Q_NAME, Q_EXT_CAM, counter++);
}

void prepare_vod_trim_payload(json_t *root_json, int part_id, int start_frame_idx, int end_frame_idx, int offset, int duration){
    json_object_set_new( root_json, "part_id", json_integer(part_id) );
    json_t *trim_info = json_object();
    json_object_set_new( trim_info, "startFrame", json_integer(start_frame_idx) );
    json_object_set_new( trim_info, "endFrame", json_integer(end_frame_idx) );
    json_object_set_new( trim_info, "offset", json_integer(offset) );
    json_object_set_new( trim_info, "duration", json_integer(duration) );
    json_object_set_new( root_json, "trim_info", trim_info );
}

bool request_trim_video(req_upload_msg_t *msg_req, string infname, json_t *root_json, string &outfname){
    // Do transcoding
    bool request_trim_status = false;
    int start_frame_idx = -1;
    int end_frame_idx = -1;
    int offset = -1;
    int duration = -1;

    bool trim_status = ffmpeg_trim_video (infname, outfname, msg_req->start_sec, msg_req->end_sec, start_frame_idx, end_frame_idx, offset, duration);
    if(chmod(outfname.c_str(), S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH) == -1){
        LOG_I("Failed to change file permissions :: %s", outfname.c_str());
    }
    LOG_I(TAG,"trim_status: %d",trim_status);
    if(trim_status){
        prepare_vod_trim_payload(root_json, msg_req->part_id, start_frame_idx, end_frame_idx, offset, duration);
        request_trim_status = true;
        file_fd_sync(outfname);
    }
    else{
        string str_msg = "video trimming failed. uploading complete video!!";
        LOG_E(TAG, str_msg.c_str() );
        // Removing this file delete because the same path is being used later for checking file availability of tar command
        // file_delete(outfname);
        nd_service_obj->send_err_msg(SM_E_UPLD_VIDEO_TRIM_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
        request_trim_status = false;
    }
    return request_trim_status;
}

bool send_log_upl_completion_msg(req_upload_msg_t *msg_req, string client_id, int upload_log_status){
    res_upload_msg_t msg;
    msg.idx = msg_req->msg_idx;
    msg.req_id = msg_req->req_id;
    bool msg_status = false;

    if(upload_log_status == CALL_SUCCESS) {
        msg.status = UPLOAD_SUCCESS;
        LOG_I(TAG,"Log upload successful for  %llu", msg_req->req_id);
        msg_status = send_msg((generic_msg_t*)&msg, RES_UPLOAD_NON_CRITICAL_LOG, sizeof(msg), Q_NAME, client_id, counter++);
    } else {
        msg.status = UPLOAD_FAIL_OTHER;
        LOG_I(TAG,"Log upload failed for  %llu", msg_req->req_id);
        msg_status = send_msg((generic_msg_t*)&msg, RES_UPLOAD_NON_CRITICAL_LOG, sizeof(msg), Q_NAME, client_id, counter++);
    }
    return msg_status;
}

void send_device_vod_count(req_upload_msg_t *msg_req) {
    int total_vod_count = get_total_vod_req_count(db_handle);
    if((total_vod_count % 10) == 0) {
        LOG_I(TAG,"Updating deviceVodCount in IoT Shadow %d",total_vod_count);
        device_vod_count_msg_t msg;
        msg.count = total_vod_count;
        string client_id = msg_req->client_id;
        bool status = send_msg((generic_msg_t*)&msg, UPDATE_DEVICE_VOD_COUNT, sizeof(msg), Q_NAME, client_id, counter++);
        LOG_I(TAG,"deviceVodCount sent to client_id %s : %d", msg_req->client_id, status);

        vod_health::pending_vod_count_t pending_vod_count;
        pending_vod_count.count = total_vod_count;
        pending_vod_count.ts = get_system_time();
        vod_health::VodHealth::send_vod_count(&pending_vod_count);
    }
}

//send fetch/cancel/delete request to ExtCam service
bool send_ext_vod_req_msg(req_upload_msg_t *msg_req, ext_vod_req_status_t ext_vod_req_status) {

    //split the dir path of file and file name from fname
    ///fname = /media/SdCard/0_trip00d5_part00b955_91.0000_181.0000_0.0_1652253436545_y.mp4
    //dir_path_from_fname : /media/SdCard/ file_name_from_fname : 0_trip00d5_part00b955_91.0000_181.0000_0.0_1652253436545_y.mp4
    string video_full_path = msg_req->fname;
    string vod_dir = "", vod_fname = "";
    if(get_folder_file_names(video_full_path, vod_dir, vod_fname) == false) {
        LOG_E(TAG, "Failed to split folder and file names ");
        return false;
    }
    req_ext_vod_msg_t req_ext_vod_msg;
    req_ext_vod_msg.msg_idx = msg_req->msg_idx;
    req_ext_vod_msg.req_priority = msg_req->req_priority;
    req_ext_vod_msg.request_time = msg_req->request_time;
    strcpy(req_ext_vod_msg.fname, vod_fname.c_str());
    strcpy(req_ext_vod_msg.vod_id, msg_req->vod_id);

    req_ext_vod_msg.done = false;
    req_ext_vod_msg.cancelled = false;
    if(ext_vod_req_status == EXT_VOD_ST_DELETE) {
        req_ext_vod_msg.done = true;
    }
    else if(msg_req->cancelled || is_vod_cancelled(db_handle, msg_req)) {
        req_ext_vod_msg.cancelled = true;
    }

    LOG_I(TAG,"ExtCam :: sending req msg. vod_id %s, filename %s, priority %d, done %d, cancelled %d, req_time %llu",
            req_ext_vod_msg.vod_id, req_ext_vod_msg.fname,
            req_ext_vod_msg.req_priority, req_ext_vod_msg.done, req_ext_vod_msg.cancelled, req_ext_vod_msg.request_time);
    bool msg_status = send_msg((generic_msg_t*)&req_ext_vod_msg, REQ_FETCH_EXT_VOD, sizeof(req_ext_vod_msg), Q_NAME, Q_EXT_CAM, counter++);
    return msg_status;
}

bool send_vod_ack_or_err(req_upload_msg_t *msg_req, upload_status_t iot_response){
    res_upload_msg_t msg;
    msg.idx = msg_req->msg_idx;
    msg.req_id = msg_req->req_id;
    msg.status = iot_response;
    string client_id = msg_req->client_id;

    bool msg_status = send_msg((generic_msg_t*)&msg, RES_UPLOAD_VOD, sizeof(msg), Q_NAME, client_id, counter++);
    return msg_status;
}



void prepare_vod_payload(json_t *root, string video_file_name_no_extn, string status,
        int quality, uint64_t alert_id, string file_name, const string rgb_status,
        string upl_failure_reason = NOT_APPLICABLE){
    json_object_set_new( root, "device_id", json_string(device_id.c_str()) );
    json_object_set_new( root, "ver", json_string("0.0") );
    if(status != "not-available" && status != "upload-failure") {
        status = "available";
    }

    json_object_set_new( root, "status", json_string(status.c_str()));
    json_object_set_new( root, "devicetype", json_string(device_type.c_str()) );
    json_object_set_new( root, "session_id", json_string(device_id.c_str()) );
    json_object_set_new( root, "filename", json_string(video_file_name_no_extn.c_str()) );
    json_object_set_new( root, "upl-failure-reason", json_string(upl_failure_reason.c_str()) );

    json_object_set_new( root, "video_status", json_string(rgb_status.c_str()) ); // ext cam related. default is 'not-applicable'
    string auth_key = "";
    string auth_value = "";
    bool header_status = get_auth_key_and_value(auth_key, auth_value);
    json_object_set_new( root, auth_key.c_str(), json_string(auth_value.c_str()) );

    if(quality == HIGHQ) {
        json_object_set_new( root, "quality", json_string("highq") );
    } 
    else if(quality == LOWQ) {
        json_object_set_new( root, "quality", json_string("lowq") );
    }
    else if(quality == DPQ) {
        json_object_set_new( root, "quality", json_string("dpq") );
    }
    json_object_set_new( root, "alert_id", json_integer(alert_id) );
}

/** @brief create a tar file from video and HD observation file.
 *  @param msg_req pointer to req_upload_msg_t where uploader will receive the message from Awsiot.
 *  @param root pointer to json_t which has vod payload. 
 *  @param outfname trim video file
 *  @param trim_status trim/vodeo video 
 *  @return tar_file_name.
 */
string prepare_vod_tar(bool upload_observation, bool upload_audio, int quality, uint64_t alert_id, json_t *root, string outfname, const string rgb_status )
{
    string video_exists = check_file_exists(outfname);
    string video_file_name = outfname.substr(outfname.find_last_of("/\\")+1);
    size_t mp4_position = video_file_name.find_last_of(MP4_EXT);  
    string video_file_name_no_extn = video_file_name.substr(0, mp4_position - MP4_EXT.length() + 1 );    // video_file_name_no_extn is send to cloud.
    prepare_vod_payload(root, video_file_name_no_extn, video_exists, quality, alert_id, outfname, rgb_status);
    string tar_file_name_no_path = video_file_name_no_extn + ".tar";
    string session_name = video_file_name_no_extn.substr(video_file_name_no_extn.find_first_of("trip"));
    string tar_folder_vod = "/home/iriscli/saveMP4/vod/";                   // tar file getting saved temprary    
    string tar_file_name = tar_folder_vod + tar_file_name_no_path ; 
    string observations_zip_file_no_path = "0_" + session_name + ".zip";
    string observations_zip_file   = ALERTS_PATH + "/" + observations_zip_file_no_path;
    if(!(file_is_present(observations_zip_file) == true)) {
        observations_zip_file.clear();
        observations_zip_file = old_path + "/" + observations_zip_file_no_path; 
    }
    bool zip_file_present = file_is_present(observations_zip_file);

    string audio_file_no_path = "0_" + session_name + ".aac";
    string audio_file   = ALERTS_PATH + "/" + audio_file_no_path;
    if(!(file_is_present(audio_file) == true)) {
        audio_file.clear();
        audio_file = old_path + "/" + audio_file_no_path; 
    }
    bool audio_file_present = file_is_present(audio_file);

#ifdef BAGHEERA
    if(!zip_file_present) {
        LOG_I(TAG, "obs zip not present in sdcard, checking in internal buffer");
        observations_zip_file   = INT_BUFF_PATH + "/" + observations_zip_file_no_path;
        zip_file_present = file_is_present(observations_zip_file);
    }

    if(!audio_file_present) {
        LOG_I(TAG, "audio not present in sdcard, checking in internal buffer");
        audio_file   = INT_BUFF_PATH + "/" + audio_file_no_path;
        audio_file_present = file_is_present(audio_file);
    }
 #endif

    check_create_dir(tar_folder_vod); 
    LOG_I(TAG, "prepare_vod_tar(): session_name: %s, tar_file_name: %s, observations_zip_file: %s, audio_file: %s", session_name.c_str(),
                tar_file_name.c_str(), observations_zip_file.c_str(), audio_file.c_str() );
    LOG_I(TAG, "prepare_vod_tar(): video_exists: %s, zip_file_present: %d, upload_observation: %d, audio_file_present: %d upload_audio = %d",
                video_exists.c_str(), zip_file_present, upload_observation, audio_file_present, upload_audio );
    string tar_cmd = "cd " + RAMFS_PATH + " && tar cf " + tar_file_name;
    string tar_cmd_tmp = tar_cmd;
    bool vid_copy_status = false;
    string video_infname = RAMFS_PATH + "/" + video_file_name;

    if( get_file_size(outfname) > 0) {
        vid_copy_status = file_copy(outfname, video_infname, false, S_IRWXU | S_IRWXG | S_IRWXO);
        if(vid_copy_status) {
            tar_cmd +=  " " + video_file_name;
        }
    } else {
        int trim_key_del_status = json_object_del(root, "trim_info");
        LOG_I(TAG, "deleting trim_info section from json as we couldnt find video file: %d", trim_key_del_status);
        LOG_E(TAG, "not copying video to ramfs as file size check failed");
    }
    string obs_zip_ramfs_path = RAMFS_PATH + "/" + observations_zip_file_no_path;
    if(upload_observation && zip_file_present ) {
        bool err_reoperate_status = nd_file_reoperate_to_file(observations_zip_file.c_str(), obs_zip_ramfs_path.c_str());
        if(err_reoperate_status){
            LOG_E(TAG, "reoperate obs failed, not uploading obs");
        }
        else {
            tar_cmd +=  " "  + observations_zip_file_no_path;
        }
    }

    string audio_ramfs_path = RAMFS_PATH + "/" + audio_file_no_path;
    if( upload_audio && audio_file_present ) {
        bool err_reoperate_status = nd_file_reoperate_to_file(audio_file.c_str(), audio_ramfs_path.c_str());
        if(err_reoperate_status){
            LOG_E(TAG, "reoperate audio failed, not uploading audio");
        }
        else {
            tar_cmd +=  " "  + audio_file_no_path;
        }
    }

    LOG_I(TAG,"tar_cmd:  %s", tar_cmd.c_str() );
    if(tar_cmd_tmp != tar_cmd) {
        int tar_status = system(tar_cmd.c_str());  // create tar file
        if(0 != tar_status || get_file_size(tar_file_name) < get_file_size(video_infname)){
            LOG_E(TAG, "deleted vod tar as min size check is failing");
            file_delete(tar_file_name);
            int trim_key_del_status = json_object_del(root, "trim_info");
            LOG_I(TAG, "deleting trim_info section from json as we couldnt make a tar file");
        }
    }
    // delete the ramfs copy of all tar files
    file_delete(obs_zip_ramfs_path);
    file_delete(audio_ramfs_path);
    file_delete(video_infname);
    file_delete(outfname);
    return tar_file_name ;
}

// S1: decrypt the file 
// S2: convert from mkv to mp4
// s3: trim the file if needed
static bool reoperate_convert_format_trim_file(string src, string dest_file, bool trim_vod, req_upload_msg_t* msg_req, json_t *root_json, int& state) {
    string decrypted_file = TRIM_VOD_OUT+"/temp_reoperated.mp4";
    string converted_file = TRIM_VOD_OUT+"/temp_mkv2mp4.mp4";
    bool err_reoperate_status = false;
    bool is_val_overridden = false;
    Config_parser bagheera_config(BAGHEERACONFIG_INI);

    LOG_I(TAG, "SRC :: %s", src.c_str());
    LOG_I(TAG, "DEST :: %s", dest_file.c_str());

    // S1  nd_file_reoperate_to_file will decrypt if the input video file is encrypted else it will bypass decryption.
    if (file_is_present(src))
    {
        pid_t pid = fork();
        if (pid < 0) {
            LOG_C (TAG, "%s cannot create a child. Fork Status %d",__func__, pid);
        } else if (pid == 0) {
            LOG_I(TAG,"Decryption input file size is %d", get_file_size(src));
            err_reoperate_status = nd_file_reoperate_to_file(src.c_str(), decrypted_file.c_str());
            _exit(0);
        } else {
            task_status_t task_status = nd_set_timeout_for_task(pid, ND_REOPERATE_MAX_TIMEOUT);
            if (task_status != TASK_STATUS_SUCCESS)
            {
                LOG_E (TAG, "nd_file_reoperate_to_file task was not succesful, status %d", task_status);
                err_reoperate_status = true;
            }
        }
        /* Extra condition for file present check added, because even if reoperate fails
         * the updated status of err_reoperate_status by child process is not reflected here
         */
        if (file_is_present(decrypted_file) == false)
        {
            LOG_E (TAG, "Target file is not present:: Reoperate has failed");
            err_reoperate_status = true;
        }
        if (err_reoperate_status)
        {
            LOG_E(TAG, "reoperate video failed on video %s", src.c_str());
            file_delete(decrypted_file);
            state = CALL_DEFER;
            return false;
        }
        LOG_I(TAG,"File Decryption successful with output filesize %d", get_file_size(decrypted_file));
    }
    else
    {
        LOG_E(TAG, " Input Video File is not available %s", src.c_str());
        return false;
    }

    // S2 Conversion to MP4 format
    //Partial privacy blackout files will be already in MP4 format, so skip conversion for those files
    bool isMP4 = false;
    string resp = check_filetype(decrypted_file);
    if (resp.size()) {
        if ((resp.find("mov") != string::npos) || (resp.find("mp4") != string::npos)) {
            converted_file = decrypted_file;
            isMP4 = true;
            LOG_I(TAG, "Partial privacy blackout file detected, skipping mp4 conversion");
        }
    }

    if (!isMP4) {
        struct convertfile_args args;
        args.src = decrypted_file;
        args.dest = converted_file;
        int cam_num = get_camera_id_from_filename(src);
        args.cam_num = cam_num;
        string fps_from_config = std::to_string(OTHER_FPS);
        switch (cam_num){
            case DEVICE_CAMERA_POSITION_LEFT:
            case DEVICE_CAMERA_POSITION_RIGHT:
                break;
            case DEVICE_CAMERA_POSITION_BACK:
                fps_from_config = bagheera_config.getConfig("camera", "inward_nrt_fps", "15", true, is_val_overridden);
                break;
            case DEVICE_CAMERA_POSITION_DMS:
                fps_from_config = bagheera_config.getConfig("camera", "dms_nrt_fps", "30", true, is_val_overridden);
                break;
            case DEVICE_CAMERA_POSITION_FRONT:
                fps_from_config = bagheera_config.getConfig("camera", "outward_nrt_fps", "30", true, is_val_overridden);
                break;
            default:
                break;
        }

        string_to_integer(fps_from_config.c_str(), args.framerate);
        LOG_I(TAG, "Converting file for camera number %d with framerate %d", cam_num, args.framerate);

        task_result_t task_result = nd_timed_task(convert_file_format, MAX_CONVERT_FILE_FORMAT_TIMEOUT, &args, "convert_fileformat");
        if (task_result == TASK_SUCCESS) {
            LOG_I(TAG,"File conversion successful with output filesize %d", get_file_size(converted_file));
        } else {
            file_delete(decrypted_file);
            file_delete(converted_file);
            string err_msg = "mkv to mp4 failed in VOD after retries also";
            nd_service_obj->send_err_msg(SM_E_UPLD_VOD_MKV_TO_MP4_FAIL, NDService::UNUSED_ERR_AUX_CODE, err_msg);
            LOG_E(TAG, "mkv to mp4 failed with %d in VOD after retries also, send vod failure to cloud", task_result);
            state = CALL_INVALID;
            return false;
        }
    }

    // S3
    bool trim_status = false;
    if (trim_vod) {
        trim_status = request_trim_video(msg_req, converted_file, root_json, dest_file);
    }

    // copy completefile if request_trim_video failed or trim_vod not needed
    if (trim_status == false) {
        trim_status = file_rename(converted_file, dest_file);
        file_fd_sync(dest_file);
    }

    LOG_I(TAG, "After Conversion/Trimming final output filesize: %d", get_file_size(dest_file));

    if (!isMP4) {
        resp = check_filetype(dest_file);
        if (resp.size()) {
            if ((resp.find("mov") != string::npos) || (resp.find("mp4") != string::npos)) {
                LOG_I(TAG, "Success in check_filetype for MP4 format");
            } else {
                LOG_E(TAG, "Failure in check_filetype for MP4 format. response: %s", resp.c_str());
                state = CALL_DEFER;
                trim_status = false;
            }
        } else {
            LOG_E(TAG, "check_filetype returned NULL");
            state = CALL_DEFER;
            trim_status = false;
        }
    }

    file_delete(decrypted_file);
    file_delete(converted_file);
    return trim_status;
}

int upload_vod(req_upload_msg_t *msg_req, bool trim_vod,  int& quality, const string rgb_status){
   
    char* req_params = NULL;
    int requestedQuality = quality;

    string client_id = msg_req->client_id;
    uint64_t req_id = msg_req->req_id;
    string video = msg_req->fname;

    //split the dir path of file and file name from fname, 
    ///fname = /media/SdCard/0_trip00d5_part00b955_91.0000_181.0000_0.0_1652253436545_y.mp4
    //dir_path_from_fname : /media/SdCard/ file_name_from_fname : 0_trip00d5_part00b955_91.0000_181.0000_0.0_1652253436545_y.mp4
    string dir_path_from_fname = "", file_name_from_fname = "";
    if(get_folder_file_names(video, dir_path_from_fname, file_name_from_fname) == false) {
        LOG_E(TAG, "failed to split folder and file names");
        mark_vod_failed(msg_req, true, VOD_FAIL_REASON_DIR_ERR, true);
        return CALL_INVALID;
    }



#ifdef BAGHEERA
    video = INT_BUFF_PATH + file_name_from_fname;
#endif
    string video_exists = check_file_exists_for_upload(video, quality) ; 
    if(video_exists == "ld-available") {
        video = video + ld_extn;
    }
    if(video_exists == "dp-available") {
        video = video + dp_extn;
    }
 

    LOG_I(TAG,"File for VOD:\"%s\" %s",video.c_str(),video_exists.c_str());
    LOG_I(TAG,"Request id: %llu, vod_id: %s, retry count: %d",
            req_id, msg_req->vod_id, msg_req->retry_count);

    bool is_transcoding = transcoding_in_progress(video, CIRCULAR_BUFFER_TC_STATUS_TRANSCODING);
    if(is_transcoding){
        LOG_I(TAG, "Video is currently being transcoded. Will retry...");
        return CALL_DEFER;
    }

    size_t mp4_position = video.find_last_of(MP4_EXT);
    if(mp4_position == string::npos) {
        LOG_E(TAG,"Requested vod file is not a mp4");
        mark_vod_failed(msg_req, true, VOD_FAIL_REASON_REQ_VOD_NOT_MP4, true);
        return CALL_INVALID;
    }

    string folder = "", file = "";
    if(get_folder_file_names(video, folder, file) == false) {
        LOG_E(TAG, "failed to split folder and file names");
        mark_vod_failed(msg_req, true, VOD_FAIL_REASON_VOD_PATH_ERROR + video, true);
        return CALL_INVALID;
    }

    // check internal memory if file not availble in sdcard
#ifdef BAGHEERA
    if(video_exists == "not-available") {
        quality = requestedQuality ;
        video = old_path + file;

        video_exists = check_file_exists_for_upload(video, quality) ;
        if(video_exists == "ld-available") {
            video = video + ld_extn;
        }
    }
    else if(requestedQuality != quality){
#else
    if(requestedQuality != quality){
#endif
        int quality_ib = requestedQuality ;
        if(video_exists == "ld-available") {
            file = file.substr(0, file.length() - ld_extn.length());
        }
        string video_ib = CIRC_BUFF_PATH + file_name_from_fname;
        string video_exists_ib = check_file_exists_for_upload(video_ib, quality_ib) ;
        if(requestedQuality == quality_ib && quality_ib != NO_Q){
            LOG_I(TAG, "It should not come here");
            video = video_ib;
            video_exists = video_exists_ib ;
            quality = quality_ib ;
            if(video_exists == "ld-available") {
                video = video + ld_extn;
            }
            LOG_I(TAG, "Video %s available in the Circular Buffer is of quality %d", video.c_str(), quality);
        }
    }

    // if cam is ext_cam and video is still not available. another thread will be trying to pull from visionpro
    if(is_external_video(video)) {
        bool file_available;
        long video_file_size;
        LOG_I(TAG, "External VOD:: %s", video.c_str());

        video = get_ext_vod_path(db_handle, msg_req);

        file_available = file_is_present(video);
        video_file_size = file_size(video);
        LOG_I(TAG, "External VOD:: new path: %s, exists: %d, size:%ld", video.c_str(), file_available, video_file_size);

        /**
         * KRT2-437: noticed instance of file unavailable even after fetch successful from MDVR,
         * So, before trying to upload, ensure if file is available.
         * If not, clear fetch_status in DB, queue to fetch again and return CALL_DEFER_FETCH_MDVR
         */
        if(!(is_ext_vod_fetched(db_handle, msg_req) && file_available && video_file_size > 0)) {
            LOG_E(TAG, "MDVR video added to upload Q but not available! Adding to fetch Q again...");
            update_fetch_ext_video_status(db_handle, msg_req, false, video);
            return CALL_DEFER_FETCH_MDVR;
        }
        video_exists = "available";
    }

    //if vod file is not available, mark it as unavailable in db and return
    if(video_exists == "not-available") {
        LOG_E(TAG, "Marking VOD as failed due to unavailability: %s", video.c_str());
        mark_vod_failed(msg_req, true, VOD_FAIL_REASON_UNAVAILABLE, true);
        return CALL_INVALID;
    }

    bool trim_path_out_exist = check_create_dir(TRIM_VOD_OUT);
    if(trim_path_out_exist == false) {
        LOG_E(TAG, "cannot create output folder for file conversion");
        mark_vod_failed(msg_req, true, VOD_FAIL_REASON_FS_ERROR_FOLDER_CREATION, false);
        return CALL_INVALID;
    }
    json_t *root = json_object();

    // renaming the LD file name to HD file name
    if(file_name_from_fname.find(ld_extn) != string::npos){
        file_name_from_fname = file_name_from_fname.substr(0, file_name_from_fname.length() - ld_extn.length());
    }
    // proceed to convert only if file is available
    string outfname = TRIM_VOD_OUT + "/" + file_name_from_fname;
    if((video_exists == "available") || (video_exists == "ld-available") || (video_exists == "dp-available") ) {
        // reoperate, convert mkv file to mp4, trim if needed before uploading to cloud
        int convert_state = CALL_INVALID;
        bool convert_status = reoperate_convert_format_trim_file(video, outfname, trim_vod, msg_req, root, convert_state);
        if(convert_status == false && convert_state == CALL_INVALID) {
            LOG_E(TAG, "file conversion failed");
            mark_vod_failed(msg_req, true, VOD_FAIL_REASON_VIDEO_DECRYPTION_FAILED, false);

            file_delete(outfname);
            json_decref(root);
            return CALL_INVALID;
        }
        else if(convert_status == false && convert_state == CALL_DEFER){
            LOG_E(TAG, "file conversion / file-type check failed; likely HQ file deleted. Trying next attempt with LQ");
            file_delete(outfname);
            json_decref(root);
            return CALL_DEFER;
        }
        // If we reach here, it indicates that conversion was successful
        //video = TRIM_VOD_OUT + file;    
    }

    LOG_I(TAG, "vod video quality: %d,    alert_id: %llu",  msg_req->quality, msg_req->alert_id);
    int video_file_size = get_file_size(outfname);
    string tar_file_name = prepare_vod_tar(msg_req->upload_observation, msg_req->upload_audio, msg_req->quality ,  msg_req->alert_id, root, outfname, rgb_status);
    LOG_I(TAG,"tar_file_name: %s",tar_file_name.c_str());
    stringstream ss;
    if(msg_req->quality == DPQ){
        ss << server_url<< "/" << api_version << "/upload/video/?requestId=" << req_id << "&requestor=dp";
    }
    else {
        ss << server_url<< "/" << api_version << "/upload/video/?requestId=" << req_id;
    }

    string end_point = ss.str();




    LOG_I(TAG,"Uploading VOD...");
    int tar_file_size = get_file_size(tar_file_name);
    json_object_set_new( root, "fileSize", json_integer(tar_file_size) );
    req_params = json_dumps(root, 0);
    if(req_params == NULL){
        LOG_E(TAG,"JSON creation failed for VOD");
        mark_vod_failed(msg_req, true, VOD_FAIL_REASON_JSON_CREATION_FAILED, false);

        json_decref(root);
        file_delete(outfname);
        file_delete(tar_file_name);
        return CALL_INVALID;
    }
    LOG_I(TAG,"VOD payload: %s",req_params);
    LOG_I(TAG,"VOD payload: %s",req_params+200);
    LOG_I(TAG,"video_file_size: %0.2f kB, tar_file_size: %0.2f kB",
            (float)(video_file_size/1000), (float)(tar_file_size/1000));
    int upload_vod_status = -1;

    multipart_curl_response_t response;

    if( tar_file_size > 0 ) {
        upload_vod_status = prepare_multipart_form_curl_request(end_point.c_str(), req_params, tar_file_name.c_str(), response);
    }
    else if(video_file_size > 0) {
        upload_vod_status = prepare_multipart_form_curl_request(end_point.c_str(), req_params, outfname.c_str(), response);
    }
    else {
        upload_vod_status = prepare_multipart_form_curl_request(end_point.c_str(), req_params, NULL, response);
    }
    LOG_I(TAG,"Time taken for upload vod call: %lld ms", response.end_time - response.start_time);
    vod_health::upload_attempt_t upload_attempt;
    upload_attempt.retry_count = msg_req->retry_count;
    upload_attempt.start_ts = response.start_time;
    upload_attempt.end_ts = response.end_time;
    upload_attempt.status = response.status;
    upload_attempt.api_resp_txt = response.api_resp_txt;
    //adding vod failure 
    if("not-available" == video_exists) {
        upload_attempt.video_available = false;
    }
    else {
        upload_attempt.video_available = true;
    }
    vod_health::VodHealth::send_vod_health(msg_req, vod_health::UPLOAD_ATTEMPT , &upload_attempt);

    json_decref(root);
    free(req_params);

    file_delete(tar_file_name);
    file_delete(outfname);

    return upload_vod_status;
}

upload_class_t get_upload_class(int level, bool is_vod){
    upload_class_t new_upload;
    if(is_vod) {
        new_upload.retry_limit = UL_RETRY_MID;
        // LOWEST_P is just a placeholder that we added so we don't have to modify this code when we add new upload priorities
        // LOWEST_P should not be used as a priority
        if(level >= UL_P1 && level < LOWEST_P) {
            new_upload.priority = (upload_priority_t)level;
        }
        else {
            new_upload.priority = UL_PRIORITY_P8;
        }
    }
    else{
        switch(level){
            case HIGH:
                new_upload.priority = UL_PRIORITY_HIGH;
                new_upload.retry_limit = UL_RETRY_MAX;
                break;
            case MED:
                new_upload.priority = UL_PRIORITY_MID;
                new_upload.retry_limit = UL_RETRY_MID;
                break;
            case MED_LOW:
                new_upload.priority = UL_PRIORITY_LOW;
                new_upload.retry_limit = UL_RETRY_MID;
                break;
            case LOW_P:
                new_upload.priority = UL_PRIORITY_LOW;
                new_upload.retry_limit = UL_RETRY_MIN;
                break;
            case ALERT_LOW:
                new_upload.priority = UL_PRIORITY_LOW;
                new_upload.retry_limit = UL_RETRY_MAX;
                break;
            default:
                new_upload.priority = UL_PRIORITY_MID;
                new_upload.retry_limit = UL_RETRY_MID;
                break;
        }
    }
    return new_upload;
}

bool check_retry_request(priority_queue<req_upload_msg_t*, vector<req_upload_msg_t*>, compare> *pq, req_upload_msg_t *msg, pthread_mutex_t qlock ){
    char thread_name[THREAD_TAG_LEN];
    pthread_getname_np(pthread_self(), thread_name, THREAD_TAG_LEN);
    int retry_count = msg->retry_count;
    upload_class_t class_l = get_upload_class(msg->level, (msg->msg_type == REQ_UPLOAD_VOD));
    bool retry_status = false;
    // cout << "crr: rl: " << class_l.retry_limit << "rc: " << retry_count << endl;
    // cout << "msg_id: " << msg->msg_idx << endl;
    if(retry_count < class_l.retry_limit){
        retry_count++;
        msg->retry_count = retry_count;
        if(msg->msg_type == REQ_UPLOAD_VOD){
            //persist the retry count in db
            LOG_I(TAG, "Updating retry_count for %s : %d. Max retry: %d", msg->vod_id, retry_count, class_l.retry_limit);
            update_vod_retry_count(db_handle, msg);
            add_to_vod_upload_Q(msg);
        }
        else {
            pthread_mutex_lock(&qlock);
            pq->push(msg);
            pthread_mutex_unlock(&qlock);
        }
        LOG_I(TAG, "%s Retrying... retry_count: %d", thread_name, retry_count);
        LOG_I(TAG, "%s Details: type - msg type: %d msg level: %d", thread_name, msg->msg_type, msg->level);
        retry_status = true;
    }else{
        if(msg->msg_type == REQ_UPLOAD_VOD){
            //In case of VOD, even after retry limit is reached, we need to communicate the failure to cloud
            //The priority of the message is set to P_VOD_FAILURE and the message will be pushed to the queue
            //It will be tried upto max MAX_FAILURE_RETRY_COUNT times in a boot cycle to communicate the failure to the Cloud
            //The failire_reason will be stored as "Exhausted max retries for VOD upload"
            LOG_C(TAG, "Exhausted all upload retries for %s : %d. Max retry: %d", msg->vod_id, retry_count, class_l.retry_limit);
            //msg->level = P_VOD_FAILURE; (its updated after the 1st attempt to communicate failure to cloud)
            //update_vod_priority(db_handle, msg); (its updated after the 1st attempt to communicate failure to cloud)
            update_vod_retry_count(db_handle, msg);
            string failure_reason = (msg->level == P_VOD_CB_DELAYED) ?
                    VOD_FAIL_REASON_EXHAUSTED_MAX_RETRY_CB_ERROR : VOD_FAIL_REASON_EXHAUSTED_MAX_RETRY;
            LOG_I(TAG, "Marking VOD as failed due to %s", failure_reason.c_str());
            //mark_vod_failed will update the priority to P_VOD_FAILURE and add
            mark_vod_failed(msg, true, failure_reason, false);
            add_to_vod_upload_Q(msg);
        }
        else {
            if(msg->msg_type == REQ_UPLOAD_EVENT){
                delete_upload_request(db_handle, msg);
                //uploaddel time
                string filename = msg->json_fname;
                string session_name = "";
                if(!get_session_name_from_string(filename, session_name)){
                    LOG_E(TAG, "Invalid filename: %s",filename.c_str());
                }
                int64_t upload_del = get_system_time();
                send_alert_upload_info_healthstats(session_name, -1, upload_del);
            }
            else if(msg->msg_type == REQ_UPLOAD_NON_CRITICAL_LOGS) {
                //failed to upload non-critical logs
                send_log_upl_completion_msg(msg, msg->client_id, CALL_FAILED);
            } else if(msg->msg_type == REQ_UPLOAD_OBSERVATIONS) {
                pthread_mutex_lock(&qlock);
                is_obs_req_present = false;
                pthread_mutex_unlock(&qlock);
            } else if(msg->msg_type == REQ_UPLOAD_EA_IMAGES){
                pthread_mutex_lock(&qlock);
                    is_ea_imgs_req_present = false;
                pthread_mutex_unlock(&qlock);
            }
            LOG_I(TAG,"%s Retrial attempts exceeded. Deleting the message...", thread_name);
            clean_up(msg);
            free(msg);
        }
        retry_status = false;
    }
    return retry_status;
}

void clean_up(req_upload_msg_t *msg ){
    // priority_queue<req_upload_msg_t*> *pq = (priority_queue<req_upload_msg_t*> *) ptr;
    switch( msg->msg_type ) {
        case REQ_UPLOAD_EVENT:
            LOG_I(TAG,"Cleaning up Event");
            // cleanup_event(msg);
            break;
        case REQ_UPLOAD_VOD:
            LOG_I(TAG,"Cleaning up VOD Request");
            // cleanup_vod(msg);
            break;
        case REQ_UPLOAD_ALLDATA:
            LOG_I(TAG,"Cleaning up All Data");
            break;
        case REQ_UPLOAD_OBSERVATIONS:
            LOG_I(TAG,"Cleaning up Observations");
            break;
        case REQ_UPLOAD_EA_IMAGES:
            LOG_I(TAG,"Cleaning up EA images message");
            break;
        case REQ_UPLOAD_CRITICAL_LOGS:
            LOG_I(TAG,"Cleaning up critical logs");
            break;
        case REQ_UPLOAD_CRITICAL_LOGS_TIME_RANGE:
            LOG_I(TAG,"Cleaning up critical logs within time range");
            break;
        case REQ_UPLOAD_NON_CRITICAL_LOGS:
            LOG_I(TAG,"Cleaning up non-critical logs");
            break;
        default:
            LOG_I(TAG,"Unknown message");
    }
}

/* If there were inertial observations left in the inertial obs temp folder during previous reboot, copy it to /media/SdCard/observations  */
bool copy_pre_reboot_isummary() {
    //MOVE all json files from /home/ubuntu/.nddevice/inertial_obs_temp to /media/SdCard/observations
    string obs_int_path = INT_COMPLETE_OBS_PATH;
    string observations_path = ALERTS_PATH + "/observations/";
    vector< pair<long int, string> > file_list;
    bool ret = true;

    get_files(observations_path.c_str(), file_list, ".json");
    get_files(observations_path.c_str(), file_list, ".zip");
    //Added to get all driver_face.jpg files to sdcard obs IA-74
    get_files(observations_path.c_str(), file_list, ".jpg");

    LOG_I(TAG, "Found %d files in %s", file_list.size(), observations_path.c_str());
    
    for( vector< pair<long int, string> >::iterator iter=file_list.begin(),end=file_list.end(); iter != end; iter++ ) {
        string &src = iter->second;
        string file = src.substr(src.find_last_of("/") + 1);

        if( src != "" ) {
            //Copy the file
#ifdef NO_SDCARD
            if(get_mount_status(nd_device_obj->get_external_eMMC_phy_mount_path(),curr_mount_src) != MOUNTED){
                LOG_E(TAG, "%s Mount failed for %s",curr_mount_src.c_str(), src.c_str());
#else
            if(sdcard_get_mount_status(ALERTS_PATH) != SDCARD_MOUNTED){
                LOG_E(TAG, "SD CARD Mount failed for %s", src.c_str());
#endif
                ret = false;
                continue;
            }

            if( file_copy(src, obs_int_path+file) ) {
                LOG_I(TAG, "Copied %s", src.c_str()); 
            } else {
                LOG_E(TAG, "Copy error %s", src.c_str());
                ret = false;
            }
            
            //Delete the file
            if( file_delete(src) ) {
                LOG_I(TAG, "Removed %s", src.c_str());
            } else {
                LOG_E(TAG, "Failed to remove %s", src.c_str());
                ret = false;
            }

        } else {
            //File is empty throw error
            LOG_E(TAG, "Empty filename fetched");
            ret = false;
        }
    }

    return ret;
}

bool update_cloud_vod_failure(req_upload_msg_t *msg){
    int update_cloud_vod_failure_status;
    bool vod_unavailable = is_vod_unavailable(db_handle, msg);

    //get failure reason from DB
    string failure_reason = NOT_APPLICABLE;

    if(!vod_unavailable) {
        //get the failure reason from DB if VOD is not marked as unavailable
        failure_reason = get_vod_failure_reason(db_handle, msg);
    }
    string rgb_status = get_rgb_status(db_handle, msg);
    string video = msg->fname;
    string video_file_name = video.substr(video.find_last_of("/\\")+1);
    size_t mp4_position = video_file_name.find_last_of(MP4_EXT);
    string video_file_name_no_extn = video_file_name.substr(0, mp4_position - MP4_EXT.length() + 1 );    // video_file_name_no_extn is send to cloud.
    json_t *root = json_object();
    prepare_vod_payload(root, video_file_name_no_extn,
            vod_unavailable? "not-available" : "upload-failure" ,
            NO_Q, msg->alert_id, "", rgb_status, failure_reason);
    char *req_params = json_dumps(root, 0);
    stringstream ss;
    ss << server_url<< "/" << api_version << "/upload/video/?requestId=" << msg->req_id;
    string end_point = ss.str();

    if(req_params != NULL){
        LOG_I(TAG,"VOD payload (failure): %s",req_params);
        LOG_I(TAG,"VOD payload (failure): %s",req_params+200);
        multipart_curl_response_t response;
        update_cloud_vod_failure_status = prepare_multipart_form_curl_request(end_point.c_str(), req_params, NULL, response);
        LOG_I(TAG, "Updated cloud with VOD failure: %d", update_cloud_vod_failure_status);
        LOG_I(TAG, "time taken for upload vod failure call: %lld ms", response.end_time - response.start_time);
        free(req_params);
    }else{
        LOG_E(TAG, "Failed to create JSON payload for VOD failure, cloud not updated");
    }
    json_decref(root);

    if (update_cloud_vod_failure_status == CALL_SUCCESS) {
        int camera_id = get_camera_id_from_filename(video);

        string timestamp;
        if(! get_timestamp_from_filename(video_file_name, timestamp)) {
            LOG_E(TAG, "Failed to get timestamp from filename: %s", video_file_name.c_str());
            timestamp = "-1";
        }

        string err_msg;
        if (vod_unavailable) {
            err_msg = VOD_ERR_MSG + timestamp;
        }
        else  { //use the failure reason from DB
            err_msg = failure_reason + " @" + timestamp;
        }
        nd_service_obj->send_err_msg(SM_E_UPLD_VIDEO_ERR, camera_id, err_msg);
        return true;
    }
    return false;
}

void handle_vod_failure(req_upload_msg_t *msg, priority_queue<req_upload_msg_t*, vector<req_upload_msg_t*>, compare> *pq) {
    //send upload failure info to cloud
    bool update_status = update_cloud_vod_failure(msg);

    if(update_status) {
        LOG_E(TAG, "handle_vod_failure :: Successfully communicated to Cloud %s : %s", msg->vod_id, msg->fname);
        if(is_external_video(msg->fname)) {
            //For ext vod, it will be deleted after the fetch thread communicates to ExtCam with status done
            set_ext_vod_req_status(db_handle, msg, EXT_VOD_ST_DELETE);
            add_to_vod_fetch_Q(msg);
        }
        else {
            //delete VOD req from DB
            delete_vod_upload_request(db_handle, msg);
            send_device_vod_count(msg);
            clean_up(msg);
            free(msg);
        }
    } else {

        //de-prioritize the VOD request if not already done.
        //Its done here to make sure the failure communication is attempted once as per the original priority
        if(msg->level != P_VOD_FAILURE){
            msg->level = P_VOD_FAILURE;
            update_vod_priority(db_handle, msg);
        }

        msg->failure_retry_count++;
        if(msg->failure_retry_count >= MAX_FAILURE_RETRY_COUNT){
            LOG_E(TAG, "handle_vod_failure :: Exhausted max retry count (%d) to communicate upl-failure, will be attempted in next boot cycle: %s",
                    MAX_FAILURE_RETRY_COUNT, msg->vod_id);
            clean_up(msg);
            free(msg);
        } else {
            LOG_E(TAG, "handle_vod_failure :: failed to communicate %s : %s . Retrying after %d sec",
                    msg->vod_id, msg->fname, CALL_DEFER_SLEEP);
            sleep(CALL_DEFER_SLEEP);
            add_to_vod_upload_Q(msg);
        }
    }
}

void print_pending_alerts(){
    vector<req_upload_msg_t*> upload_list;
    get_failed_uploads(db_handle, upload_list);
    LOG_I(TAG,"pending_alerts_queue size: %d",upload_list.size());
    for(unsigned i = 0; i<upload_list.size(); i++) {
        req_upload_msg_t *msg = (req_upload_msg_t*)upload_list[i];
        LOG_I(TAG,"Pending alert session: %s",msg->fname);
        free(msg);
    }
}

int check_backoff_wait(int backoff_attempt){
    int backoff_wait = get_exp_backoff_wait(backoff_attempt, BACKOFF_BASE_RETRY_SECS);
    if(backoff_wait < 0 || backoff_wait > 600){
        LOG_I(TAG, "Overwriting get_exp_backoff_wait value: %d as value is outside the range", backoff_wait);
        backoff_wait = 300;
    }
    LOG_I(TAG, "Backoff. Sleeping for %d secs", backoff_wait);
    return backoff_wait;
}

int get_linear_backoff_wait(int backoff_attempt, int multiplier) {
    int backoff_wait = backoff_attempt * multiplier;
    return backoff_wait;
}

bool send_observation_upload_request() {

    // Static flag to track if we've already sent the request
    static bool observation_request_sent = false;

    // If we've already sent the request, don't send again
    if (observation_request_sent) {
        LOG_D(TAG, "Observation upload request already sent, skipping");
        return true;
    }

    // Allocate memory for the message
    req_upload_msg_t *new_msg = (req_upload_msg_t*)malloc(sizeof(req_upload_msg_t));
    if (new_msg == NULL) {
        LOG_E(TAG, "Failed to allocate memory for observation upload request");
        return false;
    }

    // Initialize the message structure
    memset(new_msg, 0, sizeof(req_upload_msg_t));

    // Set up the message properties
    new_msg->msg_type = REQ_UPLOAD_OBSERVATIONS;
    new_msg->request_time = get_epoch();
    new_msg->msg_idx = 7;
    new_msg->level = MED;  // Set appropriate priority level
    new_msg->retry_count = 0;

    // Send the message using the existing message queue
    bool msg_status = send_msg((generic_msg_t*)new_msg,
                             REQ_UPLOAD_OBSERVATIONS,
                             sizeof(req_upload_msg_t),
                             "UPL_OBSERVATIONS",
                             Q_NAME,
                             7);

    if (!msg_status) {
        LOG_E(TAG, "Failed to send observation upload request");
        free(new_msg);
        return false;
    }

    LOG_I(TAG, "Successfully sent observation upload request");
    observation_request_sent = true;  // Mark that we've sent the request
    free(new_msg);
    return true;
}

bool receive_cb_broadcast(ndmb_generic_msg_t *ea_msg) {
    ndmbmsg_oldest_uploadable_file *ea_msg_final = NULL;
    ea_msg_final = reinterpret_cast<ndmbmsg_oldest_uploadable_file *>(ea_msg);
    if(NULL == ea_msg_final) {
        LOG_E(TAG, "EA publisher failed to send data");
        return false;
    }

    if(ea_msg_final->topic != TOPIC_OLDEST_UPLOADABLE_FILE) {
        LOG_E(TAG, "Unknown topic: ->%s",ea_msg_final->topic);
        return false;
    }
    pthread_mutex_lock(&qlock_drp);
    oldest_drp_retained_file = string(ea_msg_final->base_file_name);
    if(oldest_drp_retained_file != ""){
        oldest_drp_retained_file_sc = sessionCount_from_file(oldest_drp_retained_file);
        if (!send_observation_upload_request()) {
            LOG_E(TAG, "Failed to initiate observation upload request");
        }
    }else{
        string str_msg = "CB broadcasted empty string for DRP";
        LOG_E(TAG, str_msg.c_str());
        nd_service_obj->send_err_msg(SM_E_UPLD_CB_DRP_BROADCAST_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg);
    }
    pthread_mutex_unlock(&qlock_drp);
    return true;
}

string get_oldest_drp_retained_file() {
    pthread_mutex_lock(&qlock_drp);
    string file_name = oldest_drp_retained_file;
    pthread_mutex_unlock(&qlock_drp);
    return file_name;
}

int64_t get_oldest_drp_retained_file_sc() {
    pthread_mutex_lock(&qlock_drp);
    int64_t sc = oldest_drp_retained_file_sc;
    pthread_mutex_unlock(&qlock_drp);
    return sc;
}

void* handle_req_upload_misc( void *ptr ) {
    priority_queue<req_upload_msg_t*, vector<req_upload_msg_t*>, compare> *pq =
            (priority_queue<req_upload_msg_t*, vector<req_upload_msg_t*>, compare> *) ptr;
    int backoff_attempt = 0;
    int sixty_secs_counter = 0;
    int one_hour_counter = 0; 
    while(1) {
        int q_size = 0;
        pthread_mutex_lock(&qlock_misc);
        q_size = pq->size();
        pthread_mutex_unlock(&qlock_misc);
        LOG_D(TAG,"Q Size: %d",q_size);
        sixty_secs_counter++;
        one_hour_counter++; 

        if(one_hour_counter%3600 == 0){
            monitor_ea_folder_size();
            one_hour_counter = 0;
        }

        if(q_size > 0 && check_internet_exist()) {
            pthread_mutex_lock(&qlock_misc);
            req_upload_msg_t *msg = pq->top();
            LOG_I(TAG,"RT: %llu",msg->request_time);
            pq->pop();
            pthread_mutex_unlock(&qlock_misc);
            int status = 0;

            switch( msg->msg_type ) {
                case REQ_UPLOAD_CRITICAL_LOGS:
                {
                    LOG_I(TAG,"Upload Critical Log Request Received %llu", msg->request_time);
                    status = upload_logs(msg, UL_CRITICAL_LOG);
                    break;
                }
                case REQ_UPLOAD_CRITICAL_LOGS_TIME_RANGE:
                {
                    LOG_I(TAG,"Upload Critical Log Time Range Request Received %llu", msg->request_time);
                    status = upload_logs(msg, UL_CRITICAL_LOG_TIME_RANGE);
                    break;
                }
                case REQ_UPLOAD_NON_CRITICAL_LOGS:
                {
                    if(msg->trim) {
                        LOG_I(TAG,"Upload Sys Log Request Received %llu", msg->request_time);
                        status = upload_logs(msg, UL_SYS_LOG);
                    }
                    else {
                        LOG_I(TAG,"Upload Non-Critical Log Request Received %llu", msg->request_time);
                        status = upload_logs(msg, UL_NON_CRITICAL_LOG);
                    }
                    break;
                }
                case REQ_UPLOAD_SIGN_CROPS:
                {
                    LOG_I(TAG,"Upload Sign Crops Request Received %llu", msg->request_time);
                    status = prepare_upload_sign_crops(msg);
                    break;
                }
                case REQ_UPLOAD_VOD_LIST:
                {
                    LOG_I(TAG, "Upload VOD list request received. Type = %d", msg->msg_type);
                    const string job_id = msg->fname;
                    status = upload_vod_list(job_id);
                    break;
                }
                case REQ_UPLOAD_EA_IMAGES:
                {
                    LOG_I(TAG,"Upload EA Images Request Received %llu", msg->request_time);
                    status = prepare_upload_ea_images(msg);
                    break;
                }
                default:
                {
                    LOG_E(TAG,"handle_req_upload_misc :: Unknown message type %d", msg->msg_type);
                }
            }
            if(status == CALL_FAILED) {
                if(msg->msg_type == REQ_UPLOAD_NON_CRITICAL_LOGS || msg->msg_type == REQ_UPLOAD_EA_IMAGES) {
                    int backoff_wait = check_backoff_wait(backoff_attempt++);
                    sleep(backoff_wait);
                    bool retry_status = check_retry_request(pq, msg, qlock_misc);
                    if (!retry_status){
                        LOG_I(TAG,"Resetting backoff attempt");
                        backoff_attempt = 0;
                    }
                }
                else {
                    //don't retry for critical logs as it will be called every 10 mins anyway
                    free(msg);
                    LOG_I(TAG,"Failed to upload logs / vod-list. Wait for next call");
                }
            }
            else if(msg->msg_type == REQ_UPLOAD_EA_IMAGES) {
                if (get_pending_ea_imgs_count() >= EA_IMGS_FILE_COUNT_MIN) {
                    // In case of pending EA images upload them back to back
                    pthread_mutex_lock(&qlock_misc);
                        pq->push(msg);
                    pthread_mutex_unlock(&qlock_misc);
                    LOG_I(TAG, "More EA images available - Adding event request back to the queue");
                } else {
                    LOG_I(TAG, "No more EA images available");
                    pthread_mutex_lock(&qlock_misc);
                        is_ea_imgs_req_present = false;
                    pthread_mutex_unlock(&qlock_misc);
                    LOG_I(TAG, "Changed is_ea_imgs_req_present to false");
                }
            }
            else { /*uploaded*/
                backoff_attempt = 0;
                clean_up(msg);
                free(msg);
                LOG_I(TAG,"status = %d , deleted message", status);
            }
        }
        else {
            if(q_size > 0 && !check_internet_exist()  && sixty_secs_counter % 60 == 0){
                sixty_secs_counter = 0;
                LOG_E(TAG,"Cannot upload logs/misc. No internet connection!");
            }
            else if(q_size == 0){
                LOG_D(TAG,"Misc upload Q is empty.");
            }
        }
        sleep(1);
    }
    return NULL;
}


void* handle_req_upload( void *ptr ) {
    priority_queue<req_upload_msg_t*, vector<req_upload_msg_t*>, compare> *pq = (priority_queue<req_upload_msg_t*, vector<req_upload_msg_t*>, compare> *) ptr;
    int sixty_secs_counter = 0;
    int second_counter = 0;
    int backoff_attempt = 0;
    while(1){
        int q_size = 0;
        pthread_mutex_lock(&qlock);
        q_size = pq->size();
        pthread_mutex_unlock(&qlock);
        LOG_D(TAG,"Q Size: %d",q_size);
        second_counter++;
        sixty_secs_counter++;
        if(q_size > 0 && check_internet_exist()){
            // cout << "If block" << endl;
            pthread_mutex_lock(&qlock);
            req_upload_msg_t *msg = pq->top();
            LOG_I(TAG,"RT: %llu",msg->request_time);
            pq->pop();
            pthread_mutex_unlock(&qlock);
            int status = 0;
            // cout << "MR Details: type - " << msg->msg_type << msg->level << endl;

            switch( msg->msg_type ) {
                case REQ_UPLOAD_EVENT:
                    LOG_I(TAG,"Upload Event Request Received %llu", msg->request_time);
                    status = upload_event(db_handle, msg);
                    break;
                case REQ_UPLOAD_OBSERVATIONS:
                    LOG_I(TAG,"Upload Observations Request Received %llu", msg->request_time);
                    status = prepare_upload_observations(msg);
                    break;
                default:
                    LOG_I(TAG,"Unknown message");
            }
            if(status == CALL_FAILED){
                // TODO: Sleep for backoff time
                int backoff_wait = check_backoff_wait(backoff_attempt++);
                // print_pending_alerts();
                sleep(backoff_wait);
                bool retry_status = check_retry_request(pq, msg, qlock);
                if (!retry_status){
                    LOG_I(TAG,"Resetting backoff attempt");
                    backoff_attempt = 0;
                }
            }
            else if(status == SD_CARD_MOUNT_FAILED){
                pthread_mutex_lock(&qlock);
                pq->push(msg);
                LOG_I(TAG, "SD_CARD_MOUNT_FAILED - Adding event request %s back to the queue", msg->fname);
                pthread_mutex_unlock(&qlock);
            }
            else if(msg->msg_type == REQ_UPLOAD_OBSERVATIONS) {
                if (get_pending_observations_size() >= OBS_FILE_COUNT_MIN) {
                    // check_pending_observations
                    pthread_mutex_lock(&qlock);
                    pq->push(msg);
                    pthread_mutex_unlock(&qlock);
                    LOG_I(TAG, "More obs files available - Adding event request %s back to the queue", msg->fname);
                } else {
                    LOG_I(TAG, "No more obs files available");
                    pthread_mutex_lock(&qlock);
                    is_obs_req_present = false;
                    pthread_mutex_unlock(&qlock);
                    LOG_I(TAG, "Changed is_obs_req_present to false");
                }
            }
            else {
                backoff_attempt = 0;
                clean_up(msg);
                free(msg);
                LOG_I(TAG,"deleted message");
            }
        }
        else{
            if(q_size > 0 && !check_internet_exist() && sixty_secs_counter%60 == 0){
                sixty_secs_counter = 0;
                cleanup_old_observations();
                LOG_E(TAG,"Cannot upload. No internet connection!");
            }
            else if(q_size == 0){
                LOG_D(TAG,"Q is empty!!");
            }
        }
#ifdef ROUTE_LOGS
    if(second_counter%(LOG_FILE_DURATION*60) == 0)
    {
        route_logs( LOG_DIR.c_str() );
    }
#endif
        sleep(1);
    }
    return NULL;
}

void* handle_vod_elapsed_time(void * arg) {
    while(true) {
        sleep(60 * VOD_ELAPSED_TIME_COUNT_INTERVAL);
        update_vod_elapsed_time(db_handle, VOD_ELAPSED_TIME_COUNT_INTERVAL);
        LOG_I(TAG,"Updated elapsed time for pending VODs (if any) : +%d mins", VOD_ELAPSED_TIME_COUNT_INTERVAL);
    }
}

void cb_broadcasted_sc_cleanup(uint64_t steady_now, uint64_t system_now) {
    int64_t target_sc = get_oldest_drp_retained_file_sc();

    if(target_sc != -1) {
        cleanup_old_ea_imgs_drp(target_sc, drp_enabled);
    }
}

bool start_receive_cb_broadcast_thread() {
    LOG_I(TAG, "Entered %s", __func__);

    bool status = false;
    if (drp_tick_) {
        status = true;
    } else {
        drp_tick_.reset(new (std::nothrow) nd::utils::TimerTick());
        if (drp_tick_) {
            drp_tick_->RegisterCB(cb_broadcasted_sc_cleanup);
            drp_tick_->SetInterval(cb_broadcast_interval_);
            drp_tick_->Start();
            status = true;
            LOG_I(TAG, "CB broadcast receiver thread created");
        } else {
            LOG_E(TAG, "drp_tick_ is null");
        }
    }

    return status;
}

void* handle_ext_vod( void *ptr )
{
    priority_queue<req_upload_msg_t*, vector<req_upload_msg_t*>, compare> *pq = (priority_queue<req_upload_msg_t*, vector<req_upload_msg_t*>, compare> *) ptr;

    while (1) {
        int q_size = 0;
        pthread_mutex_lock(&qlock_ext_vod);
        q_size = pq->size();
        pthread_mutex_unlock(&qlock_ext_vod);
        LOG_D(TAG,"Ext VOD Q Size: %d",q_size);
        if(q_size > 0){
            pthread_mutex_lock(&qlock_ext_vod);
            req_upload_msg_t *msg = pq->top();
            pq->pop();
            pthread_mutex_unlock(&qlock_ext_vod);

            if(msg->msg_type == REQ_UPLOAD_VOD) {
                LOG_I(TAG,"Handle ext VOD : %llu , VOD %s : %s, Priority %d", msg->request_time, msg->vod_id, msg->fname, msg->req_priority);
                int vod_req_st = get_ext_vod_req_status(db_handle, msg);
                bool skip_cb_check = (vod_req_st == EXT_VOD_ST_DELETE) || is_vod_cancelled(db_handle, msg);
                bool send_ext_cam_msg = skip_cb_check;

                if(!skip_cb_check) {
                    cb_response_to_upl_query_t cb_response = get_upload_status_from_cb(msg->fname, UPLOAD_FILE_TYPE_VIDEO, cb_query_id++);
                    if (cb_response.reason == CB_UPLOAD_CHECK_QUERY_FAILED || cb_response.reason == CB_FILE_AVAILABLE_BUT_NOT_IN_DB) {
                        //if cb response reason is CB_UPLOAD_CHECK_QUERY_FAILED, add back to fetch Q and retry
                        LOG_I(TAG, "CB Error: reason: %d, vod_id: %s", cb_response.reason, msg->vod_id);
                        sleep(CALL_DEFER_FETCH_MDVR_SLEEP);
                        add_to_vod_fetch_Q(msg);
                    }
                    else if(cb_response.upload == false) {
                        LOG_W(TAG, "Not fetching VOD as per CB response. vod_id: %s, upload: %d, reason: %d",
                                msg->vod_id, cb_response.upload, cb_response.reason);


                        bool file_unavailable = false;
                        string vod_failure_reason = get_vod_failure_reason_by_id(cb_response.reason, file_unavailable);
                        mark_vod_failed(msg, true, vod_failure_reason, file_unavailable);
                        add_to_vod_upload_Q(msg);
                    }
                    else {
                        send_ext_cam_msg = true;
                    }
                }
                if(send_ext_cam_msg) {
                    ext_vod_req_status_t ext_vod_req_status = EXT_VOD_ST_NEW;
                    if(vod_req_st >= EXT_VOD_ST_NEW && vod_req_st <= EXT_VOD_ST_DELETE ){
                        ext_vod_req_status = (ext_vod_req_status_t)vod_req_st;
                    }
                    else {
                        LOG_E(TAG,"Handle ext VOD : Invalid ext_vod_req_status %d for %s. Will send the fetch req to ExtCam", vod_req_st, msg->vod_id);
                    }

                    bool ext_vod_fetch_req_sent;
                    //the request will be sent again in each boot cycle if the ACK not received
                    if(ext_vod_req_status == EXT_VOD_ST_DELETE ||
                        ext_vod_req_status == EXT_VOD_ST_NEW ||
                        ext_vod_req_status == EXT_VOD_ST_REQ_SENT ) {

                        ext_vod_fetch_req_sent =  send_ext_vod_req_msg(msg, ext_vod_req_status);
                        LOG_I(TAG,"ExtCam :: send_ext_vod_req_msg - vod_id %s, status %d", msg->vod_id, ext_vod_fetch_req_sent);
                    }
                    else {
                        LOG_D(TAG,"ExtCam :: Fetch request already sent for %s", msg->vod_id);
                        ext_vod_fetch_req_sent = true;
                    }
                    if(ext_vod_fetch_req_sent) {
                        if (ext_vod_req_status == EXT_VOD_ST_NEW) {
                            set_ext_vod_req_status(db_handle, msg, EXT_VOD_ST_REQ_SENT);
                            vod_health::ext_vod_req_t ext_vod_req;
                            ext_vod_req.ts = get_system_time();
                            vod_health::VodHealth::send_vod_health(msg, vod_health::EXT_VOD_REQ , &ext_vod_req);
                        }
                        else if(ext_vod_req_status == EXT_VOD_ST_DELETE) {
                            LOG_I(TAG,"ExtCam :: Done msg sent to ExtCam. Deleting VOD req for %s : %s", msg->vod_id, msg->fname);
                            delete_vod_upload_request(db_handle, msg);
                            send_device_vod_count(msg);
                        }
                        clean_up(msg);
                        free(msg);
                    }
                    else {
                        sleep(CALL_DEFER_FETCH_MDVR_SLEEP);
                        LOG_E(TAG,"Handle ext VOD : Failed to send request to ExtCam for %s. Adding back to Q", msg->vod_id );
                        add_to_vod_fetch_Q(msg);
                    }
                }

            }
            else {
                LOG_W(TAG,"Handle ext VOD : Unknown message type %d", msg->msg_type);
                clean_up(msg);
                free(msg);
            }
        }
        sleep(2);
    }
    return NULL;
}


cb_response_to_upl_query_t get_upload_status_from_cb(const string fname,
                                                    const upload_file_type_t file_type,
                                                    const unsigned int query_id) {

    unique_lock<mutex> lock(upl_cb_query_mtx);

    // In case communication is not successful with CB, the default behaviour will "Do Not Upload"
    cb_response_to_upl_query_t cb_response;
    cb_response.query_id = query_id;
    cb_response.upload = false;
    cb_response.reason = CB_UPLOAD_CHECK_QUERY_FAILED;

    string video_file_name = fname.substr(fname.find_last_of("/\\") + 1);

    //query to CB starts
    nd_msgq_t::nd_msg_t *msg = NULL;

    do{
        // Get upload status from file metadata or /dev/shm file
        //if file is present in sdcard, get upload status directly
        if(query_upload_status(extended_attr_enabled, video_file_name, file_type, cb_response)){
            LOG_I(TAG, "get_upload_status_from_cb:: upload %d, reason %d, file %s",
                    cb_response.upload, cb_response.reason, fname.c_str());
            break;
        }

        uploader_query_to_cb_t cb_query_msg;
        strncpy(cb_query_msg.base_file_name, video_file_name.c_str(), FNAME_LEN);
        cb_query_msg.query_id = query_id;
        cb_query_msg.file_type = file_type;

        LOG_I(TAG, "Checking with CB if file can be uploaded: %s , type %d, query_id %d",
                cb_query_msg.base_file_name, cb_query_msg.file_type, cb_query_msg.query_id);

        msg_q_upl_to_cb = nd_msgq_t::get_msgq(Q_NAME_TO_CB, nd_msgq_t::ND_MSGQ_CLIENT);
        if (msg_q_upl_to_cb == NULL)
        {
            string str_msg = "Q_NAME_TO_CB NA ";
            str_msg += video_file_name;
            LOG_C(TAG, " %s", str_msg.c_str());
            nd_service_obj->send_err_msg(SM_E_UPLD_VOD_DRP_FILE_TIME_NO_CB_RESP, NDService::UNUSED_ERR_AUX_CODE, str_msg);

            LOG_E(TAG, "Failed to get CB msgq %s. Falling back to default upload: false", Q_NAME_TO_CB);
            break;
        }

        // clear if any message is still pending in Q_NAME_TO_CB
        int rc;
        struct msqid_ds buf;
        rc = msgctl(msg_q_upl_to_cb->get_qid(), IPC_STAT, &buf);
        int num_messages = buf.msg_qnum;
        LOG_I(TAG, " Num of Previous messages still pending with CB:  %d", num_messages);
        while (num_messages > 0)
        {
            LOG_I(TAG, "poping out previous message still pending with CB");
            msg = msg_q_upl_to_cb->receive(IPC_NOWAIT);
            delete msg;
            num_messages--;
        }

        // clear if any message is still pending in Q_NAME_TO_UPL
        rc = msgctl(msg_q_cb_to_upl->get_qid(), IPC_STAT, &buf);
        num_messages = buf.msg_qnum;
        LOG_I(TAG, " Num of Previous messages still pending with UPL:  %d", num_messages);
        while (num_messages > 0)
        {
            LOG_I(TAG, "poping out previous message still pending with UPL");
            msg = msg_q_cb_to_upl->receive(IPC_NOWAIT);
            delete msg;
            num_messages--;
        }

        bool msg_status = send_msg((generic_msg_t *)&cb_query_msg, UPLOADER_QUERY_TO_CB, sizeof(cb_query_msg), Q_NAME_TO_UPL, Q_NAME_TO_CB, 0);

        // read in Q_NAME_TO_UPL
        // waits for max 500ms for CB to come up
        int cnt = 0;
        while (cnt < MAX_RETRY_FOR_CB_RES)
        {
            usleep(CB_QUERY_SLEEP_MICRO_SEC);
            if ((msg = msg_q_cb_to_upl->receive(IPC_NOWAIT)) != NULL)
            {
                break;
            }
            cnt++;
        }
        if (cnt >= MAX_RETRY_FOR_CB_RES)
        {
            string str_msg = "No response from CB ";
            str_msg += video_file_name;
            LOG_E(TAG, " %s", str_msg.c_str());
            nd_service_obj->send_err_msg(SM_E_UPLD_VOD_DRP_FILE_TIME_NO_CB_RESP, NDService::UNUSED_ERR_AUX_CODE, str_msg);
            break;
        }

        cb_response_to_upl_query_t *cb_response_msg = (cb_response_to_upl_query_t *)msg->get_buffer();
        if (cb_response_msg == NULL)
        {
            LOG_E(TAG, "Message buffer null. Skipping!!");
            string str_msg = "Msg buff null ";
            str_msg += video_file_name;
            nd_service_obj->send_err_msg(SM_E_UPLD_VOD_DRP_FILE_TIME_NO_CB_RESP, NDService::UNUSED_ERR_AUX_CODE, str_msg);
            break;
        }
        
        // Delete dev shm file as CB responded in time
        reset_upload_status(video_file_name, file_type);
        LOG_I(TAG, "get_upload_status_from_cb:: Response received from %s for %s", cb_response_msg->client_id, fname.c_str());

        cb_response.query_id = cb_response_msg->query_id;
        cb_response.upload = cb_response_msg->upload;
        cb_response.reason = cb_response_msg->reason;

    } while (false);
    //query to CB ends

    if(msg != NULL) {
        delete msg;
    }
    LOG_I(TAG, "get_upload_status_from_cb:: query_id %d, upload %d, reason %d, file %s",
            cb_response.query_id, cb_response.upload, cb_response.reason, video_file_name.c_str());
    return cb_response;
}


string get_vod_failure_reason_by_id(upl_to_cb_query_result_t failure_code, bool &file_unavailable) {
    string vod_failure_reason;
    file_unavailable = false;
    switch (failure_code)
    {
        case CB_UPLOAD:
            vod_failure_reason = NOT_APPLICABLE;
            break;
        case CB_DRP_DO_NOT_UPLOAD:
            vod_failure_reason = VOD_FAIL_REASON_DRP;
            break;
        case CB_DRP_UNKNOWN_TIMESTAMP:
            vod_failure_reason = VOD_FAIL_REASON_DRP_UNKNOWN_TS;
            break;
        case CB_RECORD_PRIVACY_ENABLED:
            vod_failure_reason = VOD_FAIL_REASON_REC_PRIVACY;
            break;
        case CB_UPLOAD_PRIVACY_ENABLED:
            vod_failure_reason = VOD_FAIL_REASON_UPL_PRIVACY;
            break;
        case CB_FILE_NOT_PRESENT_IN_DB:
            vod_failure_reason = VOD_FAIL_REASON_UNAVAILABLE;
            file_unavailable = true;
            break;
        case CB_UPLOAD_CHECK_QUERY_FAILED:
            vod_failure_reason = VOD_FAIL_REASON_CB_QUERY_FAILED;
            break;
        default:
            vod_failure_reason = NOT_APPLICABLE;
            break;
    }
    return vod_failure_reason;
}

// VOD upload thread
void *handle_req_upload_long(void *ptr)
{
    RentalUtils rental_utils;
    priority_queue<req_upload_msg_t *, vector<req_upload_msg_t *>, compare> *pq = (priority_queue<req_upload_msg_t *, vector<req_upload_msg_t *>, compare> *)ptr;
    int backoff_attempt = 0;
    drp_upl_cb_enum_t resp_reason = MAX_DRP_UPL_CB;

    while (1)
    {
        int q_size = 0;
        pthread_mutex_lock(&qlock_long);
        q_size = pq->size();
        pthread_mutex_unlock(&qlock_long);
        LOG_D(TAG, "Q Size: %d", q_size);
        if (q_size > 0 && check_internet_exist())
        {
            // cout << "If block" << endl;
            pthread_mutex_lock(&qlock_long);
            req_upload_msg_t *msg = pq->top();
            pq->pop();
            pthread_mutex_unlock(&qlock_long);
            int status = 0;
            // cout << "MR Details: type - " << msg->msg_type << msg->level << endl;

            // by default setting the response as CB_UPLOAD_CHECK_QUERY_FAILED
            cb_response_to_upl_query_t cb_response;
            cb_response.reason = CB_UPLOAD_CHECK_QUERY_FAILED;
            switch( msg->msg_type ) {
                case REQ_UPLOAD_VOD:
                {
                    LOG_I(TAG, "VOD dequeued for upload: VOD_ID - %s, Quality - %d, Alert ID - %llu, Request ID - %llu, File - %s, Retry Count - %d", msg->vod_id, msg->quality, msg->alert_id, msg->req_id, msg->fname, msg->retry_count);

                    if (is_vod_cancelled(db_handle, msg))
                    {
                        LOG_I(TAG, "VOD was cancelled / priority changed.  vod_id: %s, req time: %llu", msg->vod_id, msg->request_time);
                        status = CALL_CANCELLED;
                    }
                    else if (is_vod_req_failed(db_handle, msg))
                    {
                        LOG_I(TAG, "vod_req_failed : %s : %s", msg->vod_id, msg->fname);
                        status = CALL_INVALID;
                    }

                    else if (!rental_utils.check_valid_vod(msg))
                    {
                        LOG_W(TAG, "Data recording is disabled. Not uploading VOD. Id: %llu, video: %s : %s.", msg->req_id, msg->vod_id, msg->fname);
                        mark_vod_failed(msg, true, VOD_FAIL_REASON_DATA_RECORDING_DISABLED, false);
                        status = CALL_INVALID;
                    }
                    else {
                        cb_response = get_upload_status_from_cb(msg->fname, UPLOAD_FILE_TYPE_VIDEO, cb_query_id++);
                        if (cb_response.reason == CB_UPLOAD_CHECK_QUERY_FAILED || cb_response.reason == CB_FILE_AVAILABLE_BUT_NOT_IN_DB)
                        {
                            //if cb response reason is CB_UPLOAD_CHECK_QUERY_FAILED, retry as per VOD retry policy
                            LOG_I(TAG, "CB Error: reason: %d, vod_id: %s, retry_count: %d", cb_response.reason, msg->vod_id, msg->retry_count);
                            status = CALL_FAILED_CB_DELAY;
                        }
                        else if(cb_response.upload == false)
                        {
                            LOG_W(TAG, "Not uploading VOD as per CB response. vod_id: %s, upload: %d, reason: %d",
                                    msg->vod_id, cb_response.upload, cb_response.reason);
                            bool file_unavailable = false;
                            string vod_failure_reason = get_vod_failure_reason_by_id(cb_response.reason, file_unavailable);
                            mark_vod_failed(msg, true, vod_failure_reason, file_unavailable);
                            status = CALL_INVALID;
                        }
                        else
                        {
                            if (msg->quality == NO_Q)
                            {
                                msg->quality = HIGHQ;
                                LOG_I(TAG, "Set Default quality to highq");
                            }
                            string rgb_status = get_rgb_status(db_handle, msg);
                            status = upload_vod(msg, msg->trim, msg->quality, rgb_status);
                        }
                    }
                    break;
                }
                default:
                {
                    LOG_I(TAG, "Unknown message");
                }
            }

            if (status == CALL_CANCELLED)
            {
                // delete VOD req msg
                clean_up(msg);
                free(msg);
                LOG_I(TAG, "deleted message");
            }

            else if (status == CALL_INVALID)
            {
                // CALL_INVALID means VOD could not be uploaded  permanently.
                // And need to communicate the failure to Cloud.
                LOG_I(TAG, "handle_req_upload_long :: CALL_INVALID req_id: %llu , vod_id: %s, file: %s", msg->req_id, msg->vod_id, msg->fname);
                backoff_attempt = 0;
                handle_vod_failure(msg, pq);
            }

            else if (status == CALL_FAILED)
            {
                LOG_E(TAG, "VOD upload attempt failed: %s, vod_id", msg->vod_id);
                CONSECUTIVE_VOD_UPLOAD_FAILURE_COUNT++;
                int backoff_wait = igni_status == 0 ? IGN_OFF_VOD_RETRY_SLEEP : check_backoff_wait(backoff_attempt++);
                LOG_I(TAG, "Sleeping for %d seconds", backoff_wait);
                sleep(backoff_wait);
                check_retry_request(pq, msg, qlock_long);
            }
            else if (status == CALL_DEFER)
            {
                LOG_I(TAG, "Transcode in progress - Adding VOD request %s : %s back to the queue, after %d secs", msg->vod_id, msg->fname, CALL_DEFER_SLEEP);
                sleep(CALL_DEFER_SLEEP);
                check_retry_request(pq, msg, qlock_long);
            }
            // Not incrementing retry_count in this case
            else if (status == SD_CARD_MOUNT_FAILED)
            {
                add_to_vod_upload_Q(msg);
                LOG_I(TAG, "SD_CARD_MOUNT_FAILED - Adding VOD request %s back to the queue", msg->fname);
            }
            else if (status == CALL_DEFER_FETCH_MDVR)
            {
                LOG_I(TAG, "Ext VOD was fetched, but unavailable now. Adding %s : %s back to the fatch Q", msg->vod_id, msg->fname, CALL_DEFER_FETCH_MDVR_SLEEP);
                set_ext_vod_req_status(db_handle, msg, EXT_VOD_ST_NEW); // resetting the status so that the msg is sent to ExtCam again
                add_to_vod_fetch_Q(msg);
            } else if (status == CALL_FAILED_CB_DELAY) {
                // sleep for backoff time and retry
                int sleep_time = (msg->level == P_VOD_CB_DELAYED) ?
                        get_linear_backoff_wait(msg->retry_count, CB_DELAY_MULTIPLIER) :
                        check_backoff_wait(msg->retry_count);
                LOG_I(TAG, "Sleeping for %d seconds", sleep_time);
                sleep(sleep_time);

                if (msg->level != P_VOD_CB_DELAYED && msg->retry_count == 0) {
                    LOG_W(TAG, "Deprioritizing VOD due to CB. vod_id: %s, file: %s", msg->vod_id, msg->fname);
                    nd_service_obj->send_err_msg(SM_E_UPLD_VOD_CB_DEPRIORITIZATION, cb_response.reason, "Deprioritizing VOD due to CB");
                }
                msg->level = P_VOD_CB_DELAYED;

                check_retry_request(pq, msg, qlock_long);
            }

            // Case for CALL_SUCCESS & cases excluding above ones, message is deleted.
            // As all above cases are covered, not calling send_vod_completion_msg here to avoid duplicate message
            else
            {
                LOG_I(TAG, "Upload successful for video: %s", msg->fname);

                vod_health::vod_success_t vod_success;
                vod_success.retry_count = msg->retry_count;
                vod_success.ts = get_system_time();
                vod_health::VodHealth::send_vod_health(msg, vod_health::VOD_SUCCESS, &vod_success);

                if(is_external_video(msg->fname)) {
                    //For ext vod, it will be deleted after the fetch thread communicates to ExtCam with status done
                    set_ext_vod_req_status(db_handle, msg, EXT_VOD_ST_DELETE);
                    add_to_vod_fetch_Q(msg);
                }
                else {
                    //delete VOD req from DB
                    delete_vod_upload_request(db_handle, msg);
                    send_device_vod_count(msg);
                    clean_up(msg);
                    free(msg);
                    LOG_I(TAG,"deleted message");

                }
                CONSECUTIVE_VOD_UPLOAD_FAILURE_COUNT = 0;
                backoff_attempt = 0;
            }
        }
        else if (q_size == 0)
        {
            LOG_D(TAG, "Q is empty!!");
        }
        sleep(1);
    }
    return NULL;
}

void* uploader_data_upload_pending_status_thread(void* arg)
{
    while(1) {

    unique_lock<mutex> lock(uploader_data_upload_pend_mtx);
    uploader_data_upload_pend_cv.wait(lock,[]{return (igni_status == 0);});

        if( igni_status == 0 )
        {
            res_pend_uploader_data_upload_msg_t msg;
            msg.idx = 0;
            msg.pend_req = get_pending_vod_count(db_handle);
            msg.dhub_offline_check = shutdown_check_for_dhub_offline_and_pending_ext_cam_vods(db_handle,msg.pend_req);
            msg.retry_failure_count = CONSECUTIVE_VOD_UPLOAD_FAILURE_COUNT;
            msg.internet_status = check_and_soak_internet_availability();

            send_msg((generic_msg_t*)&msg, UPLOADER_DATA_UPLOAD_STATUS, sizeof(msg), Q_NAME, Q_POWERMON, vod_pend_msg_counter++);
        }
        else
        {
            continue;
        }
        sleep(60);
    }
}

string getUuidFromPayload(string lla_message) {
    string uuid_str = "";
    json_t *root = NULL;
    json_error_t error;
    root = json_loads(lla_message.c_str(), 0, &error);
    if(root == NULL){
        LOG_E(TAG,"json_loads failed in add_device_fields_lla");
        LOG_E(TAG,"error: on line %d: %s", error.line, error.text);
        return uuid_str;
    }

    json_t *inference_data = json_object_get(root, "inference_data");
    if( inference_data == NULL ) {
        LOG_E(TAG, "inference_data section not found in lla_message");
        json_decref(root);
        return uuid_str;
    }
    json_t *alerts_data = json_object_get(inference_data, "alerts_data");
    if( alerts_data == NULL ) {
        LOG_E(TAG, "alerts_data section not found in lla_message");
        json_decref(root);
        return uuid_str;
    }
    json_t *alerts = json_object_get(alerts_data, "alerts");
    if( alerts == NULL ) {
        LOG_E(TAG, "alerts section not found in lla_message");
        json_decref(root);
        return uuid_str;
    }

    int alerts_len = json_array_size(alerts);
    if(alerts_len != 1) {
        LOG_E(TAG, "alerts_len is %d not 1 dropping this LLA message", alerts_len );
        json_decref(root);
        return uuid_str;
    }

    json_t *alerts_0 = json_array_get(alerts, 0);
    json_t *uuid = json_object_get(alerts_0, "uuid");
    if( uuid == NULL ) {
        LOG_E(TAG, "Uuid  not found in lla_message");
        json_decref(root);
        return uuid_str;
    }

    uuid_str = json_string_value(uuid);
    json_decref(root);
    LOG_I(TAG, "UUID== %s", uuid_str.c_str());
    return uuid_str;
}

string getEventCodeFromPayload(string lla_message) {
    string event_code_str = "";
    json_t *root = NULL;
    json_error_t error;
    root = json_loads(lla_message.c_str(), 0, &error);
    if(root == NULL){
        LOG_E(TAG,"error: on line %d: %s", error.line, error.text);
        return event_code_str;
    }

    json_t *inference_data = json_object_get(root, "inference_data");
    if( inference_data == NULL ) {
        LOG_E(TAG, "inference_data section not found in lla_message");
        json_decref(root);
        return event_code_str;
    }
    json_t *alerts_data = json_object_get(inference_data, "alerts_data");
    if( alerts_data == NULL ) {
        LOG_E(TAG, "alerts_data section not found in lla_message");
        json_decref(root);
        return event_code_str;
    }
    json_t *alerts = json_object_get(alerts_data, "alerts");
    if( alerts == NULL ) {
        LOG_E(TAG, "alerts section not found in lla_message");
        json_decref(root);
        return event_code_str;
    }

    int alerts_len = json_array_size(alerts);
    if(alerts_len != 1) {
        LOG_E(TAG, "alerts_len is %d not 1 dropping this LLA message", alerts_len );
        json_decref(root);
        return event_code_str;
    }

    json_t *alerts_0 = json_array_get(alerts, 0);
    json_t *event_code = json_object_get(alerts_0, "event_code");
    if( event_code == NULL ) {
        LOG_E(TAG, "event_code  not found in lla_message");
        json_decref(root);
        return event_code_str;
    }

    event_code_str = json_string_value(event_code);
    json_decref(root);
    LOG_I(TAG, "Event code== %s", event_code_str.c_str());
    return event_code_str;
}

string getSessionFromPayload(string lla_message){
    json_t *root = NULL;
    json_error_t error;
    root = json_loads(lla_message.c_str(), 0, &error);
    if(root == NULL){
        LOG_E(TAG, "json decode error: on line %d: %s", error.line, error.text);
        return "";
    }
    json_t *session  = json_object_get(root, "videoName");
    if( session == NULL ) {
        LOG_E(TAG, "session  not found in lla_message");
        json_decref(root);
        return "";
    }
    string session_str = json_string_value(session);
    json_decref(root);
    return session_str;
}

class LlaAlert {
    public:
        int lla_priority;
        uint64_t time;
        string payload;
        int retry_count;
        LlaAlert() : lla_priority(0), time(0), payload(""), retry_count(0) { }
        LlaAlert(int lla_priority, uint64_t time, string payload, int retry_count) : lla_priority(lla_priority), time(time), payload(payload), retry_count(retry_count) { }
};

struct CompareLla {
    bool operator()(LlaAlert const& l1, LlaAlert const& l2)
    {
        if(l1.lla_priority == l2.lla_priority){
            return l1.time < l2.time;
        }
        return l1.lla_priority < l2.lla_priority;
    }
};

void setLlaPriority(LlaAlert& l1, string event_code){
    if (event_code == EVENT_CODE_HIGH_G){
        l1.lla_priority = 100;
    }
    else if(event_code == EVENT_CODE_MOD_G){
        l1.lla_priority = 99;
    }
    else if(event_code == EVENT_CODE_LOW_G){
        l1.lla_priority = 98;
    }
    else{
        l1.lla_priority = 1;
    }
}
int do_lla_upload(LlaAlert& lla){
    string end_point = server_url+"/"+api_version+"/upload/lla";
    json_t *root;
    json_error_t error;
    root = json_loads(lla.payload.c_str(), 0, &error);
    int status = CALL_INVALID;
    if(!root){
        LOG_E(TAG, "json decode error: on line %d: %s", error.line, error.text);
        return CALL_INVALID;
    }
    char* req_params = json_dumps(root, 0);
    string session  = json_string_value(json_object_get(root, "videoName"));

    string uuid  = getUuidFromPayload(lla.payload);
    LOG_I(TAG, "Uploading LLA for session: %s", session.c_str());
    int64_t starttime = get_system_time();
        multipart_curl_response_t multipart_curl_response;
        status = prepare_multipart_form_curl_request(end_point.c_str(), req_params, NULL, multipart_curl_response);
        LOG_I(TAG,"Time taken for LLA upload call: %lld ms", multipart_curl_response.end_time - multipart_curl_response.start_time);
    int64_t endtime = get_system_time();
    send_lla_info_history_healthstats(session, uuid, starttime, endtime, status, lla.retry_count-1);
        if(status == CALL_FAILED){
            if(!check_internet_exist()){
            LOG_E(TAG,"Cannot upload LLA. No internet connection!");
        }
        if (lla.retry_count < lla_retry_limit){
            LOG_E(TAG, "Will retry after %d secs", LLA_RETRY_SLEEP);
        }
                sleep(LLA_RETRY_SLEEP);
            }

    int64_t lla_del_time = get_system_time();

    if(status == CALL_SUCCESS || lla.retry_count >= lla_retry_limit){
        send_lla_add_del_history_healthstats(session, uuid, -1, lla_del_time, lla.retry_count-1);
        }
    json_decref(root);
    free(req_params);
    return status;
}

void handle_lla_retry(priority_queue<LlaAlert, vector<LlaAlert>, CompareLla> *lla_queue, LlaAlert& lla){
    int retry_count = lla.retry_count;
    if (retry_count < lla_retry_limit) {
        retry_count++;
        LOG_I(TAG, "Retrying LLA... retry_count: %d", retry_count);
        lla.retry_count = retry_count;
        pthread_mutex_lock(&qlock_lla);
        lla_queue->push(lla);
        pthread_mutex_unlock(&qlock_lla);
    }
    else{
        LOG_E(TAG, "LLA upload failure. Retrial attempts exhausted, removing from queue");
    }
}

void* upload_lla(void *ptr){
    priority_queue<LlaAlert, vector<LlaAlert>, CompareLla> *lla_queue = (priority_queue<LlaAlert, vector<LlaAlert>, CompareLla> *) ptr;
    while(1){
        pthread_mutex_lock(&qlock_lla);
        int q_size = lla_queue->size();
        pthread_mutex_unlock(&qlock_lla);
        if(q_size > 0){
            pthread_mutex_lock(&qlock_lla);
            LlaAlert lla = lla_queue->top();
            lla_queue->pop();
            pthread_mutex_unlock(&qlock_lla);
            int status = do_lla_upload(lla);
            if(status == CALL_SUCCESS){
                LOG_I(TAG, "LLA upload successful, removing from queue: %d", status);
            }
            else{
                LOG_E(TAG, "LLA upload failure. Retrying... :%d", status);
                handle_lla_retry(lla_queue, lla);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
    }
    return NULL;
}

bool check_lla_enabled(){
    bool override_val = true;
    bool is_val_overridden = false;
    string nd_config_path = ND_DEVICE_REL_PATH + "/latest/nd_config.ini";
    Config_parser nd_config_parser(nd_config_path);
    if(!nd_config_parser.getParseStatus()){
        return false;
    }
    if("true" != nd_config_parser.getConfig("low_latency_alert_notification","enabled","false", override_val, is_val_overridden)) {
        return false;
    }
    string ndcore_config_path = ND_DEVICE_REL_PATH + "/latest/nd_core_common.ini";
    Config_parser ndcore_config_parser(ndcore_config_path);
    if(ndcore_config_parser.getParseStatus()){
        uploader_socket_path = ndcore_config_parser.getConfig("messenger_sockets","lla_uploader","");
        uploader_socket_topic = ndcore_config_parser.getConfig("messenger_topics","lla_uploader","");
        if(!uploader_socket_path.empty() && !uploader_socket_topic.empty()){
            return true;
        }
    }
    return false;
}

void* handle_req_upload_lla( void *ptr ) {
    if(!check_lla_enabled()){
        LOG_C(TAG, "LLA not enabled. Exiting thread..!");
        return NULL;
    }
    priority_queue<LlaAlert, vector<LlaAlert>, CompareLla> lla_queue;
    pthread_t lla_uploader_thread;
    int ret = pthread_create(&lla_uploader_thread, NULL, upload_lla, (void *)&lla_queue);
    if(ret == 0) {
        LOG_I(TAG, "LLA uploader thread created successfully");
        pthread_setname_np(lla_uploader_thread, TAG_THREAD_P_LLA_UP);
    }
    else {
        LOG_E(TAG, "Failed to create lla_uploader_thread. Error code: %d", ret);
        nd_service_obj->send_err_msg(SM_E_UPLD_THREAD_CREATE_FAIL, ret,
                "Failed to create lla_uploader_thread");
        //terminate the process so that it can be restarted by systemd
        _exit(1);
    }

    NDMessenger::ClientBuilder lla_uploader_subscriber;
    lla_uploader_subscriber.setServer(uploader_socket_path).setTopic(uploader_socket_topic);
    while(1){
        string message = lla_uploader_subscriber.subscribe();
        if(!message.empty()){
            pthread_mutex_lock(&qlock_lla);
            string event_code = getEventCodeFromPayload(message);
            int64_t lla_add_time = get_system_time();
            LlaAlert lla(0, get_epoch(), message, 1);
            setLlaPriority(lla, event_code);
            lla_queue.push(lla);
            send_lla_add_del_history_healthstats(getSessionFromPayload(lla.payload), getUuidFromPayload(lla.payload), lla_add_time, -1, lla.retry_count-1);
            pthread_mutex_unlock(&qlock_lla);
            // upload_lla(message);
        }else{
            LOG_E(TAG,"LLA message is junk. Will retry subscribing after 1 sec..!");
            sleep(1);
        }
    }
    pthread_mutex_destroy(&qlock_lla);
    return NULL;
}

int init_lla_thread(){
    pthread_t lla_thread;
    if (pthread_mutex_init(&qlock_lla, NULL) != 0)
    {
        string str_msg = "mutex init failed for qlock_lla";
        LOG_E(TAG, str_msg.c_str() );
        nd_service_obj->send_err_msg(SM_E_UPLD_MUTEX_INIT_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
        return -1;
    }
    int ret = pthread_create(&lla_thread, NULL, handle_req_upload_lla, NULL);
    if (ret == 0) {
        LOG_I(TAG, "LLA thread created successfully");
        pthread_setname_np(lla_thread, TAG_THREAD_P_LLA);
    }
    else {
        LOG_E(TAG, "Failed to create lla_thread. Error code: %d", ret);
        nd_service_obj->send_err_msg(SM_E_UPLD_THREAD_CREATE_FAIL, ret,
                "Failed to create lla_thread" );
        //terminate the process so that it can be restarted by systemd
        _exit(1);
    }
    return 0;
}

bool is_vod_req_timeout(req_upload_msg_t *msg) {
    int elapsed_min = get_vod_elapsed_time(db_handle, msg);
    bool is_vod_timeout = elapsed_min >= VOD_TIMEOUT;
    if(is_vod_timeout) {
        nd_service_obj->send_err_msg(SM_E_UPLD_VOD_TIMEOUT, NDService::UNUSED_ERR_AUX_CODE, msg->fname );
        LOG_C(TAG, "VOD timeout for %s", msg->fname);
    }
    return is_vod_timeout;
}

/**
 * @brief method to check if VOD is expired
 * If expired (30 days drivetime passed), it will be deleted from DB and this will never be informed to cloud.
 */
bool vod_expired(req_upload_msg_t *msg) {
    int elapsed_min = get_vod_elapsed_time(db_handle, msg);
    return (elapsed_min >= VOD_EXPIRED);
}

/**
 * @brief method to fetch pending VODs from DB and add to queue
 *
 * @param db_handle - DB Handle
 * @param vod_queue - VOD uploader queue
 */
void fetch_pending_vods(){
    vector<req_upload_msg_t*> vod_list;
    get_pending_vods(db_handle, vod_list);
    LOG_C(TAG,"fetch_pending_vods: %d",vod_list.size());
    for(int i = 0, n = vod_list.size(); i < n; i++) {

        req_upload_msg_t *msg = (req_upload_msg_t*)vod_list[i];
        LOG_D(TAG,"fetch_pending_vods: %s, priority %d, request_time %llu, retry_count %d",msg->fname, msg->req_priority, msg->request_time, msg->retry_count);
        if(vod_expired(msg)) {
            LOG_C(TAG,"VOD Expired: %s, request_time %llu. Dropping from DB",msg->fname, msg->request_time);
            delete_vod_upload_request(db_handle, msg);
            send_device_vod_count(msg);
            nd_service_obj->send_err_msg(SM_E_UPLD_VIDEO_ERR, NDService::UNUSED_ERR_AUX_CODE, "VOD Expired error..!" );
        } else {
            msg->failure_retry_count = 0;

            if(msg->ib_alert && get_ext_vod_req_status(db_handle, msg) != EXT_VOD_ST_DONE) {
                //ib_alert is set when its an external video.
                //add them to a separate queue so that it can be fetched independently of uploader queue.
                //This is to minimize the dependency on MDVR for external video upload.

                if(is_vod_req_timeout(msg)) {
                    LOG_C(TAG,"VOD Timeout: %s, request_time %llu",msg->fname, msg->request_time);
                    mark_vod_failed(msg, true, VOD_FAIL_REASON_EXT_VOD_TIMEOUT, false);
                    add_to_vod_upload_Q(msg);
                    //After the failure communication to Cloud, Uploader will intimate ExtCam as done for deleteion of the fetched video based on Criteria.
                } else {
                    add_to_vod_fetch_Q(msg);
                }
            }
            else {
                add_to_vod_upload_Q(msg);
            }
        }
    }
}

void add_failed_uploads_queue(uploader_db_handle_t db_handle, void *ptr){
    priority_queue<req_upload_msg_t*, vector<req_upload_msg_t*>, compare> *pq = (priority_queue<req_upload_msg_t*, vector<req_upload_msg_t*>, compare> *) ptr;
    vector<req_upload_msg_t*> upload_list;
    LOG_I(TAG,"add_failed_uploads_queue before: %d",upload_list.size());
    get_failed_uploads(db_handle, upload_list);
    LOG_I(TAG,"add_failed_uploads_queue: %d",upload_list.size());
    for(unsigned i = 0; i<upload_list.size(); i++) {
        pthread_mutex_lock(&qlock);
        req_upload_msg_t *msg = (req_upload_msg_t*)upload_list[i];
        LOG_I(TAG,"Vector adding failed request to queue: %s, request_time %llu",msg->fname, msg->request_time);
        pq->push(msg);
        pthread_mutex_unlock(&qlock);
    }
}


string copy_alert_to_int_buff(string folder){
    string fname = folder + "/summary.json";
    string md5sum = "";
    if(file_is_present(fname)){
        string file_name = folder.substr(folder.rfind("/")+1);
        if(file_name.find("/") == string::npos){
            string alert_dest = INT_BUFF_PATH + "/" + file_name + "_summary.json.zip";
            string alert_enc = "/dev/shm/" + file_name + "_summary.json.zip";
            try{
                Zipper zipper(alert_enc);
                zipper.add(fname);
                zipper.close();
            }
            catch (...) {
                LOG_E(TAG, "Zipper exception!!  read error");
                nd_service_obj->send_err_msg(SM_E_UPLD_ZIP_READ_FAIL, NDService::UNUSED_ERR_AUX_CODE, "Zipper exception!! SdCard read error" );
                if(!file_delete(alert_enc)){
                    LOG_E(TAG, "Failed to delete unoperated alert: %s", alert_enc.c_str());
                }
                return md5sum;
            }
            if(ND_AUTH_SUCCESS != nd_file_operate_to_file(alert_enc.c_str(), alert_dest.c_str())){
                LOG_E(TAG, "File operate failed.");
                if(!file_delete(alert_dest)){
                    LOG_E(TAG, "Failed to delete partial operated alert: %s", alert_dest.c_str());
                }
            }
            if(file_is_present(alert_dest)){
                if(!calculate_md5sum(alert_dest, md5sum)) {
                    LOG_E(TAG, "Failed to get checksum for file %s", alert_dest.c_str());
                }
            }
            else{
                md5sum = "";
            }
            if(!file_delete(alert_enc)){
                LOG_E(TAG, "Failed to delete unoperated alert: %s", alert_enc.c_str());
            }
            if(md5sum != ""){
                #ifdef ENFORCE_ALERT_SYNC
                    int alert_fp;
                    alert_fp = open(alert_dest.c_str(), O_WRONLY|O_CREAT,0666);
                    fsync(alert_fp);
                    close(alert_fp);
                    LOG_I(TAG,"Synced file %s to sdcard.",alert_dest.c_str());
                #endif
            }
        }
    }
    return md5sum;
}


string copy_alert_to_circular_buff(string folder){
    string fname = folder + "/summary.json";
    string md5sum = "";
    if(file_is_present(fname)){
        string file_name = folder.substr(folder.rfind("/")+1);
        if(file_name.find("/") == string::npos){
            string alert_dest = ALERTS_PATH + "/" + file_name + "_summary.json.zip";
            string alert_enc = RAMFS_PATH + "/" + file_name + "_summary.json.zip";
            try{
                Zipper zipper(alert_enc);
                zipper.add(fname);
                zipper.close();
            }
            catch (...) {
                LOG_E(TAG, "Zipper exception!!  read error");
                nd_service_obj->send_err_msg(SM_E_UPLD_ZIP_READ_FAIL, NDService::UNUSED_ERR_AUX_CODE, "Zipper exception!! SdCard read error" );
                if(!file_delete(alert_enc)){
                    LOG_E(TAG, "Failed to delete unoperated alert: %s", alert_enc.c_str());
                }
                return md5sum;
            }
            if(ND_AUTH_SUCCESS != nd_file_operate_to_file(alert_enc.c_str(), alert_dest.c_str())){
                LOG_E(TAG, "File operate failed.");
                if(!file_delete(alert_dest)){
                    LOG_E(TAG, "Failed to delete partial operated alert: %s", alert_dest.c_str());
                }
            }
            if(file_is_present(alert_dest)){
                if(!calculate_md5sum(alert_dest, md5sum)) {
                    LOG_E(TAG, "Failed to get checksum for file %s", alert_dest.c_str());
                }
            }
            else{
                md5sum = "";
            }
            if(!file_delete(alert_enc)){
                LOG_E(TAG, "Failed to delete unoperated alert: %s", alert_enc.c_str());
            }
            if(md5sum != ""){
                #ifdef ENFORCE_ALERT_SYNC
                    int alert_fp;
                    alert_fp = open(alert_dest.c_str(), O_WRONLY|O_CREAT,0666);
                    fsync(alert_fp);
                    close(alert_fp);
                    LOG_I(TAG,"Synced file %s to sdcard.",alert_dest.c_str());
                #endif
            }
        }
    }
    return md5sum;
}


bool send_alert_copied_message_ib(req_upload_msg_t *msg) {
    bool msg_status = false;
    circular_buffer_add_file_db_msg_t cb_msg;
    cb_msg.msg_idx = 0;
    cb_msg.file_info.time = msg->request_time;
    cb_msg.file_info.duration = 0;
    cb_msg.file_info.file_type = CIRCULAR_BUFFER_TYPE_TEXT_ALERTS;
    cb_msg.file_info.camtype = CIRCULAR_BUFFER_CAM_FRONT;
    cb_msg.file_info.tc_status = CIRCULAR_BUFFER_TC_STATUS_TRANSCODED;
    cb_msg.file_info.upl_vid_enabled = DEFAULT_VALUE_UPL_VID_ENABLED;
    cb_msg.file_info.rec_vid_enabled = DEFAULT_VALUE_REC_VID_ENABLED;

    string folder(msg->json_fname);
    string alert_folder_name = folder.substr(folder.rfind("/")+1);
    string summary_created = alert_folder_name + "_summary.json.zip";
    strcpy(cb_msg.file_info.base_file_name,summary_created.c_str());
    // copy to internal buffer folder and send message to int buff service
    string summary_created_full_path = INT_BUFF_PATH + "/" + alert_folder_name + "_summary.json.zip";
    int alert_file_size = get_file_size(summary_created_full_path);
    if(alert_file_size <= 0){
        LOG_I(TAG,"Alert filesize 0. not sending message to internal_buffer %s", 
                                        summary_created_full_path.c_str());
        return msg_status;
    }
    cb_msg.file_info.file_size = alert_file_size;
    // send add file db message to both IB and CB
    msg_status = send_msg((generic_msg_t*)&cb_msg, REQ_INTERNAL_BUFFER_ADD_FILE_DB, 
                          sizeof(cb_msg), Q_NAME, "q_internal_buffer", counter++);
    msg_status = send_msg((generic_msg_t*)&cb_msg, REQ_CIRCULAR_BUFFER_ADD_FILE_DB, 
                          sizeof(cb_msg), Q_NAME, "q_circular_buffer", counter++);
    
    //send update priority message to IB to consider alert with high priority
    internal_buffer_update_priority_msg_t priority_msg;
    strncpy(priority_msg.base_file_name, summary_created.c_str(), FNAME_LEN);
    priority_msg.priority = INTERNAL_BUFFER_PRIORITY_ALERT;

    if( msg->ib_alert == true ) {
        LOG_I(TAG, "Sending update priority message to internal buffer");
        send_msg((generic_msg_t*)&priority_msg, REQ_INTERNAL_BUFFER_UPDATE_PRIORITY,
                                      sizeof(priority_msg), Q_NAME, "q_internal_buffer", 0);
    }
    return msg_status;
}
bool send_alert_copied_message_cb(req_upload_msg_t *msg) {
    bool msg_status = false;
    circular_buffer_add_file_db_msg_t cb_msg;
    cb_msg.msg_idx = 0;
    cb_msg.file_info.time = msg->request_time;
    cb_msg.file_info.duration = 0;
    cb_msg.file_info.file_type = CIRCULAR_BUFFER_TYPE_TEXT_ALERTS;
    cb_msg.file_info.camtype = CIRCULAR_BUFFER_CAM_FRONT;
    cb_msg.tc_status = CIRCULAR_BUFFER_TC_STATUS_TRANSCODED;
    cb_msg.file_info.upl_vid_enabled = DEFAULT_VALUE_UPL_VID_ENABLED;
    cb_msg.file_info.rec_vid_enabled = DEFAULT_VALUE_REC_VID_ENABLED;

    string folder(msg->json_fname);
    string alert_folder_name = folder.substr(folder.rfind("/")+1);
    string summary_created = alert_folder_name + "_summary.json.zip";
    strcpy(cb_msg.file_info.base_file_name,summary_created.c_str());
    // copy to SdCard and send message to circ buff service
    string summary_created_full_path = ALERTS_PATH + "/" + alert_folder_name + "_summary.json.zip";
    int alert_file_size = get_file_size(summary_created_full_path);
    if(alert_file_size <= 0){
        LOG_I(TAG,"Alert filesize 0. not sending message to circular_buffer %s", 
                                        summary_created_full_path.c_str());
        return msg_status;
    }
    cb_msg.file_info.file_size = alert_file_size;
    // send add file db message to CB
    msg_status = send_msg((generic_msg_t*)&cb_msg, REQ_CIRCULAR_BUFFER_ADD_FILE_DB, 
                          sizeof(cb_msg), Q_NAME, "q_circular_buffer", counter++);
    
   return msg_status;
}

bool check_remount_sd_card(){
    // checking sdcard mount status for 3 mins.
    static const int retry_limit = 12*3;
    int i = 0;
#ifdef NO_SDCARD
    mount_status_t status = get_mount_status(nd_device_obj->get_external_eMMC_phy_mount_path(),curr_mount_src);
    if(status == MOUNTED || status == MOUNTED_READONLY){
        LOG_I (TAG,"SDCARD_MOUNTED from %s", curr_mount_src.c_str());
#else
    sdcard_status_t status = sdcard_get_mount_status(ALERTS_PATH);
    if(status == SDCARD_MOUNTED){
        LOG_I (TAG,"SDCARD_MOUNTED");
#endif
        return true;
    }
    else{
        while(i++ < retry_limit){
#ifdef NO_SDCARD
            status = get_mount_status(nd_device_obj->get_external_eMMC_phy_mount_path(),curr_mount_src);
            if(status == MOUNTED){
#else
            status = sdcard_get_mount_status(ALERTS_PATH);
            if(status == SDCARD_MOUNTED){
#endif
                LOG_I (TAG,"SDCARD_MOUNTED");
                return true;
            }else{
                LOG_E(TAG,"SDCARD NOT MOUNTED. RETRYING!!");
            }
            sleep(5);
        }
    }
    return false;
}

void clone_message_received(req_upload_msg_t *msg, req_upload_msg_t *new_msg){
    new_msg->msg_type = msg->msg_type;
    new_msg->length = msg->length;
    strcpy(new_msg->client_id,msg->client_id);
    new_msg->msg_idx = msg->msg_idx;
    new_msg->res_reqd = msg->res_reqd;
    new_msg->level = msg->level;
    new_msg->payload_size = msg->payload_size;
    strcpy(new_msg->fname,msg->fname);
    strcpy(new_msg->json_fname,msg->json_fname);
    new_msg->req_id = msg->req_id;
    new_msg->retry_count = msg->retry_count;
    new_msg->request_time = msg->request_time;
    new_msg->trim = msg->trim;
    new_msg->upload_observation = msg->upload_observation;
    new_msg->upload_audio = msg->upload_audio;
    new_msg->start_sec = msg->start_sec;
    new_msg->end_sec = msg->end_sec;
    new_msg->part_id = msg->part_id;
    new_msg->ib_alert = msg->ib_alert;
    new_msg->quality = msg->quality;
    new_msg->alert_id = msg->alert_id;
    new_msg->cancelled = msg->cancelled;
    new_msg->req_priority = msg->req_priority;
    strcpy(new_msg->vod_id, msg->vod_id);
    new_msg->failure_retry_count = msg->failure_retry_count;

    if(new_msg->retry_count != 0)
    {
        LOG_E(TAG,"retry_count == %d . Resetting to 0.", msg->retry_count);
        new_msg->retry_count = 0;
    }
}

static void read_ext_camera_config() {
    int framerate;
    bool audio_enable;

    //read_ext_camera_common_config(ext_cam_feature_enabled);
    ext_cam_feature_enabled = is_ext_cam_feature_enabled();
    if(ext_cam_feature_enabled == false) {
        return;
    }

    read_ext_cam_ch1_config(ext_cam_enabled[0], ext_cam_framerate[0], ext_cam_audio_enable[0]);
    read_ext_cam_ch2_config(ext_cam_enabled[1], ext_cam_framerate[1], ext_cam_audio_enable[1]);
    read_ext_cam_ch3_config(ext_cam_enabled[2], ext_cam_framerate[2], ext_cam_audio_enable[2]);
    read_ext_cam_ch4_config(ext_cam_enabled[3], ext_cam_framerate[3], ext_cam_audio_enable[3]);
    read_ext_cam_vod_retry_config(MAX_EXT_VOD_RETRY_COUNT, 
            EXT_CAM_FILES_VOD_PROCESS_DELAY, MAX_WAIT_TIME_VOD_FILE_PULL);
    read_ext_cam_time_zone_hours(time_zone);

    read_save_ext_camera_files_in_dhub_config(save_ext_camera_files_in_dhub);
}

bool init_config_from_factory(void) {

    UPLOADER_DB_PATH = nd_device_obj->get_db_base_path() + "uploader.db";
    EA_DB_PATH = nd_device_obj->get_db_base_path() + "ea.db";
    CIRC_BUFF_PATH = nd_device_obj->get_external_eMMC_mount_path();
    ALERTS_PATH = CIRC_BUFF_PATH;
    EXT_CAM_FILE_PATH = CIRC_BUFF_PATH;
    old_path = nd_device_obj->get_external_eMMC_old_mount_path();
    ALERTS_PATH_DIR = nd_device_obj->get_external_eMMC_mount_path();
    ALERTS_PATH_DIR_PHYSMNT = nd_device_obj->get_external_eMMC_phy_mount_path();
    MOUNT_SRC = nd_device_obj->get_external_eMMC_dev_node();
    CB_DB_PATH = nd_device_obj->get_db_base_path() + "circular_buffer.db";
    sdcard_img_path = nd_device_obj->get_external_eMMC_loop_mount_path();
    SIGN_CROP_OUTPUT_PATH = nd_device_obj->get_sign_crop_base_path();
    EA_IMGS_PATH = nd_device_obj->get_ea_imgs_mount_path();
    return true;
}

bool is_external_video(string video) {
    int cam_num = get_cam_num_from_filename(video);
    if(cam_num >= DEVICE_CAMERA_POSITION_MAX &&
            cam_num < (DEVICE_CAMERA_POSITION_MAX + MAX_EXT_CAMERAS) &&
            ext_cam_feature_enabled &&
            ext_cam_enabled[get_ext_cam_num(cam_num)]) {
        return true;
    }
    return false;
}

int upload_vod_list(const string job_id) {
    string status;
    bool is_file_empty = false;
    bool csv_status = dump_vod_list_csv(db_handle, VOD_LIST_CSV_FILE, is_file_empty);

    if(csv_status == false) {
        LOG_E(TAG, "Failed to dump vod list to csv");
        status = "not-available";
    }
    else {
        if(is_file_empty) {
            LOG_I(TAG, "VOD list is empty");
            status = "no-pending";
        }
        else {
            LOG_I(TAG, "VOD list dumped to csv");

            //make a 7z of the csv file
            string sevenz_response;
            stringstream sevenz_cmd_stream;
            sevenz_cmd_stream << "7za a " << VOD_LIST_7Z_FILE << " " << VOD_LIST_CSV_FILE;
            string sevenz_cmd = sevenz_cmd_stream.str();
            int sevenz_status = system_execute_with_resp("7z vod-list", sevenz_cmd, sevenz_response);
            if(!sevenz_status) {
                LOG_E(TAG, "7z failed with response %s", sevenz_response.c_str());
                status = "not-available";
            }
            else {
                LOG_I(TAG, "7z success for pending-vod-list");
                status = "available";
            }

        }
    }
    int status_code = CALL_FAILED;

    //"{\"status\":\"available/not-available/no-pending\"}"
    json_error_t error;
    json_t *root = json_pack_ex(&error, 0, "{s:s}", "status", status.c_str());
    char* data_str = NULL;

    if(NULL == root) {
        LOG_E(TAG, "json_pack failed in upload_vod_list %s", error.text);
    } else {
        data_str = json_dumps(root, 0);
    }

    if(data_str == NULL) {
        LOG_E(TAG, "json_dumps failed in upload_vod_list");
    }
    else {
        string end_point = server_url+"/"+api_version+"/upload/device/pending-vod/jobId/" + job_id;
        LOG_I(TAG, "Uploading pending-vod-list status %s", status.c_str());
        multipart_curl_response_t multipart_curl_response;
        int status_code = prepare_multipart_form_curl_request(end_point.c_str(),
                data_str, (status == "available") ? VOD_LIST_7Z_FILE : NULL, multipart_curl_response);    
        LOG_I(TAG,"time taken to upload pending-vod-list call: %lld ms", multipart_curl_response.end_time - multipart_curl_response.start_time);
        free(data_str);
    }
    json_decref(root);

    //delete the csv file if available
    if(file_is_present(VOD_LIST_CSV_FILE)) {
        if(!file_delete(VOD_LIST_CSV_FILE)) {
            LOG_E(TAG, "Failed to delete vod-list csv file");
        }
    }
    //delete the 7z file if available
    if(file_is_present(VOD_LIST_7Z_FILE)) {
        if(!file_delete(VOD_LIST_7Z_FILE)) {
            LOG_E(TAG, "Failed to delete vod-list 7z file");
        }
    }

    return status_code;
}


void add_to_vod_upload_Q(req_upload_msg_t *msg) {
    pthread_mutex_lock(&qlock_long);
    pq_long.push(msg);
    pthread_mutex_unlock(&qlock_long);
    LOG_I(TAG,"Added to VOD upload Q %s : %s .", msg->vod_id, msg->fname);

}
void add_to_vod_fetch_Q(req_upload_msg_t *msg) {
    pthread_mutex_lock(&qlock_ext_vod);
    pq_ext_vod.push(msg);
    pthread_mutex_unlock(&qlock_ext_vod);
    LOG_I(TAG,"Added to VOD fetch Q %s : %s", msg->vod_id, msg->fname);

}

bool create_ext_vod_thread() {
    pthread_t t4;
    if (pthread_mutex_init(&qlock_ext_vod, NULL) != 0)
    {
        string str_msg = "mutex init failed for qlock_ext_vod";
        LOG_E(TAG, str_msg.c_str() );
        nd_service_obj->send_err_msg(SM_E_UPLD_MUTEX_INIT_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
        return false;
    }
    int ret = pthread_create(&t4, NULL, handle_ext_vod, (void *)&pq_ext_vod);
    if (ret == 0) {
        LOG_I(TAG, "handle_ext_vod thread created successfully");
        pthread_setname_np(t4, TAG_THREAD_FETCH_EXT_VOD);
        THREAD_FETCH_EXT_VOD_CREATED = true;
    } else {
        LOG_E(TAG, "Failed to create handle_ext_vod thread. Error code: %d", ret);
        nd_service_obj->send_err_msg(SM_E_UPLD_THREAD_CREATE_FAIL, ret,
                "Failed to create handle_ext_vod thread" );
    }
    return THREAD_FETCH_EXT_VOD_CREATED;
}

bool mark_vod_failed(req_upload_msg_t *msg,
        bool is_failed,
        string failure_reason,
        bool is_unavailable) {

    bool ret = update_vod_failure_details(db_handle, msg, is_failed, failure_reason, is_unavailable);
    if (ret) {
        vod_health::vod_failure_t vod_failure;
        vod_failure.reason = failure_reason;
        vod_failure.ts = get_system_time();
        vod_health::VodHealth::send_vod_health(msg, vod_health::VOD_FAILURE, &vod_failure);
    }
    return ret;
}

bool init_upl_cb_msgq() {
    msg_q_cb_to_upl = nd_msgq_t::get_msgq( Q_NAME_TO_UPL, nd_msgq_t::ND_MSGQ_SERVER);
    if( msg_q_cb_to_upl == NULL ) {
        LOG_E(DRP, "Cannot create message queue Q_TO_UPL_DRP");
        return false;
    }
    return true;
}

int main() {
    nd_service_obj = NDService::get_service_obj(TAG);
    counter = 0;
    bool status_log = nd_log_init(LOG_DIR.c_str());
    if(status_log == false) {
        string str_msg = "unable to init logger :: Exiting from main";
        printf("unable to init logger :: Exiting from main");
        nd_service_obj->send_err_msg(SM_E_UPLD_LOG_INIT_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
        return -1;
    }
#ifdef ROUTE_LOGS
    route_logs( LOG_DIR.c_str() );
#endif

    /* CURLOPT_NOSIGNAL=1 prevents libcurl from suppressing SIGPIPE itself.
     * Ignore SIGPIPE process-wide so that a peer closing a connection mid-write
     * (e.g. Network middlebox (proxy, firewall, NAT) silently dropping, server timeout) does not kill the uploader process.
     * curl_easy_perform() will return CURLE_SEND_ERROR instead, which is handled by libcurl gracefully. */    
    signal(SIGPIPE, SIG_IGN);
    set_nd_service_ext_object(nd_service_obj);
    nd_device_obj_init();
    init_config_from_factory();
    if(init_server_params() == false){
        string str_msg = "Parsing of config files failed. Exiting Uploader!!";
        LOG_E(TAG, str_msg.c_str() );
        nd_service_obj->send_err_msg(SM_E_UPLD_CFG_PARSE_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
        return -1;
    }
    
#ifdef NO_SDCARD
    LOG_I(TAG, "SdCard mounted from %s", curr_mount_src.c_str()); 
#endif     
    bool st_upl_cm_msg_q = init_conn_mgr_msgq();
    if(st_upl_cm_msg_q == false) {
        string str_err_msg = "Cannot create UPL_CM message queue";
        LOG_E(TAG, str_err_msg.c_str() );
        nd_service_obj->send_err_msg(SM_E_UPLD_CM_MSGQ_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_err_msg);
    }

    init_lla_thread();
    read_ext_camera_config();
    read_rgb_analysis_config(rgb_analysis);
    Config_parser c(BAGHEERACONFIG_INI);
    bool val_overridden = false;
    string drp_config_str  ="0";
    if (!c.getParseStatus ()) {
        LOG_E (TAG, "Error parsing bagheera_config");
    }
    if("1" == c.getConfig("drp","enabled",drp_config_str, true, val_overridden)) {
        drp_enabled = true;
    }
    if(drp_enabled) {
        val_overridden = false;
        drp_config_str = c.getConfig("drp","clock_hours", drp_config_str, true, val_overridden);
        if(string_to_int64(drp_config_str, drp_clock_hours) == false) {
            LOG_E(DRP, "failed to get drp_clock_hours from drp_config_str");
            drp_clock_hours = 72;
        }
        nd::utils::drp_clock_hour_range_check(drp_clock_hours);
        LOG_I(DRP, "drp_config_str: %s , clock_hours: %d",drp_config_str.c_str(),  drp_clock_hours);
    }
    
    bool is_ea_enabled = false;

    val_overridden = false;
    if("1" == c.getConfig("ea_config", "enabled", "0", true, val_overridden)) {
        is_ea_enabled = true;
    }
    else {
        is_ea_enabled = false;
    }

    if("1" == c.getConfig("uploader_settings", "curl_init_once_flag", "0", true, val_overridden)) {
        curl_init_once_flag = true;
    }
    else {
        curl_init_once_flag = false;
    }

    if(curl_init_once_flag) {
        // Initialize CURL library once at startup (before threads are created) if curl_init_once_flag is true
        CURLcode curl_init_result = curl_global_init(CURL_GLOBAL_ALL);
        if (curl_init_result != CURLE_OK) {
            string str_msg = "Failed to initialize CURL library: " + string(curl_easy_strerror(curl_init_result));
            LOG_E(TAG, str_msg.c_str());
            nd_service_obj->send_err_msg(SM_E_UPLD_CURL_INIT_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg);
            return -1;
        }
        LOG_I(TAG, "CURL global initialization completed successfully");
    }

    // Read extended attribute config
    extended_attr_enabled = read_extended_attr_config();

    init_upl_cb_msgq();

    setCurrDHUBWifiModeFromDB(); 
    db_handle = create_db (UPLOADER_DB_PATH);
    if( NULL == db_handle ) {
        string str_msg = "Cannot create db";
        LOG_E(TAG, str_msg.c_str() );
        nd_service_obj->send_err_msg(SM_E_UPLD_DB_CREAT_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
        return -1;
    }
    ea_db_handle = create_ea_db(EA_DB_PATH);
    if( NULL == ea_db_handle ) {
        string str_msg = "Couldn't create EA db";
        LOG_E(TAG, str_msg.c_str());
        nd_service_obj->send_err_msg(SM_E_UPLD_DB_CREAT_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
        return -1;
    }
    
    nd_msgq_t::nd_msg_t *msg;
    nd_msgq_t *server_q = nd_msgq_t::get_msgq( Q_NAME, nd_msgq_t::ND_MSGQ_SERVER);
    string observations_path = ALERTS_PATH + "/observations";
    bool sd_card_mounted = check_remount_sd_card();
    if(sd_card_mounted){
        bool obs_status = check_create_dir(observations_path);
        bool obs_status_int = check_create_dir(INT_COMPLETE_OBS_PATH);
        if(obs_status && obs_status_int){
            LOG_I(TAG,"Observations folder exists!!");
            cleanup_old_observations();
            if( copy_pre_reboot_isummary() ) {
                LOG_I(TAG, "copy_pre_reboot_isummary success");
            } else {
                LOG_E(TAG, "copy_pre_reboot_isummary failed");
            }
        }
        else{
            LOG_E(TAG, "Observations folder creation failed. Will retry!!");
        }
    }
    else{
        string str_msg = "CANNOT MOUNT SDCARD. Observations folder is not created!!";
        LOG_E(TAG, str_msg.c_str() );
        nd_service_obj->send_err_msg(SM_E_UPLD_SDCRD_MNT_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
    }

    if(sd_card_mounted){
        //check if sdcard path is accessible
        if(is_ea_path_healthy()){
            if(is_ea_enabled){
                ea_imgs_sync_folder_and_db();
            }
            monitor_ea_folder_size();
        }else{
            string str_msg = "EA images path not healthy. Skipping bootup sync!";
            LOG_E(TAG, str_msg.c_str() );
            nd_service_obj->send_err_msg(SM_E_UPLD_EA_IMGS_DB_SYNC_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
        }
    }else{
        string str_msg = "SDCARD NOT MOUNTED. EA Images folder is not created!!";
        LOG_E(TAG, str_msg.c_str() );
        nd_service_obj->send_err_msg(SM_E_UPLD_SDCRD_MNT_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
    }

    LOG_I(TAG, "Connecting to CB broadcast...");
    string service_name = "NDMB_UPL_SERVICE";
    NDMBClient ea_msg_client(service_name);
    bool ret = ea_msg_client.subscribe(TOPIC_OLDEST_UPLOADABLE_FILE, receive_cb_broadcast);
    LOG_I(TAG, "Subscribing to topic %s: %s", TOPIC_OLDEST_UPLOADABLE_FILE.c_str(), ret ? "success" : "failed");
    if (!ret) {
        LOG_E(TAG, "Subscribing to topic %s failed", TOPIC_OLDEST_UPLOADABLE_FILE.c_str());        
    }

    bool status = start_receive_cb_broadcast_thread();
    if(status == false){
        string str_msg = "CB broadcast receiver thread start failed";
        LOG_E(TAG, str_msg.c_str());
        nd_service_obj->send_err_msg(SM_E_UPLD_CB_DRP_BROADCAST_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg);
    }

    vector< pair<long int, string> > vec_trim_out;
    get_files(TRIM_VOD_OUT.c_str(), vec_trim_out, ".mp4");
    remove_files_from_disk(vec_trim_out);
    vector< pair<long int, string> >().swap(vec_trim_out);

    // clean all the zips in the folder on bootup
    clean_ea_zips(EA_IMGS_ZIPS_PATH);

    priority_queue<req_upload_msg_t*, vector<req_upload_msg_t*>, compare> pq;
    priority_queue<req_upload_msg_t*, vector<req_upload_msg_t*>, compare> pq_misc;

    pthread_t t1;
    pthread_t t2;
    pthread_t t3;
    pthread_t t5;
    pthread_t t6;

    if (pthread_mutex_init(&qlock, NULL) != 0)
    {
        string str_msg = "mutex init failed for qlock";
        LOG_E(TAG, str_msg.c_str() );
        nd_service_obj->send_err_msg(SM_E_UPLD_MUTEX_INIT_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
        return -1;
    }

    if (pthread_mutex_init(&qlock_long, NULL) != 0)
    {
        string str_msg = "mutex init failed for qlock_long";
        LOG_E(TAG, str_msg.c_str() );
        nd_service_obj->send_err_msg(SM_E_UPLD_MUTEX_INIT_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
        return -1;
    }

    if (pthread_mutex_init(&qlock_misc, NULL) != 0)
    {
        string str_msg = "mutex init failed for qlock_misc";
        LOG_E(TAG, str_msg.c_str() );
        nd_service_obj->send_err_msg(SM_E_UPLD_MUTEX_INIT_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
        return -1;
    }

    int rc_thread_create = 0;
    rc_thread_create = pthread_create(&t1, NULL, handle_req_upload, (void *)&pq);
    if(rc_thread_create == 0) {
        pthread_setname_np(t1, TAG_THREAD_P_SMALL);
        LOG_I(TAG, "handle_req_upload thread created successfully");
    }
    else {
        LOG_E(TAG, "pthread_create failed for handle_req_upload. Return code %d", rc_thread_create);
        nd_service_obj->send_err_msg(SM_E_UPLD_THREAD_CREATE_FAIL, rc_thread_create,
                "pthread_create failed for handle_req_upload" );
        return -1;
    }

    rc_thread_create = pthread_create(&t2, NULL, handle_req_upload_long, (void *)&pq_long);
    if(rc_thread_create == 0) {
        LOG_I(TAG, "handle_req_upload_long thread created successfully");
        pthread_setname_np(t2, TAG_THREAD_P_LARGE);
    }
    else {
        LOG_E(TAG, "pthread_create failed for handle_req_upload_long. Return code %d", rc_thread_create);
        nd_service_obj->send_err_msg(SM_E_UPLD_THREAD_CREATE_FAIL, rc_thread_create,
                "pthread_create failed for handle_req_upload_long" );
        return -1;
    }

    rc_thread_create = pthread_create(&t3, NULL, handle_req_upload_misc, (void *)&pq_misc);
    if(rc_thread_create == 0) {
        LOG_I(TAG, "handle_req_upload_misc thread created successfully");
        pthread_setname_np(t3, TAG_THREAD_LOG_UPL);
    }
    else {
        LOG_E(TAG, "pthread_create failed for handle_req_upload_misc. Return code %d", rc_thread_create);
        nd_service_obj->send_err_msg(SM_E_UPLD_THREAD_CREATE_FAIL, rc_thread_create,
                "pthread_create failed for handle_req_upload_misc" );
        return -1;
    }

    rc_thread_create = pthread_create(&t5, NULL, uploader_data_upload_pending_status_thread, NULL);
    if(rc_thread_create == 0) {
        LOG_I(TAG, "uploader_data_upload_pending_status_thread created successfully");
        pthread_setname_np(t5, TAG_THREAD_UPLOADER_DATA_UPLOAD_PENDING_STATUS);
    }
    else {
        LOG_E(TAG, "pthread_create failed for uploader_data_upload_pending_status_thread. Return code %d", rc_thread_create);
        nd_service_obj->send_err_msg(SM_E_UPLD_THREAD_CREATE_FAIL, rc_thread_create,
                "pthread_create failed for uploader_data_upload_pending_status_thread" );
        //no need to terminate the process here, as this thread is not critical
    }

    rc_thread_create = pthread_create(&t6, NULL, handle_vod_elapsed_time, NULL);
    if(rc_thread_create == 0) {
        LOG_I(TAG, "handle_vod_elapsed_time thread created successfully");
        pthread_setname_np(t6, TAG_THREAD_VOD_ELAPSED_TIME);
    }
    else {
        LOG_E(TAG, "pthread_create failed for handle_vod_elapsed_time. Return code %d", rc_thread_create);
        nd_service_obj->send_err_msg(SM_E_UPLD_THREAD_CREATE_FAIL, rc_thread_create,
                "pthread_create failed for handle_vod_elapsed_time" );
        //no need to terminate the process here, as this thread is not critical
    }

    int hs_th_status = create_hs_thread();
    if(hs_th_status != 0) {
        LOG_E(TAG, "Failed to create health status thread. Return code: %d", hs_th_status);
        string str_msg = "Failed to create health status thread";
        LOG_E(TAG, str_msg.c_str() );
        nd_service_obj->send_err_msg(SM_E_UPLD_HS_TH_CREATION_FAIL, hs_th_status, str_msg );
    }

    add_failed_uploads_queue(db_handle, (void *)&pq);

    //Check if the external VODs are present in DB, create relevant thread only if available
    if(check_ext_vod_req_present(db_handle) && !THREAD_FETCH_EXT_VOD_CREATED) {
        bool th_created = create_ext_vod_thread();
        if(th_created == false) {
            LOG_E(TAG, "Failed to create external VOD thread");
            return -1;
        }
    }

    //delete cancelled vod requests from db
    delete_cancelled_vod_requests(db_handle);
    fetch_pending_vods();


#ifdef USE_UPLOADER_TEST
    //print vod queue
    print_queue(&pq_long);
#endif

    if( server_q == NULL ) {
        string str_msg = "Cannot create message queue";
        LOG_E(TAG, str_msg.c_str() );
        nd_service_obj->send_err_msg(SM_E_UPLD_MSGQ_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
        return -1;
    }

    while(1) {
        if( (msg = server_q->receive( )) == NULL ) {
            LOG_E(TAG,"Receive message failed");
            continue;
        }

        generic_msg_t *gen_msg = (generic_msg_t *)msg->get_buffer();
        if(gen_msg == NULL){
            LOG_E(TAG,"Message buffer null. Skipping!!");
            continue;
        }

        LOG_I(TAG,"Message Received: %s len: %d, msg_type : %d",gen_msg->client_id,gen_msg->msg_idx, gen_msg->msg_type);
        bool msg_consumed = true;

        switch( gen_msg->msg_type ) {
            case POWERMON_IGNITION:
                {
                    powermon_ignition_msg_t *igni_msg = (powermon_ignition_msg_t*)gen_msg;
                    LOG_I(TAG, "Ign status = %lld, crank_change_time = %lld, lpw_status = %lld", igni_msg->status, igni_msg->crank_change_time, igni_msg->lpw_status);
                    if(igni_msg->status == static_cast<int64_t>(IGNITION_OFF))
                    {
                        igni_status = 0;
                        uploader_data_upload_pend_cv.notify_one();
                        LOG_I(TAG,"Recieved ignition off");
                    }
                    else if(igni_msg->status == static_cast<int64_t>(IGNITION_ON))
                    {
                        igni_status = 1;

                       LOG_I(TAG,"Recieved ignition on");
                    }

                    if(LOW_POWER_WAKEUP_TRUE == igni_msg->lpw_status)
                    {
                        LOG_I(TAG,"In Low power wake up");
                    }
                    break;
                }
            case REQ_DHUB_WIFI_MODE_FROM_DB:
                {
                    LOG_I(TAG,"RECIEVED GET_DHUB_WIFI_MODE_DB");
                    setCurrDHUBWifiModeFromDB();
                    break;
                }
            case REQ_UPLOAD_ADD_EA_FILE_DB:
                {
                    LOG_I(TAG, "RECEIVED REQ_UPLOAD_ADD_EA_FILE_DB");
                    uploader_add_ea_file_db_msg_t *ea_msg = (uploader_add_ea_file_db_msg_t*)gen_msg;
                    LOG_I(TAG, "EA image added has size %d", ea_msg->file_size_bytes);

                    if(ea_msg->file_size_bytes > TWENTY_FIVE_KB){
                        string str_msg = "EA image size is greater than 25KB";
                        LOG_W(TAG, str_msg.c_str());
                        nd_service_obj->send_err_msg(SM_E_UPLD_EA_IMG_LARGE_FILE_SIZE, NDService::UNUSED_ERR_AUX_CODE, str_msg );
                    }

                    if(ea_msg->base_file_name != ""){
                        if(ea_msg->udid == -1){
                            ea_msg->udid = udid_from_file(ea_msg->base_file_name);
                        }
                        
                        if(ea_msg->session_count == -1){
                            ea_msg->session_count = sessionCount_from_file(ea_msg->base_file_name);
                        }
                        
                        ea_img_status error_code = get_img_status_from_msg(ea_msg);

                        if(!insert_db_ea_imgs(ea_db_handle, ea_msg, error_code)){
                            string str_msg = "Insert EA image in DB failed";
                            LOG_E(TAG, str_msg.c_str());
                            nd_service_obj->send_err_msg(SM_E_UPLD_EA_IMGS_DB_INSERT_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
                        }
                    }
                    break;
                }
            case RES_FETCH_EXT_VOD:
                {
                    res_ext_vod_msg_t *ext_cam_msg = (res_ext_vod_msg_t*)gen_msg;
                    LOG_I(TAG,"ExtCam msg received. vod_id %s, vod_status %d, rgb_status %s", ext_cam_msg->vod_id, ext_cam_msg->vod_status, ext_cam_msg->rgb_status);
                    static req_upload_msg_t *persistent_upload_msg_ptr = (req_upload_msg_t*)malloc(sizeof(req_upload_msg_t));
                    if (nullptr == persistent_upload_msg_ptr) {
                        LOG_E(TAG,"RES_FETCH_EXT_VOD : malloc failed!!");
                        break;
                    }

                    memset(persistent_upload_msg_ptr, 0, sizeof(req_upload_msg_t));

                    string video = EXT_CAM_FILE_PATH + ext_cam_msg->fname;
                    bool query_status = get_vod_req_by_id_and_fname(db_handle, video, ext_cam_msg->vod_id, persistent_upload_msg_ptr);
                    if(query_status == false) {
                        LOG_W(TAG,"Not found any active req in DB: %s : %s, ignoring!", ext_cam_msg->vod_id, video.c_str());
                        break;
                    }

                    ext_vod_status_t ext_vod_status = ext_cam_msg->vod_status;
                    bool ext_vod_add_to_q = false;
                    switch (ext_vod_status)
                    {
                        case EXT_VOD_ACK:
                        {
                            //update DB with status EXT_VOD_ST_ACK
                            set_ext_vod_req_status(db_handle, persistent_upload_msg_ptr, EXT_VOD_ST_ACK);
                            vod_health::ext_vod_ack_t ext_vod_ack;
                            vod_health::VodHealth::send_vod_health(persistent_upload_msg_ptr, vod_health::EXT_VOD_ACK, &ext_vod_ack);
                            break;
                        }
                        case EXT_VOD_SUCCESS:
                        {
                            //Update DB with relavent fields related to ext cam
                            ext_vod_req_status_t ext_vod_req_status = EXT_VOD_ST_DONE;
                            int is_fetched = 1;
                            int not_available = 0;
                            int is_failed = 0;
                            string failure_reason = "NA";
                            string rgb_status = ext_cam_msg->rgb_status;

                            update_ext_vod_stats(db_handle, persistent_upload_msg_ptr, ext_vod_req_status, is_fetched,
                                    not_available, is_failed, failure_reason, rgb_status);

                            
                            vod_health::ext_vod_resp_t ext_vod_resp;
                            ext_vod_resp.ts = get_system_time();
                            ext_vod_resp.status = true;
                            ext_vod_resp.reason = failure_reason;
                            ext_vod_resp.rgb_analysis = rgb_status;
                            ext_vod_resp.is_vod_available = 1;

                            vod_health::VodHealth::send_vod_health(persistent_upload_msg_ptr, vod_health::EXT_VOD_RESP, &ext_vod_resp);

                            //add to upload queue
                            ext_vod_add_to_q = true;

                            break;
                        }
                        case EXT_VOD_FAILURE:
                        {
                            //Update DB with relavent fields related to ext cam
                            ext_vod_req_status_t ext_vod_req_status = EXT_VOD_ST_DONE;
                            int is_fetched = 0;
                            int not_available = 0;
                            int is_failed = 1;
                            string failure_reason = ext_cam_msg->failure_reason;
                            string rgb_status = ext_cam_msg->rgb_status;

                            update_ext_vod_stats(db_handle, persistent_upload_msg_ptr, ext_vod_req_status, is_fetched,
                                    not_available, is_failed, failure_reason, rgb_status);

                            vod_health::ext_vod_resp_t ext_vod_resp;
                            ext_vod_resp.ts = get_system_time();
                            ext_vod_resp.status = false;
                            ext_vod_resp.reason = failure_reason;
                            ext_vod_resp.rgb_analysis = rgb_status;
                            ext_vod_resp.is_vod_available = -1;
                            vod_health::VodHealth::send_vod_health(persistent_upload_msg_ptr, vod_health::EXT_VOD_RESP, &ext_vod_resp);

                            //add to upload queue
                            ext_vod_add_to_q = true;

                            break;
                        }
                        case EXT_VOD_NOT_AVAILABLE:
                        {
                            //Update DB with relavent fields related to ext cam
                            ext_vod_req_status_t ext_vod_req_status = EXT_VOD_ST_DONE;
                            int is_fetched = 0;
                            int not_available = 1;
                            int is_failed = 1;
                            string failure_reason = ext_cam_msg->failure_reason;
                            string rgb_status = ext_cam_msg->rgb_status;

                            update_ext_vod_stats(db_handle, persistent_upload_msg_ptr, ext_vod_req_status, is_fetched,
                                    not_available, is_failed, failure_reason, rgb_status);

                            vod_health::ext_vod_resp_t ext_vod_resp;
                            ext_vod_resp.ts = get_system_time();
                            ext_vod_resp.status = false;
                            ext_vod_resp.reason = failure_reason;
                            ext_vod_resp.rgb_analysis = rgb_status;
                            ext_vod_resp.is_vod_available = 0;
                            vod_health::VodHealth::send_vod_health(persistent_upload_msg_ptr, vod_health::EXT_VOD_RESP, &ext_vod_resp);

                            //add to upload queue
                            ext_vod_add_to_q = true;
                            break;
                        }
                        default:
                        {
                            LOG_E(TAG,"Invalid ext_vod_status %d", ext_vod_status);
                            break;
                        }
                    }
                    if(ext_vod_add_to_q) {
                        req_upload_msg_t *new_ext_vod_req = (req_upload_msg_t*)malloc(sizeof(req_upload_msg_t));
                        if (nullptr != new_ext_vod_req) {
                            memset(new_ext_vod_req, 0, sizeof(req_upload_msg_t));
                            clone_message_received(persistent_upload_msg_ptr, new_ext_vod_req);
                            add_to_vod_upload_Q(new_ext_vod_req);
                        }
                        else {
                            LOG_E(TAG,"new_ext_vod_req is NULL, malloc failed, msg not processed");
                        }

                    }
                    LOG_I(TAG,"ExtCam msg processed");
                    break;
                }
            default:
                {
                    msg_consumed = false;
                    break;
                }
        }

        if(msg_consumed) {
            delete msg;
            continue;
        }

        req_upload_msg_t *upload_msg = (req_upload_msg_t*)gen_msg;

#ifdef USE_UPLOADER_TEST
        log_upload_msg(upload_msg);
        log_upload_msg_size();
#endif

        req_upload_msg_t *new_msg = (req_upload_msg_t*)malloc(sizeof(req_upload_msg_t));
        memset(new_msg, 0, sizeof(req_upload_msg_t));
        clone_message_received(upload_msg, new_msg);
        new_msg->request_time = get_epoch();
        LOG_I(TAG, "epoch: %llu",get_epoch());
        bool msg_added_to_queue = true;
        switch(new_msg->payload_size){
            case PAYLOAD_SMALL:
                if(new_msg->msg_type == REQ_UPLOAD_CRITICAL_LOGS ||
                        new_msg->msg_type == REQ_UPLOAD_NON_CRITICAL_LOGS ||
                        new_msg->msg_type == REQ_UPLOAD_CRITICAL_LOGS_TIME_RANGE ||
                        new_msg->msg_type == REQ_UPLOAD_VOD_LIST) {
                    LOG_I(TAG, "Upload log/vod-list request received. Type = %d", new_msg->msg_type);
                    pthread_mutex_lock(&qlock_misc);
                    pq_misc.push(new_msg);
                    pthread_mutex_unlock(&qlock_misc);
                }
                else if(new_msg->msg_type == REQ_UPLOAD_SIGN_CROPS ){
                    LOG_I(TAG, "Upload Sign Crop request received. Type = %d", new_msg->msg_type);
                    pthread_mutex_lock(&qlock_misc);
                    pq_misc.push(new_msg);
                    pthread_mutex_unlock(&qlock_misc);
                }
                else if(new_msg->msg_type == REQ_UPLOAD_EVENT){
#ifdef BAGHEERA
					string md5sum = copy_alert_to_int_buff(new_msg->json_fname);
#else
					string md5sum = copy_alert_to_circular_buff(new_msg->json_fname);
#endif
                    if(md5sum.length() > 1){
#ifdef BAGHEERA
					    // send_msg to internal_buffer
                        send_alert_copied_message_ib(new_msg);
                        LOG_I(TAG,"Copied alert %s to internal buffer folder", new_msg->json_fname);
#else
						// send_msg to circular_buffer
                        send_alert_copied_message_cb(new_msg);
                        LOG_I(TAG,"Copied alert %s to circular buffer folder", new_msg->json_fname);
#endif
						string alert_file = new_msg->json_fname;

                        string session_name = "";
                        if(!get_session_name_from_string(alert_file, session_name)){
                            LOG_E(TAG, "Invalid filename: %s",alert_file.c_str());
                        }
                        alert_file = alert_file + ".zip";
                        strcpy(new_msg->fname,alert_file.c_str());
                        insert_db (db_handle, new_msg, U_NEW, md5sum);
                        pthread_mutex_lock(&qlock);
                        pq.push(new_msg);
                        pthread_mutex_unlock(&qlock);

                        //uploadadd time
                        int64_t upload_add = get_system_time();
                        send_alert_upload_info_healthstats(session_name, upload_add, -1);

                        collect_signal_info(session_name, 0, false);
                    }
                    else{
                        msg_added_to_queue = false;
                        LOG_E(TAG,"Cannot copy alert!!! :%s", new_msg->json_fname);
                    }
                } else if (new_msg->msg_type == REQ_UPLOAD_OBSERVATIONS) {
                    if (false == is_obs_req_present) {
                            pthread_mutex_lock(&qlock);
                                pq.push(new_msg);
                                is_obs_req_present = true;
                                LOG_I(TAG, "changed is_obs_req_present to true");
                            pthread_mutex_unlock(&qlock);
                    } else {
                        LOG_I(TAG, "is_obs_req_present is true. Skipping");
                    }
                } else if(new_msg->msg_type == REQ_UPLOAD_EA_IMAGES) {
                    pthread_mutex_lock(&qlock_misc);
                    if(is_ea_imgs_req_present == false) {      
                        pq_misc.push(new_msg);
                        is_ea_imgs_req_present = true;
                        LOG_I(TAG, "changed is_ea_imgs_req_present to true");
                    } else {
                        LOG_I(TAG, "is_ea_imgs_req_present is true. Skipping");
                    }
                    pthread_mutex_unlock(&qlock_misc);
                }
                else {
                    LOG_I(TAG,"Invalid message type: %d", new_msg->msg_type);
                }
                break;
            case PAYLOAD_LARGE:
                upload_status_t iot_response = UPLOAD_ACK;
                LOG_I(TAG, "VOD req received. Id: %llu, vod_id: %s , video: %s , cancelled: %d, priority: %d",
                        new_msg->req_id, new_msg->vod_id, new_msg->fname, new_msg->cancelled, new_msg->level);
                string video = new_msg->fname;
                bool is_ext_vod = is_external_video(video);
                do {
                    if(new_msg->cancelled) {
                        //vod cancelled, Mark in DB and delete entry from DB in next boot
                        bool cancelled = mark_vod_cancelled(db_handle, new_msg);
                        LOG_I(TAG, "VOD marked as cancelled: %d for %s. Will be cleared in next boot", cancelled, new_msg->vod_id);

                        if(is_ext_vod) {
                            //init of external VOD thread if not already created
                            if(!THREAD_FETCH_EXT_VOD_CREATED) {
                                bool th_created = create_ext_vod_thread();
                                if(th_created == false) {
                                    LOG_E(TAG, "Failed to create external VOD thread");
                                    return -1;
                                }
                            }
                            //reset the ext_vod_req_status so that the cancellation is communicated to Ext Cam service
                            set_ext_vod_req_status(db_handle, new_msg, EXT_VOD_ST_NEW);
                            add_to_vod_fetch_Q(new_msg);
                        }
                        break;
                    }
                    if (is_vod_req_available(db_handle, new_msg) ){
                        if(is_vod_priority_changed(db_handle, new_msg)) {
                            LOG_I(TAG, "VOD priority changed to %d. Id: %llu, vod_id: %s, video: %s .", new_msg->level, new_msg->req_id, new_msg->vod_id, new_msg->fname);
                            bool cancelled = mark_vod_cancelled(db_handle, new_msg);
                            LOG_I(TAG, "Priority changed: older VOD marked as cancelled: %d for %s. Will be cleared in next boot", cancelled, new_msg->vod_id);
                            //the new VOD request will be added to the queue below
                        }
                        else {
                            LOG_C(TAG, "Ignoring. VOD req already exists. Id: %llu, vod_id: %s, video: %s .", new_msg->req_id, new_msg->vod_id, new_msg->fname);
                            break;
                        }
                    }

                    //check if the total VOD requests are less than MAX_VOD_REQUESTS
                    if(get_total_vod_req_count(db_handle) >= MAX_VOD_Q_SIZE) {
                        LOG_E(TAG, "Pending VOD request count exceeded.  VOD: %s : awsiot response 'err'", new_msg->vod_id);
                        iot_response = UPLOAD_FAIL_OTHER;
                        break;
                    }

                    strcpy(new_msg->json_fname,new_msg->fname);


                    bool vod_persisted = insert_db_vod(db_handle, new_msg, is_ext_vod);
                    if(vod_persisted) {
                        LOG_I(TAG, "VOD req added to DB. Id: %llu, vod_id: %s, external: %d, video: %s.", new_msg->req_id, new_msg->vod_id, is_ext_vod, new_msg->fname);
                        
                        vod_health::add_req_db_t add_req_db;
                        add_req_db.ts = get_system_time();
                        int rank;
                        if (!get_vod_rank(db_handle, new_msg, rank)) {
                            LOG_E(TAG, "Failed to get rank for VOD request: %s", new_msg->vod_id);
                            LOG_E(TAG, "Setting rank to -1");
                            rank = -1;
                        }
                        add_req_db.rank = rank;
                        vod_health::VodHealth::send_vod_health(new_msg, vod_health::REQ_ADD_UPLOAD_Q, &add_req_db);

                    }
                    else {
                        LOG_E(TAG,"Failed to persist VOD request: %s", new_msg->vod_id);
                    }

                    new_msg->failure_retry_count = 0;

                    if(is_ext_vod) {
                        //late init of external VOD thread if not already created
                        if(!THREAD_FETCH_EXT_VOD_CREATED) {
                            bool th_created = create_ext_vod_thread();
                            if(th_created == false) {
                                LOG_E(TAG, "Failed to create external VOD thread");
                                return -1;
                            }
                        }
                        //simply request to Ext Cam service regardless of video availability in CB path
                        add_to_vod_fetch_Q(new_msg);
                    }
                    else {
                        add_to_vod_upload_Q(new_msg);
                    }
                }
                while(false);

                bool msg_status = false;
                msg_status = send_vod_ack_or_err(upload_msg, iot_response);
                LOG_I(TAG,"VOD req ACK (%d) sent to client_id %s : %d", iot_response, new_msg->client_id, msg_status);

                send_device_vod_count(upload_msg);
                break;
        }
        LOG_I(TAG,"Added to Queue: %d", msg_added_to_queue);
        delete msg;
    }
    request_hs_thread_shutdown();
    pthread_mutex_destroy(&qlock);
    pthread_mutex_destroy(&qlock_long);
    pthread_mutex_destroy(&qlock_ext_vod);
    pthread_mutex_destroy(&qlock_drp);
    LOG_I(TAG, "Mutexes destroyed successfully");
    nd_service_obj->release_service_obj();
    LOG_I(TAG, "NDService object released successfully");
    curl_global_cleanup();
    LOG_I(TAG, "CURL global cleanup completed");
    printf("Exiting Uploader!!\n");
    LOG_I(TAG, "Exiting Uploader!!");
}