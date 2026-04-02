/* Copyright (C) 2016 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Y Suresh Kumar <suresh.kumar@netradyne.com>, November 2016
 */
#include "circular_buffer.h"
#include <sqlite3.h> 
#include <stdio.h>

#include <cstdlib>
#include <unistd.h>
#include <curl/curl.h>
#include <fstream>
#include <nd_msg_utils.h> 
#include <sys/statvfs.h>
#include <unordered_map>
#include <unordered_set>
#include <nd_file_utils.h>
#include "service_utils.h"
#include "system_utils.h"
#include "nd_msg_types.h"
#include "nd_db_utils.h"
#include "nd_prop_utils.h"
#include "nd_ext_cam_utils.h"
//for mount command
#include <sys/mount.h>
#include <errno.h>

#include <glob.h>
#include "buffer_common_utils.h"
#include <nd_auth_openssl.h>
#include <nd_auth_utils.h>
#include <healthstats_utils.h>
#include <nd_utils.h>
#include <sys/ipc.h>
#include <sys/msg.h>

#define NEW_Q 1
#if NEW_Q

int generic_queue_push, generic_queue_pop;

#include <queue>
#include <memory>
#include <condition_variable>
#include <utility>
#include <mutex>

std::mutex mutex_genq;
std::condition_variable condition;
std::mutex mutex_health_msg;
std::mutex mutex_send_storage_info;
std::condition_variable send_storage_info_cond;
std::atomic<bool> send_storage_info_ready(false);
std::atomic<bool> process_file_size_check_queue_ready(false);

#endif

using namespace std;

static const char *TAG="CB";
ND_DeviceFactory *nd_device_obj = NULL;
#define ROUTE_LOGS
#define MAX_ALERT_HQ_FILE 80
#define Q_NAME_TO_UPL "Q_TO_UPL"
#define Q_NAME_TO_CB "Q_TO_CB"
#define hours_to_milliseconds 60*60*1000
#define DEFAULT_VALUE_UPL_VID_ENABLED_COLUMN 1
static int dump_payload_hours = 0;
#define MAX_PAYLOAD_DUMP_HOURS 24
#define MIN_PAYLOAD_DUMP_HOURS 0
static const string PREFIX_VIDLIST_PAYLOAD_LOG = "/home/ubuntu/.nddevice/log/circ_buff/log_json";
#if defined (KRAIT) || defined(BAGHEERA2)
#define NO_SDCARD
#endif

//#define SDCARD_FSCK
class sdcard_recovery_ctx;
//nd service object, to detect critical Errors which will be send to Health stats and cloud
NDService *nd_service_obj = NULL ;
static const string DEF_INI_DEVICE_VERSION = "0.0.0";
static const string DEF_INI_SESSION_ID = "none";
static const string DEF_INI_DEVICE_ID = "none";
static const string DEF_INI_DEVICE_TYPE = "bagheera";
static const string DEF_INI_PERCENT_CIRC_BUFF = "90";
static const string DEF_INI_STORAGE_TIER = "1";
static const string DEF_INI_SERVER = "prod";
static const string DEF_INI_API_VERSION = "v1";
static const string DEF_INI_SERVER_URL = "https://idms.netradyne.com/restserver/api";
static string old_path = "";
static string sdcard_img_path = "";

static const string ld_extn = ".ld.mp4";
static int64_t sessionCountForMiscFile = -1;
static int64_t udidForMiscFile = -1;
static const string circ_buff_reboot_token_file = "/dev/shm/circ_buff_reboot_token_file.bin";
static bool first_after_boot = true;
static const int max_size_generic_queue_priority = 20;
static const int max_size_generic_queue = 120;
static int64_t sessionCount_alert_file = 0 ;
bool store_dms_file = true ;
bool store_lq_dms_file = false ;
bool dms_camera_enabled = false ;
static const string store_lq_dms_file_config_str = "store_lq_dms_file";
int num_of_session_to_delete_dms_file = INT_MAX;
static int64_t service_start_time;

bool video_list_sent = false;
bool deleteUnkownFileType = false;
static bool root_monitor_enabled = false;


#ifdef SDCARD_FSCK
static const int FSCK_COMMAND_TASK_TIMEOUT = 120;
static const string sdcard_device_node = "/dev/mmcblk1p1";
#endif

const string file_path_delay_cb_response = DEV_SHM + "/" + nd_xattr + "cb_delay_response";

circular_buffer_vid *CIRC_BUFF_ctx = circular_buffer_vid::get_circular_buffer("", "");
req_storage_health_msg_t *req_health_msg = NULL;

static const int64_t month_in_seconds = 30*24*60*60;
static const int64_t minutes_in_millisec = 60000;
static const string upl_to_cb_folder = "/dev/shm/upl_cb/";
int64_t FILLING_LIMIT;
int64_t FILLING_LIMIT_PERCENT = 95; // default % is 50
int64_t RESERVED_SPACE = 0;
static const int OPERATE_DELTA_SIZE = 80; // File size increase amount when operated
static const string DEF_INI_MOUNT_SDCARD = "false";
bool MOUNT_SDCARD = true;          // Mount SdCard to emmc or sdcard
bool save_ext_cam_files_in_dhub = true;
int64_t current_sessionCount = 0;
int64_t current_udid = 0;
extern bool drp_enabled ;
extern int64_t drp_clock_hours ;
NDMBServer server(SERVICE_CB);
const string DEFAULT_USE_UNLIKE_STR = "false";
// This value is used to determine the circular buffer capacity
// 1 -> max possible storage with 64 GB EMMC
// 2 -> max storage possible with 128 GB EMMC

constexpr int MAX_CLOUD_NOTIFY_RETRY = 5;
static const int cstring_len = 150;
static const int sleep_before_SdCard_insertion = 10;

static const int MAX_FILLING_LIMIT_TIER = 4;
extern int max_alert_hq_file ;
// Memory units come in 64,128,256,512 etc
/*
    STORAGE TIER            STORAGE HOURS EXPECTED
    64GB                         ~50Hrs
    128GB                        ~100Hrs
    256GB                        ~200Hrs
    512GB                        ~400Hrs
*/
int FILLING_LIMIT_TIER[MAX_FILLING_LIMIT_TIER] = { 64, 
                     	                           128, 
	                                              256,
                                                  512};
 
int get_previous_DMS_file_index(int64_t sessionCnt) ;
bool sync_directories(string src_for_sync, string dest_for_sync);
bool card_stats();
void* sdcard_recovery_thread_main(void* args);

bool send_storage_info(int64_t time);
int check_and_delete_null_row_db();
//variable that enable/disables smart transcoding
extern bool SMART_TC_ENABLE;
/* variable that starts transcoding only after sync_directories() function is
 * complete
 */
extern bool START_TRANSCODING;
#define PRI_Q <<"\""   //// helps in json creation


typedef enum circular_buffer_notify_type
{
    CLOUD_NOTIFY = -2 ,
    HEALTH_NOTIFY = -1
}circular_buffer_notify_type_t;

bool collect_notify_videolist(pair < vector< json_add_del >, vector< json_add_del > > &add_del_files, int& files_added, int& files_deleted);
bool check_and_delete_alert_hq_file(sqlite3 *db);
bool check_and_delete_alert_dms_file(sqlite3 *db);
int64_t drp_calcualte_timestamp_uploader(const char* fileName); 
string trim_whitespace(const std::string& str) ;
int64_t get_timeStamp_from_DB(int64_t);
// function to get oldest and latest session from db
bool oldestNewest_Session();
static void populate_add_file_msg_from_db_entry(const file_data_str_db_t& db_entry, circular_buffer_fileinfo_t& file_info);
// func to check limits of spl files (start, stop, alert) future requirment
bool check_limits(sqlite3 *db, circular_buffer_filetype_t file_type, int file_size)
{
    return true;
}

/*
 * update_file_DB call add_file_DB which adds an entry to
 * the database if no entry for that filename is found
 * if entry with the filename is present in db, it updates
 * the file size of the entry in the db.
 *
 */
bool update_file_DB(circular_buffer_fileinfo_t fileinfo){

    return add_file_DB(fileinfo);
}
static void dump_videolist_payload_to_file(const string &url,
                                          const string &payload,
                                          CURLcode curl_res,
                                          uint32_t dump_payload_hours)
{
    
    uint64_t current_time_ms = get_system_monotonic_time();
    static uint64_t dump_duration_ms = dump_payload_hours * hours_to_milliseconds;
    if(current_time_ms  > dump_duration_ms) {
        //stop dumping payloads after specified duration
        return;
    }
    const std::string videolist_payload_failures_log_file =
        std::string(PREFIX_VIDLIST_PAYLOAD_LOG) +"_" +
        std::to_string(get_system_time()) + ".log";
    std::ofstream out(videolist_payload_failures_log_file.c_str(), std::ios::out | std::ios::app);
    if (!out.is_open()) {
        return;
    }

    out << "time_ms=" << get_system_time() << "\n";
    out << "curl_res=" << (int)curl_res << "\n";
    out << "url=" << url << "\n";
    out << "payload:\n";
    out << payload;
    if (payload.empty() || payload.back() != '\n') {
        out << "\n";
    }
}

bool send_video_list()
{
    fill_circular_buffer_header();
    int cloud_notify = CLOUD_NOTIFY;
    pair < vector< json_add_del >, vector< json_add_del > > add_del_files;
    bool cloud_notify_success = collect_notify_videolist(add_del_files, cloud_notify, cloud_notify);
    if( true == cloud_notify_success ){
        LOG_I(TAG, "collect_notify_videolist is succeded");
        bool res = update_add_del_files_db(add_del_files);
        if( false == res ){
            LOG_E(TAG, "Handle this; update_add_del_files_db failed");
        }
        add_del_files.first.clear();
        add_del_files.second.clear();
    }
    else
    {
        LOG_I(TAG, "collect_notify_videolist is FAILED");
    }
    return cloud_notify_success;
}

bool adding_miscs_file_to_DB(string src_file)
{
    int file_type = CIRCULAR_BUFFER_TYPE_TEXT;
    int cam_num = INVALID_CAM_NUM;
    string extn = "";
    string file_path = "";

    file_path = get_file_path(src_file);
    if(file_path == "") {
        LOG_E(TAG, "File is not available");
        return false;
    }

    if( src_file.find(".tmp.mp4") != src_file.npos ) {
        /* Note tmp file will have the extn .mp4.tmp.mp4 */
        LOG_I(TAG, "Found temp file %s", src_file.c_str());
        string tmp_base_file_name = src_file.substr(0, src_file.find(".tmp.mp4"));
        LOG_I(TAG, "src_file.substr %s", tmp_base_file_name.c_str());
        if( file_size( file_path + "/" + tmp_base_file_name ) > 0 ) {
            LOG_I(TAG, "Already contains original file %s; deleting .tmp.mp4 file", tmp_base_file_name.c_str());
            /* Update the transcode status to TRANSCODE_WAITING making the file
             * available again for transcoding. This is because the tmp file may
             * be a partially copied file and it is safer to re-transcode the
             * original file. This situation occurs due to abrupt shutdown of
             * device / service and should be a rare occurrence. Hence the
             * additional cost of re-transcoding is justified. Updating the
             * transcode status to reflect this.
             */
            LOG_I(TAG, "Making the original file %s available for transcoding", tmp_base_file_name.c_str());
            bool ret = update_tc_status_DB(tmp_base_file_name.c_str(), CIRCULAR_BUFFER_TC_STATUS_WAITING);
            if (!ret){
                LOG_E(TAG, "Failed to update tc status for %s to WAITING", tmp_base_file_name.c_str());
            }
            // Note file deletion needs to occur irrespective of the update function
            // success or failing because file in this instance is a tmp transcoded
            // file and the original file is already present
            /* file deletion occurs outside this function on returning false */
            return false;
        }
        else {
            if( rename(  ( file_path + "/" + src_file).c_str(), 
                        ( file_path + "/" + tmp_base_file_name).c_str() ) < 0 ) {
                LOG_E(TAG, "moving misc .tmp.mp4 file failed; deleting .tmp.mp4 file");
                return false;
            }
            src_file = src_file.substr(0, src_file.find(".tmp.mp4"));
        }
    }

    // Read extended attributes for misc files if use extended attributes enabled
    circular_buffer_fileinfo_t file_info = {0};
    bool miscs_file_add= false;
    string fullfile_name =nd_device_obj->get_external_eMMC_mount_path() + src_file;
    if (CIRC_BUFF_ctx->extended_attr_enabled) {
        LOG_I(TAG, "Reading metadata from xattrs to add entry in DB for this miscs file: %s", fullfile_name.c_str());
        miscs_file_add=read_file_metadata_from_xattrs( src_file, file_info);
    } 
    if(miscs_file_add == false){
    // dont add if file is not of type .txt, .mp4, .mkv, .json, .zip, .aac
    int count_format = std::count(src_file.begin(), src_file.end(), '_');
    size_t position = src_file.find_last_of(".");
    if ( position != src_file.npos ) {
        extn = src_file.substr( src_file.find_last_of(".") );
    }
    LOG_D(TAG, "extn %s, count_format %d", extn.c_str(), count_format);

    if ( !(extn == ".mp4" || extn == ".zip" || extn == ".mkv" || extn == ".txt" || extn == ".json" || extn == ".aac" ) )
    {
        LOG_E(TAG, "extn %s, count_format %d. Not part of regular file; deleting %s",
            extn.c_str(), count_format, src_file.c_str());
        delete_file_from_sdcard(fullfile_name);
        return false;
    }

    // Video with ext .mp4/.mkv are considered as type (3) 
    if((extn == ".mp4") || (extn == ".mkv")){
        file_type = CIRCULAR_BUFFER_TYPE_NORMAL;
    }
    // Audio with ext .aac are considered as type (6) 
    if(extn == ".aac" ){
        file_type = CIRCULAR_BUFFER_TYPE_TEXT;
    }
    // Text files with ext .txt/.json/.zip are considered as type (7)
    // Note: .zip is used for shield update and .json is used for alerts
    // and .txt is used for alerts.
    if((extn == ".zip") || (extn == ".txt") || (extn == ".json")){
        file_type = CIRCULAR_BUFFER_TYPE_TEXT_ALERTS;
    }
    string first = src_file.substr(0,1);
    if( !string_to_integer(first, cam_num) ) {
        LOG_E(TAG, "failed in string_to_integer,  continuing as cam 3");
    }
    if( cam_num < CIRCULAR_BUFFER_CAM_FRONT || cam_num >= CIRCULAR_BUFFER_CAM_ERROR) {
        LOG_E(TAG, "unknown cam_num %d, ", cam_num);
        cam_num = INVALID_CAM_NUM;
        LOG_E(TAG, "cam_num changed to %d", cam_num);
    }

    int64_t timestamp = 0;
    try {
        timestamp = extractTimestamp(src_file);
        if(timestamp > 0){
            timestamp = convert_epoch_format(timestamp, DigitsOfEpoch::eDigits_MilliSeconds);
        }
    } catch (const std::runtime_error& e) {
        timestamp = 0;
        LOG_E(TAG, "Failed to get the time stamp from misc file : %s Error : %s", src_file.c_str(), e.what());
    }

    
    file_info.tc_status = CIRCULAR_BUFFER_TC_STATUS_WAITING;
    // when adding files as misc files as a part of
    // circular_buffer.db crash issue, including time stamp of file as
    // DRP need time stamp from db to delete the file for clock_hours delete boundary.
    file_info.time           = timestamp;
    strcpy(file_info.base_file_name, src_file.c_str());
    string fullfile_name = file_path + "/" + src_file;
    file_info.file_size       = file_size(fullfile_name);

    if( file_info.file_size <= 0 ){
        LOG_E(TAG, "unable to find the file_size/GetFileSize returned %d", file_info.file_size);
        return false;
    }
    LOG_I(TAG, "%s: GetFileSize: %d", file_info.base_file_name, file_info.file_size );
    file_info.file_type      = (circular_buffer_filetype_t)file_type;
    file_info.camtype        = (circular_buffer_camtype_t)cam_num;
    file_info.duration       = 60000;
    // upl_vid_enabled enabled means upload privacy is disabled. by defaul upload video should be enabled.
    // Details: https://netradyne.atlassian.net/browse/DT-954?focusedCommentId=359586
    file_info.upl_vid_enabled = DEFAULT_VALUE_UPL_VID_ENABLED_COLUMN;
    file_info.rec_vid_enabled = DEFAULT_VALUE_REC_VID_ENABLED_COLUMN;
    LOG_I(TAG, "adding misc file, set upl_vid_enabled: %d, rec_vid_enabled %d", file_info.upl_vid_enabled, file_info.rec_vid_enabled);

    if( src_file.find(ld_extn) != src_file.npos ) {
        file_info.tc_status = CIRCULAR_BUFFER_TC_STATUS_TRANSCODED;
    }
    update_udid_sessionCount_from_filename(file_info);
    }

    // Read status from extended attributes for misc files
    circular_buffer_filestatus_t status = CIRCULAR_BUFFER_STATUS_NEW;
    if(CIRC_BUFF_ctx->extended_attr_enabled){
        if(get_file_xattr(fullfile_name, FileMetadataKey::status, &status, sizeof(status)) == XATTR_OK) {
            LOG_I(TAG, "File %s status from xattr : %d", fullfile_name.c_str(), status);
        }else{
            LOG_I(TAG, "Failed to get status xattr for file %s, using default status %d", fullfile_name.c_str(), status);
        }
    }
    // Read compression status from extended attributes for misc files
    circular_buffer_file_compression_t compression = CIRCULAR_BUFFER_MEDIUM_COMPRESSION;
    if(CIRC_BUFF_ctx->extended_attr_enabled){
        if(get_file_xattr(fullfile_name, FileMetadataKey::compr_type, &compression, sizeof(compression)) == XATTR_OK) {
            LOG_I(TAG, "File %s compression from xattr : %d", fullfile_name.c_str(), compression);
        }else{
            LOG_I(TAG, "Failed to get compression xattr for file %s, using default compression %d", fullfile_name.c_str(), compression);
        }
    }

    bool ret = add_file_DB(file_info, BATCH_MODE_ADD, status, compression);
    if(ret == false){
        LOG_E(TAG, "adding misc file: Error in add_file_DB");
        return false; // to delete file if it was not added to db
    }
    return true;
}

bool update_file_size_db(string filename, int64_t file_size){
    circular_buffer_fileinfo_t fileinfo = {0};
    if( filename.length() > FNAME_LEN){    
        LOG_C(TAG, "filename length %d is greater than FNAME_LEN %d, filename %s", filename.length(), FNAME_LEN, filename.c_str());
    }
    nd_strncpy(fileinfo.base_file_name, filename.c_str(), (filename.length() < FNAME_LEN) ? filename.length()+1 : FNAME_LEN); // +1 for null termination
    fileinfo.file_size = file_size;
    string extn = "";
    size_t position = filename.find_last_of(".");
    if ( position != filename.npos ) {
        extn = filename.substr( filename.find_last_of(".") );
    }
    // Video with ext .mp4/.mkv are considered as type (3) 
    if((extn == ".mp4") || (extn == ".mkv")){
        fileinfo.file_type = CIRCULAR_BUFFER_TYPE_NORMAL;
    }
    // Audio with ext .aac are considered as type (6) 
    if(extn == ".aac" ){
        fileinfo.file_type = CIRCULAR_BUFFER_TYPE_TEXT;
    }
    // Text files with ext .txt/.json/.zip are considered as type (7)
    // Note: .zip is used for shield update and .json is used for alerts
    // and .txt is used for alerts.
    if((extn == ".txt") || (extn == ".json") || (extn == ".zip")){
        fileinfo.file_type = CIRCULAR_BUFFER_TYPE_TEXT_ALERTS;
    }
    fileinfo.time = 0; // time is not used in this case
    fileinfo.udid = 0; // udid is not used in this case
    fileinfo.sessionCount = 0; // sessionCount is not used in this case
    update_udid_sessionCount_from_filename(fileinfo);
    fileinfo.camtype =  (circular_buffer_camtype_t) get_cam_num_from_filename(fileinfo.base_file_name);
    fileinfo.duration = 60000; // duration is not used in this case
    fileinfo.tc_status = CIRCULAR_BUFFER_TC_STATUS_WAITING;
    fileinfo.upl_vid_enabled = UPL_VID_ENABLED_DO_NOT_UPDATE;
    fileinfo.rec_vid_enabled = REC_VID_ENABLED_DO_NOT_UPDATE;
    LOG_I(TAG," update_file_size_db: fileinfo.base_file_name %s, fileinfo.file_size %d",
        fileinfo.base_file_name, fileinfo.file_size);
    bool ret = add_file_DB(fileinfo);
    return ret;
}

bool delete_file_from_sdcard(string src_file)
{
    int64_t del_starttime = -1, del_endtime = -1;
    if(src_file.find("/") == string::npos){
            string file_path = "";

        file_path = get_file_path(src_file);
        if(file_path == "") {
            LOG_E(TAG, "File is not available");
            return false;
        }
        src_file.assign(file_path + "/" + src_file);

    }
    del_starttime = get_system_time();
    bool status = true;
    if(true == file_is_present(src_file)){
        status = file_delete(src_file, CIRC_BUFF_ctx->use_unlink); // Passing true to use unlink, instead of remove
    }
    del_endtime = get_system_time();
    fill_map_del_file_healthstats(src_file, "sdcard", status, del_starttime, del_endtime);
    return status;
}

enum notify_header_var {
    NOTIFY_HEADER_SESID,
    NOTIFY_HEADER_DEVID,
    NOTIFY_HEADER_KEY,
    NOTIFY_HEADER_DEVVER,
    NOTIFY_HEADER_DEVTYPE,
    NOTIFY_HEADER_VER,
    NOTIFY_HEADER_CLIPDUR,
    NOTIFY_HEADER_ISCOMP,

    NOTIFY_HEADER_UNKNOWN
};
static const string notify_header[32] = 
{
    "\"session_id\": ", 
    ", \"device_id\": ", 
    ", \"key\": ", 
    ", \"deviceversion\": ", 
    ", \"devicetype\": ", 
    ", \"ver\": ", 
    ", \"default_clip_duration\": ",
    ", \"isCompleteList\": "
};

enum notify_db_overview_var {
    NOTIFY_DBOVERVIEW_OLDESTINDEX,
    NOTIFY_DBOVERVIEW_UNKNOWN
};

static const string notify_db_overview[8] =
{
    "\"oldestVideoIndex\": "
};

enum notify_db_info_var {
    NOTIFY_DBINFO_DBIDENTIFIER,
    NOTIFY_DBINFO_DBIDENTIFIERCREATIONTIME,
    NOTIFY_DBINFO_UNKNOWN,
};

static const string notify_db_info[8] =
{
     "\"dbIdentifier\": ",
    ", \"dbIdentifierCreationTime\": "
};

enum notify_adddel_var {
    NOTIFY_ADDDEL_FILENAME,
    NOTIFY_ADDDEL_DURATION,
    NOTIFY_ADDDEL_PRIORITY,
    NOTIFY_ADDDEL_DBINDEX,
    NOTIFY_ADDDEL_UDID,
    NOTIFY_ADDDEL_SESSIONCOUNT,

    NOTIFY_ADDDEL_UNKNOWN
};
static const string notify_add_del[32] = 
{
    "\"filename\": ",
    ", \"duration\": ",
    ", \"priority\": ",
    ", \"dbIndex\": ",
    ", \"udid\": ",
    ", \"sessionCount\": "
};

static bool convert_header_json(circular_buffer_header_t header, string &header_string)
{
    std::stringstream str_stream;
    string complete = header.iscomplete == 0 ? "false" : "true" ;

    str_stream \
        << notify_header[NOTIFY_HEADER_SESID]       PRI_Q   << header.session_id  PRI_Q\
        << notify_header[NOTIFY_HEADER_DEVID]       PRI_Q   << header.device_id  PRI_Q\
        << notify_header[NOTIFY_HEADER_KEY]         PRI_Q   << header.key  PRI_Q\
        << notify_header[NOTIFY_HEADER_DEVVER]      PRI_Q  << header.device_version  PRI_Q\
        << notify_header[NOTIFY_HEADER_DEVTYPE]     PRI_Q  << header.devicetype  PRI_Q\
        << notify_header[NOTIFY_HEADER_VER]         PRI_Q  << header.version  PRI_Q\
        << notify_header[NOTIFY_HEADER_CLIPDUR]             << header.dafault_clip_durtn\
        << notify_header[NOTIFY_HEADER_ISCOMP]              << complete ;
        
    header_string = str_stream.str();
    return true;
}

static bool convert_db_overview_json(string &db_overview_string, int oldest_video_index)
{
    std::stringstream str_stream;

    str_stream \
        << notify_db_overview[NOTIFY_DBOVERVIEW_OLDESTINDEX] << oldest_video_index;

    db_overview_string = str_stream.str();
    return true;
}

static bool convert_db_info_json(string &db_info_string, string db_identifier, int64_t db_creation_time)
{
    std::stringstream str_stream;

    str_stream \
        << notify_db_info[NOTIFY_DBINFO_DBIDENTIFIER] PRI_Q  << db_identifier PRI_Q\
        << notify_db_info[NOTIFY_DBINFO_DBIDENTIFIERCREATIONTIME] << db_creation_time;

    db_info_string = str_stream.str();
    return true;
}

static bool convert_add_del_array_json(string &add_string, vector< json_add_del >add_files)
{
    std::stringstream str_stream;

    int i = 0;
    int add_files_len = add_files.size();

    if(add_files_len == 0)
    {
        LOG_D(TAG, "add_files_len is 0");
        add_string = "";
        return true;
    }

    LOG_D(TAG, "convert_add_del_array_json array len is %d", add_files_len);

    while(i<(add_files_len-1))
    {
        
        json_add_del &add_str = add_files[i];
        string type_str = "";
        if( add_str.file_type == CIRCULAR_BUFFER_TYPE_START)
        {
            type_str = "ON";
        }
        else if( add_str.file_type == CIRCULAR_BUFFER_TYPE_NORMAL)
        {
            type_str = "GENERAL";
            // Sessions with Alerts to be pushed into Alert Queue will be marked with CIRCULAR_BUFFER_NO_COMPRESSION.
            // CAM 0/1 HQ FILES
            // CAM 2,3 Ext Camers 4,5,6,7 and DMS Camera 8 LQ FILES will be updated.
            if( (add_str.alert_type == CIRCULAR_BUFFER_NO_COMPRESSION)
                || (add_str.alert_type == CIRCULAR_BUFFER_NO_COMPRESSION_ADJ)
                || (add_str.alert_type == CIRCULAR_BUFFER_NO_COMPRESSION_NO_LQ)) {
		        type_str = "ALERT";
	        }
        }
        else if( add_str.file_type == CIRCULAR_BUFFER_TYPE_ALERT)
        {
            type_str = "ALERT";
        }
        else if( add_str.file_type == CIRCULAR_BUFFER_TYPE_STOP)
        {
            type_str = "OFF";
        }
        else
        {
            LOG_E(TAG, "Error HERE handle this case");
            LOG_E(TAG, "add_str.file_type %d", add_str.file_type);
            i++;
            continue;
        }
        
        stringstream ss_filename;
        ss_filename << add_str.file_name;
        string filename_temp;
        while (getline(ss_filename, filename_temp, '/' ));
        if( filename_temp == "" )
            filename_temp = add_str.file_name;

        str_stream << "{" \
            << notify_add_del[NOTIFY_ADDDEL_FILENAME]    PRI_Q  << filename_temp PRI_Q\
            << notify_add_del[NOTIFY_ADDDEL_DURATION]           << add_str.duration \
             << notify_add_del[NOTIFY_ADDDEL_PRIORITY]    PRI_Q  << type_str PRI_Q\
            << notify_add_del[NOTIFY_ADDDEL_DBINDEX]    << add_str.index_id \
            << notify_add_del[NOTIFY_ADDDEL_UDID]    << add_str.udid \
            << notify_add_del[NOTIFY_ADDDEL_SESSIONCOUNT]    << add_str.sessionCount << "}, ";
        i++;
    }   

    json_add_del &add_str = add_files[i];

    string type_str = "";
    if( add_str.file_type == CIRCULAR_BUFFER_TYPE_START)
    {
        type_str = "ON";
    }
    else if( add_str.file_type == CIRCULAR_BUFFER_TYPE_NORMAL)
    {
        type_str = "GENERAL";
        // Sessions with Alerts to be pushed into Alert Queue will be marked with CIRCULAR_BUFFER_NO_COMPRESSION.
        // CAM 0/1 HQ FILES
        // CAM 2,3 Ext Camers 4,5,6,7 and DMS Camera 8 LQ FILES will be updated.
        if( (add_str.alert_type == CIRCULAR_BUFFER_NO_COMPRESSION)
            || (add_str.alert_type == CIRCULAR_BUFFER_NO_COMPRESSION_ADJ)
            || (add_str.alert_type == CIRCULAR_BUFFER_NO_COMPRESSION_NO_LQ)) {
		    type_str = "ALERT";
	    }
    }
    else if( add_str.file_type == CIRCULAR_BUFFER_TYPE_ALERT)
    {
        type_str = "ALERT";
    }
    else if( add_str.file_type == CIRCULAR_BUFFER_TYPE_STOP)
    {
        type_str = "OFF";
    }
    else
    {
        LOG_E(TAG, "Error HERE handle this case");
        return false;
    }
    
    stringstream ss_filename;
    ss_filename << add_str.file_name;
    string filename_temp;
    while (getline(ss_filename, filename_temp, '/' ));
    if( filename_temp == "" )
        filename_temp = add_str.file_name;

    str_stream << "{" \
        << notify_add_del[NOTIFY_ADDDEL_FILENAME]    PRI_Q  << filename_temp PRI_Q\
        << notify_add_del[NOTIFY_ADDDEL_DURATION]           << add_str.duration \
        << notify_add_del[NOTIFY_ADDDEL_PRIORITY]    PRI_Q  << type_str PRI_Q\
        << notify_add_del[NOTIFY_ADDDEL_DBINDEX]    << add_str.index_id \
        << notify_add_del[NOTIFY_ADDDEL_UDID]    << add_str.udid \
        << notify_add_del[NOTIFY_ADDDEL_SESSIONCOUNT]    << add_str.sessionCount << "} ";

    add_string = str_stream.str();

    return true;
}

static bool form_Json(string &add_del_json, pair < vector< json_add_del >,
                    vector< json_add_del > > &add_del_files, int oldest_video_index,
                    string db_identifier, int64_t db_creation_time)
{
    string header_string = "", add_string = "", del_string = "", db_overview_string = "", db_info_string = "";

    std::stringstream str_stream;
    bool ret;
    ret = convert_header_json(CIRC_BUFF_ctx->header, header_string);
    ret = ret && convert_db_overview_json(db_overview_string, oldest_video_index);
    ret = ret && convert_db_info_json(db_info_string, db_identifier, db_creation_time);
    ret = ret && convert_add_del_array_json(add_string, add_del_files.first);
    ret = ret && convert_add_del_array_json(del_string, add_del_files.second);
    if(ret == false){
        LOG_E(TAG, "Failed in form_jsons");
        return false;
    }
    
    str_stream  << "{" << endl << header_string << ", " << endl \
                << "\"circBuffDbOverview\" :" << endl << "{" << endl << db_overview_string << endl << "}, " << endl \
                << "\"dbInfo\" :" << endl << "{" << endl << db_info_string << endl << "}, " << endl \
                << "\"added\" : [" << endl <<  add_string << "], " << endl \
                << "\"deleted\" : [" << endl <<  del_string << "]" << endl \
                << "}" << endl;

    add_del_json = str_stream.str();

    //printf("add_del_json :: %s\n", add_del_json.c_str());
    return true;
}


static bool init_config() {

    bool status = true, status1 = true, status2 = true, status3 = true;
    CIRC_BUFF_ctx->device_config = new Config_parser(DEVICE_CONFIG_INI);
    if( CIRC_BUFF_ctx->device_config->getParseStatus() != true) {
        LOG_E(TAG, "Cannot allocate Config DEVICE_CONFIG_INI");
        status = false;
    }
    CIRC_BUFF_ctx->nd_config = new Config_parser(ND_DEVICE_INI);
    if( CIRC_BUFF_ctx->nd_config->getParseStatus() != true) {
        LOG_E(TAG, "Cannot allocate Config ND_DEVICE_INI");
        status1 = false;
    }
    CIRC_BUFF_ctx->bagheera_config = new Config_parser(BAGHEERA_CONFIG_INI);
    if( CIRC_BUFF_ctx->bagheera_config->getParseStatus() != true) {
        LOG_E(TAG, "Cannot allocate Config BAGHEERA_CONFIG_INI");
        status2 = false;
    }

    CIRC_BUFF_ctx->cloud_config = new Config_parser(CLOUD_CONFIG_INI);
    if( CIRC_BUFF_ctx->cloud_config->getParseStatus() != true) {
        LOG_E(TAG, "Cannot allocate Config CLOUD_CONFIG_INI");
        status3 = false;
    }
    return status && status1 && status2 && status3;
}

static bool deinit_config() {

    bool retval1 = false;
    bool retval2 = false;
    bool retval3 = false;
    bool retval4 = false;

    if (CIRC_BUFF_ctx->device_config)
    {
        delete CIRC_BUFF_ctx->device_config;
        CIRC_BUFF_ctx->device_config = NULL;
        retval1 = true;
    }
    else
    {
        LOG_E(TAG, "CIRC_BUFF_ctx->device_config == null");
    }

    if (CIRC_BUFF_ctx->nd_config)
    {
        delete CIRC_BUFF_ctx->nd_config;
        CIRC_BUFF_ctx->nd_config = NULL;
        retval2 = true;
    }
    else
    {
        LOG_E(TAG, "CIRC_BUFF_ctx->nd_config == null");
    }

    if (CIRC_BUFF_ctx->bagheera_config)
    {
        delete CIRC_BUFF_ctx->bagheera_config;
        CIRC_BUFF_ctx->bagheera_config = NULL;
        retval3 = true;
    }
    else
    {
        LOG_E(TAG, "CIRC_BUFF_ctx->bagheera_config == null");
    }

    if (CIRC_BUFF_ctx->cloud_config)
    {
        delete CIRC_BUFF_ctx->cloud_config;
        CIRC_BUFF_ctx->cloud_config = NULL;
        retval4 = true;
    }
    else
    {
        LOG_E(TAG, "CIRC_BUFF_ctx->cloud_config == null");
    }

    return retval1 && retval2 && retval3 && retval4;
}
void read_payload_dump_config(){
     Config_parser cfg_prsr(BAGHEERA_CONFIG_INI);
    if (!cfg_prsr.getParseStatus()) {
        LOG_E(TAG, "Can't parse %s for payload dump config", BAGHEERA_CONFIG_INI);
        dump_payload_hours = MIN_PAYLOAD_DUMP_HOURS;
        return;
    }
    bool get_override_val = true;
    bool is_val_overridden = false;
    std::string payload_dump_str = cfg_prsr.getConfig("sdcard", "payload_dump_interval", "0", get_override_val, is_val_overridden);
   if(string_to_integer(payload_dump_str, dump_payload_hours) == false){
        LOG_E(TAG, "Failed to read payload_dump_interval config from bagheera_config.ini");
        dump_payload_hours = MIN_PAYLOAD_DUMP_HOURS;
    } 
    if((dump_payload_hours < MIN_PAYLOAD_DUMP_HOURS) || (dump_payload_hours > MAX_PAYLOAD_DUMP_HOURS))
    {
        LOG_I(TAG, "payload_dump_interval read from ini is %u hours, not in valid range setting it to 0", dump_payload_hours);
        dump_payload_hours = MIN_PAYLOAD_DUMP_HOURS;
    }

    LOG_I(TAG, "payload_dump_interval read from ini is %u hours", dump_payload_hours);
    return;
}
bool notify_cloud(string add_del_json)
{
    //get auth token
    string auth_header = "";
    bool header_status = get_auth_header(auth_header);

    if(!header_status) {
        LOG_E(TAG, "Corrupted jwt, Not connecting to cloud !");
        return false;
    }
    //// logic post to cloud reply with success or failure
    CURL *curl;
    struct curl_slist *headers = NULL;
    CURLcode res;

    if(init_config() == false)
    {
        LOG_E(TAG, "init_config() failed in notify_cloud(). Default values set.");
    }
    string cloud_server = CIRC_BUFF_ctx->cloud_config->getConfig("cloud","server",DEF_INI_SERVER);
    string server_url = CIRC_BUFF_ctx->cloud_config->getConfig(cloud_server,"injestion",DEF_INI_SERVER_URL);
    string version = CIRC_BUFF_ctx->cloud_config->getConfig("cloud","injection-version",DEF_INI_API_VERSION);
    deinit_config();

    
    curl = curl_easy_init();
    if(!curl) { LOG_E(TAG, "Early init failed"); return false; }

    struct string_t s;
    init_string(&s);
    
    string devtype = "X-DeviceType: " + CIRC_BUFF_ctx->header.devicetype;
    string header_device_id = "X-DeviceId: " + CIRC_BUFF_ctx->header.device_id;
    
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, devtype.c_str());
    headers = curl_slist_append(headers, header_device_id.c_str());
    headers = curl_slist_append(headers, auth_header.c_str());    

    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers); 
    /* Request type :: POST */
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");
    /* URL to post */
    
    string url = server_url + "/" + version + "/" + VIDLIST_URL;
    LOG_I(TAG, "URL @@ %s", url.c_str());
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    /* MAX timeout */ // YSK
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, VIDLIST_CURL_TIMEOUT);

    // Specifying cerificate path explicitly; as libcurl is not picking certs from the default path
#ifdef CA_CERT_PATH
    curl_easy_setopt(curl, CURLOPT_CAPATH, "/etc/ssl/certs");
#endif
    /* size of the POST data */
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, strlen(add_del_json.c_str()));
    /* pass in a pointer to the data - libcurl will not copy */
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, add_del_json.c_str());

    // write functions
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);

    res = curl_easy_perform(curl);
    /* Check for errors */ 
    if(res != CURLE_OK)
      LOG_I(TAG, "curl_easy_perform() failed @ %s",
              curl_easy_strerror(res));

    /* always cleanup */ 
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    headers = NULL;

    curl_global_cleanup();
    if(res != CURLE_OK) {
        LOG_C(TAG, "curl_easy_perform() failed @ %s",
                curl_easy_strerror(res));
        nd_service_obj->send_err_msg(SM_E_CB_CREATION_FAIL, res, "Cloud notify failed, response code " + to_string(res));
    }else{
        LOG_I(TAG, "res %d , CURLE_OK %d ", res, CURLE_OK);
    }
    if(dump_payload_hours > MIN_PAYLOAD_DUMP_HOURS)
    {
        dump_videolist_payload_to_file(url, add_del_json, res, dump_payload_hours);
    }
    string responce = "";
    if(s.ptr) {
        responce = string(s.ptr);
        LOG_I(TAG, "string @@%s@@", s.ptr);
        free(s.ptr);
    }

    if(res != CURLE_OK) {
        return false;
    }

    if(responce.find(RESPONSE_JWT_HEADER_MISSING) != string::npos ||
        responce.find(RESPONSE_JWT_SIG_INVALID) != string::npos ||
            responce.find(RESPONSE_JWT_ALG_INVALID) != string::npos) {
        notify_key_corruption();
    }

    LOG_D(TAG, "responce @@%s@@", responce.c_str());

    // return true only if responce contains true and Videolist saved
    LOG_D(TAG, " true %d Videolist saved %d" , responce.find("true"), responce.find("Videolist saved"));
    return ( (responce.find("true") != responce.npos ) && 
             ( responce.find("Videolist saved")  != responce.npos) );
}

bool send_sdcard_mount_circbuffer (bool force)
{
    circular_buffer_sdcard_mount_msg_t msg;
    msg.time = get_system_time();
    msg.force_mount = force ;
    LOG_C(TAG, "Sending msg to Diagnostic service ");
    return (send_msg ((generic_msg_t*)&msg, REQ_CIRCBUFF_SDCARD_MOUNT, sizeof (msg),
                CIRC_BUFF_ctx->circular_buffer_q_name, diagnostic_q_name, 0));
}

#ifdef SDCARD_FSCK
void run_fsck_command() {
    string cmd = "fsck -t ext4 -nf " + sdcard_device_node;
    string resp = "";

    bool ret = system_execute_with_resp("RUN_FSCK", cmd, resp);
    if(!ret) {
        LOG_E(TAG, "failed to execute fsck run command");
        exit(1);
    }

    LOG_I(TAG, "resp: %s", resp.c_str());

    // parse the fsck response 
    stringstream ss(resp);
    string prev_line = "", line = "";

    string error_msg = "";
    int aux_code = 1;    

    while(std::getline(ss, line, '\n')) {
        if(line == "") {
            continue;
        }

        size_t pos = line.find("Fix?");

        if(pos == string::npos) {
            prev_line = line;
            continue;
        }

        if(pos == 0) {
            error_msg = prev_line;
        } else {
            error_msg = line.substr(0, pos);
        }

        LOG_I(TAG, "sending fsck error: %s to sm", error_msg.c_str());
        nd_service_obj->send_err_msg(SM_E_CB_FSCK_ERRORS, aux_code, error_msg);

        // increment aux code for every error message we sent so that sm do not drop back to back messages
        aux_code++; 
    }

    if(aux_code == 1) {
        LOG_I(TAG, "No fsck errors detected");
    }

    exit(0);
}

void fork_fsck_run_process() {

    pid_t pid = fork();

    if (pid < 0) {
        LOG_E(TAG, "Failed to fork process for fsck command run");
        return;
    }

    if (pid == 0){
        run_fsck_command();
    } else {
        LOG_I(TAG, "fsck run child process launched with pid = %d", pid);
        task_status_t tc_process_status = nd_set_timeout_for_task(pid, FSCK_COMMAND_TASK_TIMEOUT);
        switch (tc_process_status){
            case TASK_STATUS_SUCCESS:
                LOG_I(TAG, "fsck run child process %d successfully completed", pid);
                break;
            case TASK_STATUS_FAILED:
                LOG_E(TAG, "fsck run child process %d failed", pid);
                nd_service_obj->send_err_msg(SM_E_CB_FSCK_CMD_FAIL, NDService::UNUSED_ERR_AUX_CODE, "fsck command run failed");
                break;
            case TASK_STATUS_KILLED:
                LOG_E(TAG, "fsck run child process %d killed", pid);
                nd_service_obj->send_err_msg(SM_E_CB_FSCK_CMD_FAIL, NDService::UNUSED_ERR_AUX_CODE, "fsck command run killed");
                break;
            default:
                LOG_E(TAG, "fsck run child process %d unexpected return. Assuming it failed", pid);
                break;
        }
    }
}
#endif // SDCARD_FSCK
#if NEW_Q
char* block_wait_pop() { // blocking pop
    char* return_ptr = NULL;
    bool popped = false;
    LOG_D(TAG, "going for a blocking wait");
    std::unique_lock<std::mutex> lk(mutex_genq);
    condition.wait(lk, []{return ( (!CIRC_BUFF_ctx->generic_queue.empty()) || (!CIRC_BUFF_ctx->generic_queue_priority.empty()) ); });
////////////////// only for special package. TBD : delete it ///////////////////
    static int q_size = 0  ;
    static int pq_size = 0  ;
////////////////////////////////////////////////////////////////////////////////
    if(!CIRC_BUFF_ctx->generic_queue_priority.empty()) {
        return_ptr = (char *)CIRC_BUFF_ctx->generic_queue_priority.front();
        CIRC_BUFF_ctx->generic_queue_priority.pop();
        popped = true;
        pq_size++;
    }

    if( (!popped) && (!CIRC_BUFF_ctx->generic_queue.empty()) ) {
        return_ptr = (char *)CIRC_BUFF_ctx->generic_queue.front();
        CIRC_BUFF_ctx->generic_queue.pop();
        q_size++;
    }
    generic_queue_pop++;

    LOG_I(TAG, "poping message %d popped: %d, q_size: %d %d, pq_size: %d %d", generic_queue_pop, popped, CIRC_BUFF_ctx->generic_queue.size(), q_size, CIRC_BUFF_ctx->generic_queue_priority.size(), pq_size);
    return return_ptr;
}

void* sysv_messageq_thread_main(void* args) {

    nd_msgq_t::nd_msg_t *msg;

    while(1) {

        if( (msg = CIRC_BUFF_ctx->db_main_msg_q->receive( )) == NULL ) {
            LOG_C(TAG, "Receive message failed");
            continue;
        }
        circular_buffer_generic_msg_t *g_msg = (circular_buffer_generic_msg_t *)msg->get_buffer();
        if(g_msg == NULL)
            continue;

        // needed this loop since g_msg->len received is not correct always.
        g_msg->len = 0;
        bool priority = false;
        switch( g_msg->type ) {
            case REQ_CIRCULAR_BUFFER_CLEAN_DB:
                g_msg->len = sizeof(circular_buffer_generic_msg_t);
                priority = true;
            break;
            case REQ_CIRCULAR_BUFFER_COPY_ADD_FILE_DB:
                g_msg->len = sizeof(circular_buffer_copy_add_file_db_msg_t);
            break;
            case REQ_CIRCULAR_BUFFER_ADD_FILE_DB_FOR_SAVE_EXT_CAMERA_FILES_IN_DHUB:
                g_msg->len = sizeof(circular_buffer_add_file_db_msg_t);
            break;
            case REQ_CIRCULAR_BUFFER_ADD_FILE_DB:
                g_msg->len = sizeof(circular_buffer_add_file_db_msg_t);
            break;
            case REQ_CIRCULAR_BUFFER_UPDATE_FILE_DB:
                g_msg->len = sizeof(circular_buffer_update_file_db_msg_t);
            break;
            case REQ_CIRCBUFF_SDCARD_MOUNT: // This is not expected as message moved to diagnostic service
                g_msg->len = sizeof(circular_buffer_sdcard_mount_msg_t);
                priority = true;
            break;
            case REQ_CIRCULAR_BUFFER_FILE_COMPRESSION: // This is high priority as this message is used for alert files, for updating compression status
                g_msg->len = sizeof(circular_buffer_file_compression_msg_t);
                priority = true;
            break;
            case REQ_CIRCULAR_BUFFER_SDCARD_FSCK_CHECK: // This is not expected as message moved to diagnostic service
                g_msg->len = sizeof(circular_buffer_generic_msg_t);
                priority = true;
            break;
            case REQ_CIRCULAR_BUFFER_UPDATE_FILE_DB_PYTHON:
                LOG_I(TAG, "REQ_CIRCULAR_BUFFER_UPDATE_FILE_DB_PYTHON received");
                g_msg->len = sizeof(circular_buffer_update_file_db_msg_python_t);
                priority = false; // python msg is not priority, it was added mistakenly as priority, changing it back to false
                break;
            case REQ_CIRCULAR_BUFFER_ADD_FILE_DB_PYTHON:
                LOG_I(TAG, "REQ_CIRCULAR_BUFFER_ADD_FILE_DB_PYTHON received");
                g_msg->len = sizeof(circular_buffer_add_file_db_msg_python_t);
                priority = false; // mentinoned above
                break;
            case REQ_CIRCULAR_BUFFER_NOTIFY_VIDEO_LIST_TO_CLOUD:
                LOG_I(TAG, "Received REQ_CIRCULAR_BUFFER_NOTIFY_VIDEO_LIST_TO_CLOUD");
                g_msg->len = sizeof(data_record_metrics_t);
                break;
            case REQ_STORAGE_INFO:
                g_msg->len = sizeof(req_storage_health_msg_t);
                LOG_I(TAG, "Received REQ_STORAGE_INFO");
                //send_storage_info(); //changed by Abhishek
		        break;
            case ERROR:
            default:
                g_msg->len = 0;
                LOG_E(TAG, "Unknown message %d received", g_msg->type);
            break;
        }

        if (g_msg->len == 0) {
            LOG_E(TAG, "g_msg->len is 0 not storing the message");
            delete msg;
            continue;
        }

        char* new_message = (char*)malloc(g_msg->len);
        if(!new_message) {
            LOG_E(TAG, "Unable to malloc; continue in sysv_messageq_thread");
            delete msg;
            continue;
        }
#if 0
        // debug logs will remove soon
        for (int cnt = 0; cnt<g_msg->len;cnt++) {
            printf("%c",((char*)g_msg)[cnt]);
        }
#endif
        LOG_D(TAG, "g_msg->len %d g_msg->type %d priority: %d", g_msg->len, g_msg->type, priority);

        memcpy(new_message, g_msg, g_msg->len);
        if (priority == true) { 
            std::lock_guard<std::mutex> lk(mutex_genq);
            if(CIRC_BUFF_ctx->generic_queue_priority.size()  >= max_size_generic_queue_priority){
                LOG_I(TAG, "No space in generic_queue_priority a internal queue to store message");
                nd_service_obj->send_err_msg(SM_E_CB_INTERNAL_QUEUE_FULL, NDService::UNUSED_ERR_AUX_CODE, "No space in generic_queue_priority" ); // TBD no need to send this msg.
                continue;
            }
            CIRC_BUFF_ctx->generic_queue_priority.push(new_message);
            condition.notify_one();
         }
        else {
            std::lock_guard<std::mutex> lk(mutex_genq);
            if(CIRC_BUFF_ctx->generic_queue.size()  >= max_size_generic_queue){
                LOG_E(TAG, "No space in generic_queue g_msg->type %d", g_msg->type );
                nd_service_obj->send_err_msg(SM_E_CB_INTERNAL_QUEUE_FULL, NDService::UNUSED_ERR_AUX_CODE, "No space in generic_queue" );
                continue;
            }
            CIRC_BUFF_ctx->generic_queue.push(new_message);
            condition.notify_one();
        }
        generic_queue_push++;

        LOG_I(TAG, "pushing message %d", generic_queue_push);
        LOG_I( TAG, "generic_queue length %d generic_queue_priority length %d", 
                CIRC_BUFF_ctx->generic_queue.size(),  CIRC_BUFF_ctx->generic_queue_priority.size() );

        delete msg;
    }

}
#endif

void process_file_size_check_queue() {
    int q_size = 0;
    {
        std::lock_guard<std::mutex> lk(CIRC_BUFF_ctx->mutex_process_file_size_check_queue);
        q_size = CIRC_BUFF_ctx->file_size_check_queue.size();
        if(q_size <= CIRC_BUFF_ctx->MIN_PROCESS_FILE_SIZE_CHECK_QUEUE) {
            LOG_C(TAG," process_file_size_check_queue() called but queue size is %d, returning", q_size);
            return ;
        }
    }
    int count = 0;
    LOG_I(TAG," process_file_size_check_queue() called but queue size is %d, processing", q_size);
    while((count <= CIRC_BUFF_ctx->MIN_PROCESS_FILE_SIZE_CHECK_QUEUE)) {
        string src_file = "";
        int64_t db_file_size = 0;
        {
            std::lock_guard<std::mutex> lk(CIRC_BUFF_ctx->mutex_process_file_size_check_queue);
            if(true == CIRC_BUFF_ctx->file_size_check_queue.empty()) {
                LOG_I(TAG, "file_size_check_queue is empty, returning");
                return ;
            }
            auto front_ele = CIRC_BUFF_ctx->file_size_check_queue.front();
            CIRC_BUFF_ctx->file_size_check_queue.pop();
            src_file = front_ele.first;
            db_file_size = front_ele.second;
        }
        if(src_file.empty()) {
            LOG_E(TAG, "src_file is empty, skipping file size check");
            continue;
        }
        string src_file_path = nd_device_obj->get_external_eMMC_mount_path() + "/" + src_file;
	int64_t dir_file_size = file_size(src_file_path);
        if(db_file_size != dir_file_size) {
            LOG_I(TAG, "File size mismatch for file %s db_file_size: %llu dir_file_size: %llu", src_file_path.c_str(), db_file_size, dir_file_size);
            if(false == update_file_size_db(src_file, dir_file_size)) {
                LOG_E(TAG, "Failed to update file size in DB for file %s", src_file.c_str());
            }
        } else {
            LOG_D(TAG, "File size match for file %s db_file_size: %ld dir_file_size: %d", src_file.c_str(), db_file_size, dir_file_size);
        }
        count++;
    }
    return ;
}

void* send_storage_info_main(void* args) {
    LOG_I(TAG, "inside send_storage_info_main() " );

    while(1) {
        LOG_D(TAG, "send_storage_info_main() inside while loop will wait on condition" );
        std::unique_lock<std::mutex> lk(mutex_send_storage_info);
        send_storage_info_cond.wait(lk, []{ return (send_storage_info_ready || process_file_size_check_queue_ready); });
        LOG_I(TAG, "send_storage_info_main() inside while loop, got notified ... " );
        if(true == send_storage_info_ready){
            mutex_health_msg.lock();
            int64_t health_msg_time = req_health_msg->time ;
            mutex_health_msg.unlock();
            send_storage_info(health_msg_time);
            send_storage_info_ready = false;
        }
        process_file_size_check_queue();
        process_file_size_check_queue_ready = false;
    }
    LOG_I(TAG, "Exiting send_storage_info_main() " );
}

void signal_process_file_size_check(string filename, int64_t file_size) {

    //No need to check file size mismatch for dms lq file when store_lq_dms_file is false.
    if( (filename.find(ld_extn) != string::npos) && (filename.find("8_trip") != string::npos) && store_lq_dms_file == false) {
        return;
    }	
    std::lock_guard<std::mutex> lk(CIRC_BUFF_ctx->mutex_process_file_size_check_queue);
    CIRC_BUFF_ctx->file_size_check_queue.push({filename, file_size});
    LOG_I(TAG, "Pushed file %s with size %ld to file_size_check_queue", filename.c_str(), file_size);
    // If the queue size exceeds the maximum limit, signal the process_file_size_check_queue
    // to process the queue and check file sizes.  
    if(CIRC_BUFF_ctx->file_size_check_queue.size() >= CIRC_BUFF_ctx->MAX_FILE_SIZE_CHECK_QUEUE) {
        LOG_I(TAG, "Signalling process_file_size_check_queue");
        process_file_size_check_queue_ready = true;
        send_storage_info_cond.notify_one();
    }
}

void clone_convert_python_message(void *msg, void *new_msg,msg_type_t type){
    if(msg == NULL){
        LOG_E(TAG,"clone_convert_python_message() null value receved in msg");
        return ;
    }
    circular_buffer_add_file_db_msg_python_t *base_msg = (circular_buffer_add_file_db_msg_python_t *) msg;
    circular_buffer_add_file_db_msg_t *base_new_msg = (circular_buffer_add_file_db_msg_t *) new_msg;
    if(type == REQ_CIRCULAR_BUFFER_ADD_FILE_DB){
        base_new_msg->msg_type = REQ_CIRCULAR_BUFFER_ADD_FILE_DB;
    }else if(type == REQ_CIRCULAR_BUFFER_UPDATE_FILE_DB){
        base_new_msg->msg_type = REQ_CIRCULAR_BUFFER_UPDATE_FILE_DB;
    }else{
        LOG_E(TAG,"clone_convert_python_message() Invalid message type received");
        return ;
    }
    base_new_msg->length = base_msg->length;
    strcpy(base_new_msg->client_id,base_msg->client_id);
    base_new_msg->msg_idx = base_msg->msg_idx;
    base_new_msg->res_reqd = base_msg->res_reqd;
    strcpy(base_new_msg->file_info.base_file_name,base_msg->file_info.base_file_name);
    string_to_int64(base_msg->file_info.time, base_new_msg->file_info.time);
    base_new_msg->file_info.duration = base_msg->file_info.duration;
    base_new_msg->file_info.file_size = base_msg->file_info.file_size;
    base_new_msg->file_info.file_type = base_msg->file_info.file_type;
    base_new_msg->file_info.camtype = base_msg->file_info.camtype;
    base_new_msg->file_info.tc_status = base_msg->file_info.tc_status;
    base_new_msg->file_info.upl_vid_enabled = DEFAULT_VALUE_UPL_VID_ENABLED_COLUMN;
    base_new_msg->file_info.rec_vid_enabled = DEFAULT_VALUE_REC_VID_ENABLED_COLUMN;
    base_new_msg->file_info.udid = -1; //hardcoding as this is not sent from python
    base_new_msg->file_info.sessionCount = -1; //hardcoding as this is not sent from python
}

int callback_count_entry_available(void* data, int argc, char** argv, char** azColName) {

    int32_t *count = (int32_t *)data;
    if(argc != 1) {
        *count = -1;
    } else {
        *count = atoi(argv[0]);
    }
    return 0;

}

bool check_sdcard_mounted(void) {
    string cmd = "df -h | grep " + nd_device_obj->get_external_eMMC_old_mount_path() +" | awk '{print $6}'", response = "";
    if(!system_execute_with_resp("CHECK_MOUNT_POINT", cmd, response)) {
        LOG_E(TAG,"Failed to execute mount check command", cmd.c_str());
        return false;
    }

    if(response.find(nd_device_obj->get_external_eMMC_old_mount_path()) != string::npos) {
        LOG_I(TAG,"/media/Sdcard is mounted : %s", response.c_str());
        return true;
    } else {
        LOG_I(TAG,"/media/Sdcard is not mounted : %s", response.c_str());
        return false;
    }
}

int64_t get_service_uptime()
{
    int64_t service_uptime = get_system_monotonic_time() - service_start_time ;
    return service_uptime;
}

int print_storage_info(res_storage_health_msg_t storage_info) {

    LOG_I(TAG, "num LQ files 0 = %ld", storage_info.no_of_lq_0);
    LOG_I(TAG, "num LQ files 1 = %ld", storage_info.no_of_lq_1);
    LOG_I(TAG, "num LQ files ext cam = %ld", storage_info.no_of_lq_N);
    LOG_I(TAG, "LQ file size 0 = %f", storage_info.size_of_lq_0);
    LOG_I(TAG, "LQ file size 1 = %f", storage_info.size_of_lq_1);
    LOG_I(TAG, "LQ files size ext cam = %f", storage_info.size_of_lq_N);
    LOG_I(TAG, "num HQ files 0 = %ld", storage_info.no_of_hq_0);
    LOG_I(TAG, "num HQ files 1 = %ld", storage_info.no_of_hq_1);
    LOG_I(TAG, "num HQ files 2 = %ld", storage_info.no_of_hq_2);
    LOG_I(TAG, "num HQ files 3 = %ld", storage_info.no_of_hq_3);
    LOG_I(TAG, "num HQ files ext cam = %ld", storage_info.no_of_hq_N);
    LOG_I(TAG, "HQ file size 0 = %f", storage_info.size_of_hq_0);
    LOG_I(TAG, "HQ file size 1 = %f", storage_info.size_of_hq_1);
    LOG_I(TAG, "HQ file size 2 = %f", storage_info.size_of_hq_2);
    LOG_I(TAG, "HQ file size 3 = %f", storage_info.size_of_hq_3);
    LOG_I(TAG, "HQ file size ext cam = %f", storage_info.size_of_hq_N);
    LOG_I(TAG, "num partial file 0 = %ld", storage_info.no_of_partial_0);
    LOG_I(TAG, "num partial file 1 = %ld", storage_info.no_of_partial_1);
    LOG_I(TAG, "num partial file 2 = %ld", storage_info.no_of_partial_2);
    LOG_I(TAG, "num partial file 3 = %ld", storage_info.no_of_partial_3);
    LOG_I(TAG, "partial file size 0 = %f", storage_info.size_of_partial_0);
    LOG_I(TAG, "partial file size 1 = %f", storage_info.size_of_partial_1);
    LOG_I(TAG, "partial file size 2 = %f", storage_info.size_of_partial_2);
    LOG_I(TAG, "partial file size 3 = %f", storage_info.size_of_partial_3);
    LOG_I(TAG, "num Aud files = %ld", storage_info.no_of_audio_files);
    LOG_I(TAG, "Aud file size = %f", storage_info.size_of_audio_files);
    LOG_I(TAG, "num Obs files = %ld", storage_info.no_of_obs_files);
    LOG_I(TAG, "Obs file size = %f", storage_info.size_of_obs_files);
    LOG_I(TAG, "avail_video_storage_min = %ld", storage_info.avail_video_storage_min);
    LOG_I(TAG, "no_of_files_added = %ld", storage_info.no_of_files_added);
    LOG_I(TAG, "no_of_files_deleted = %ld", storage_info.no_of_files_deleted);
    LOG_I(TAG, "oldest video time stamp = %ld", storage_info.ovd_ts);
    LOG_I(TAG, "oldest video latitude = %f", storage_info.ovd_lat);
    LOG_I(TAG, "oldest video longitude = %f", storage_info.ovd_lon);
    LOG_I(TAG, "oldest video udid = %s", storage_info.ovd_udid);


}

bool send_storage_info(int64_t time)
{
    res_storage_health_msg_t storage_info;
    storage_info.time = time;
    storage_info.no_of_lq_0 = -1;
    storage_info.no_of_lq_1 = -1;
    storage_info.no_of_lq_N = -1;
    storage_info.size_of_lq_0 = -1.0;
    storage_info.size_of_lq_1 = -1.0;
    storage_info.size_of_lq_N = -1.0;
    storage_info.no_of_hq_0 = -1;
    storage_info.no_of_hq_1 = -1;
    storage_info.no_of_hq_2 = -1;
    storage_info.no_of_hq_3 = -1;
    storage_info.no_of_hq_N = -1;
    storage_info.size_of_hq_0 = -1.0;
    storage_info.size_of_hq_1 = -1.0;
    storage_info.size_of_hq_2 = -1.0;
    storage_info.size_of_hq_3 = -1.0;
    storage_info.size_of_hq_N = -1.0;
    storage_info.no_of_partial_0 = -1;
    storage_info.no_of_partial_1 = -1;
    storage_info.no_of_partial_2 = -1;
    storage_info.no_of_partial_3 = -1;
    storage_info.size_of_partial_0 = -1.0;
    storage_info.size_of_partial_1 = -1.0;
    storage_info.size_of_partial_2 = -1.0;
    storage_info.size_of_partial_3 = -1.0;
    storage_info.no_of_audio_files = -1;
    storage_info.size_of_audio_files = -1.0;
    storage_info.no_of_obs_files = -1;
    storage_info.size_of_obs_files = -1.0;
    storage_info.avail_video_storage_min = -1;
    storage_info.no_of_files_added = -1;
    storage_info.no_of_files_deleted = -1;
    storage_info.ovd_ts = -1;
    storage_info.ovd_lat = -1.0;
    storage_info.ovd_lon = -1.0;
    memset(&storage_info.ovd_udid, 0 , sizeof(storage_info.ovd_udid));

    storage_info.service_uptime = get_service_uptime();
    get_num_lq_vid_files(storage_info.no_of_lq_0,storage_info.no_of_lq_1,storage_info.no_of_lq_N,storage_info.size_of_lq_0,storage_info.size_of_lq_1,storage_info.size_of_lq_N);
    get_num_hq_vid_files(storage_info.no_of_hq_0,storage_info.no_of_hq_1,storage_info.no_of_hq_2,storage_info.no_of_hq_3,storage_info.no_of_hq_N,storage_info.size_of_hq_0,storage_info.size_of_hq_1,storage_info.size_of_hq_2,storage_info.size_of_hq_3,storage_info.size_of_hq_N);
    get_partial_files_info(storage_info.no_of_partial_0,storage_info.no_of_partial_1,storage_info.no_of_partial_2,storage_info.no_of_partial_3,storage_info.size_of_partial_0,storage_info.size_of_partial_1,storage_info.size_of_partial_2,storage_info.size_of_partial_3);
    get_num_audio_files(storage_info.no_of_audio_files,storage_info.size_of_audio_files);
    get_Observation_files_details(storage_info.no_of_obs_files,storage_info.size_of_obs_files);
    storage_info.avail_video_storage_min = get_available_video_storage_min();
    
    pair < vector< json_add_del >, vector< json_add_del > > add_del_files;
    collect_notify_videolist(add_del_files, storage_info.no_of_files_added, storage_info.no_of_files_deleted);
    add_del_files.first.clear();
    add_del_files.second.clear();

    std::string udid = "";
    std::string fileName = "";
    int ret = get_oldest_video_details(storage_info.ovd_ts,storage_info.ovd_lat,storage_info.ovd_lon,udid, fileName);
    if(ret < 0){
        fileName = "";
        udid = "-1";
        LOG_E(TAG,"Unable to get oldest uploadable video details");
    }

    nd_strncpy(storage_info.ovd_udid, udid.c_str(), sizeof(storage_info.ovd_udid));

    LOG_D(TAG,"udid1 is %s",storage_info.ovd_udid);
    //print_storage_info(storage_info);
    return (send_msg ((generic_msg_t*)&storage_info, RES_STORAGE_INFO, sizeof (storage_info),
                CIRC_BUFF_ctx->circular_buffer_q_name, diagnostic_q_name, 0));    
}

void circular_buffer_msg_loop()
{
    LOG_I(TAG, "entered into circular_buffer_msg_loop");
    nd_msgq_t::nd_msg_t *msg;
    circular_buffer_generic_msg_t* clean_db;
    circular_buffer_add_file_db_msg_t *add_file_msg;
    circular_buffer_copy_add_file_db_msg_t *copy_add_file_msg;
    circular_buffer_update_file_db_msg_t *update_file_msg;
    circular_buffer_sdcard_mount_msg_t *sdcard_mount_msg;
    circular_buffer_file_compression_msg_t *file_compression_msg;
    // thread for running fsck command on configured low power wakeup
    pthread_t fsck_run_thread;

    int64_t sdcard_remount_req_time = 0x00;
    int64_t const sdcard_remount_req_ignore_time = 60;
    bool ret;
    

    vector<file_data_str_t> all_files_dir;
    vector<file_data_str_db_t> all_files_db;

    while(1) {
    
#if !NEW_Q    
        if( (msg = CIRC_BUFF_ctx->db_main_msg_q->receive( )) == NULL ) {
            LOG_C(TAG, "Receive message failed");
            break;
        }
        circular_buffer_generic_msg_t *g_msg = (circular_buffer_generic_msg_t *)msg->get_buffer();
#else

        circular_buffer_generic_msg_t *g_msg = (circular_buffer_generic_msg_t *)block_wait_pop();
#if 0
        // debug logs will remove soon
        for (int cnt = 0; cnt<g_msg->len;cnt++) {
            printf("%c",((char*)g_msg)[cnt]);
        }
#endif
#endif //NEW_Q
        if(g_msg == NULL) {
            LOG_E(TAG, "Empty message received in CB ");
            continue;
        }
        LOG_D(TAG, "g_msg->len %d g_msg->type %d", g_msg->len, g_msg->type);

        switch( g_msg->type ) {

            case REQ_CIRCULAR_BUFFER_CLEAN_DB:
                clean_db = (circular_buffer_generic_msg_t *)g_msg;
                LOG_I(TAG, "CLEAN_DB received");

                if(get_details(all_files_dir, all_files_db) == false) {
                    LOG_E(TAG, "get_details(all_files_dir, all_files_db) == false");
                    return;
                }
                else {
                    if (!circular_buffer_cleanup(all_files_dir, all_files_db)){
                        LOG_E(TAG, "Error in circular_buffer_cleanup()");
                    }
                }
                adjust_filling_limit( all_files_db);
                all_files_dir.clear();
                all_files_db.clear();

                START_TRANSCODING = true;
                // Sending critical info with oldest and latest session details
                oldestNewest_Session();
                break;

           case REQ_CIRCULAR_BUFFER_ADD_FILE_DB_FOR_SAVE_EXT_CAMERA_FILES_IN_DHUB:
                {
                    add_file_msg = (circular_buffer_add_file_db_msg_t *)g_msg;
                    LOG_I(TAG, "ADD_FILE_DB: %s", add_file_msg->file_info.base_file_name);
                    if(strstr(add_file_msg->file_info.base_file_name, "_y_summary") != NULL){
                        add_file_msg->file_info.sessionCount = -1 ;
                        add_file_msg->file_info.udid = -1;
                        LOG_I(TAG, "summary json file %s ", add_file_msg->file_info.base_file_name );
                    }
                    // Commenting lenght check as python sysv message doesn't have the exact length

                    int64_t copy_starttime = -1, copy_endtime = -1;
                    string hs_session(add_file_msg->file_info.base_file_name);
                    string sdcard_path_file = nd_device_obj->get_external_eMMC_mount_path() + "/" + add_file_msg->file_info.base_file_name;
                    if(drp_enabled){
                        string fileName = add_file_msg->file_info.base_file_name;
                        if(add_file_msg->file_info.time == 0) {
                            add_file_msg->file_info.time = extractTimestamp(fileName);
                            LOG_D("DRP", "add_file_msg->file_info.time: %lld CIRC_BUFF_ctx->current_time: %lld", add_file_msg->file_info.time,  CIRC_BUFF_ctx->current_time );
                        }
                        if( abs(add_file_msg->file_info.time - CIRC_BUFF_ctx->current_time) > ( 5 * minutes_in_millisec ) ) {  // correct timestamp of mp4 files before writing to DB.
                            add_file_msg->file_info.time = get_timeStamp_from_DB(sessionCount_from_file(add_file_msg->file_info.base_file_name));
                            LOG_I("DRP", "add_file_msg->file_info.time: %lld CIRC_BUFF_ctx->current_time: %lld", add_file_msg->file_info.time,  CIRC_BUFF_ctx->current_time );
                        }
                    }
                    ret = add_file_DB(add_file_msg->file_info);
                    fill_map_add_file_healthstats(hs_session, "sdcard", ret);
                    if(ret == false){
                        LOG_E(TAG, "Error in add_file_DB");
                        //CIRC_BUFF_ctx->close_db(CIRC_BUFF_ctx->db_handle);
                        break;
                    }
                    LOG_I(TAG, "Success in add_file_DB");
                }
                break;

            case REQ_CIRCULAR_BUFFER_ADD_FILE_DB:
            {
                add_file_msg = (circular_buffer_add_file_db_msg_t *)g_msg;
                string file_name(add_file_msg->file_info.base_file_name);
                validate_filename(file_name);
                nd_strncpy(add_file_msg->file_info.base_file_name, file_name.c_str(), ((file_name.length() + 1) < FNAME_LEN) ? (file_name.length() + 1) : FNAME_LEN);
#ifndef NO_SDCARD // only for bagheera
                if (file_is_present(nd_device_obj->get_external_eMMC_mount_path() + add_file_msg->file_info.base_file_name )){
                    LOG_I(TAG, "File already available in SdCard, ignoring this request %s", add_file_msg->file_info.base_file_name);
                    break; 
                }
#endif
                LOG_I(TAG, "ADD_FILE_DB: %s file.time: %lld ", add_file_msg->file_info.base_file_name, add_file_msg->file_info.time );
                if(strstr(add_file_msg->file_info.base_file_name, ".zip") != NULL){
                    update_udid_sessionCount_from_filename(add_file_msg->file_info);

                }
                // Commenting lenght check as python sysv message doesn't have the exact length
                {
                    int64_t copy_starttime = -1, copy_endtime = -1;
                    string hs_session(add_file_msg->file_info.base_file_name);
                    string sdcard_path_file = nd_device_obj->get_external_eMMC_mount_path() + "/" + add_file_msg->file_info.base_file_name;
                    int64_t updated_timestamp = 0;

                    int sdc_file_size;
                    sdc_file_size = file_size(sdcard_path_file);
                    add_file_msg->file_info.file_size = sdc_file_size;
                    if(drp_enabled){
                        if(add_file_msg->file_info.sessionCount > 0) {  
                            current_sessionCount = add_file_msg->file_info.sessionCount ;
                            current_udid = add_file_msg->file_info.udid;
                            string fileName = add_file_msg->file_info.base_file_name;
                            if(add_file_msg->file_info.time > 0){
                                updated_timestamp = convert_epoch_format(add_file_msg->file_info.time, DigitsOfEpoch::eDigits_MilliSeconds);
                            } 
                            add_file_msg->file_info.time = updated_timestamp ;
                            LOG_D(TAG, "current_sessionCount: %lld, current_time: %lld", current_sessionCount, CIRC_BUFF_ctx->current_time);
                        }else{
                            LOG_C(TAG,"Negative or Zero session count received from add file db message");
                        }
                        string fileName = add_file_msg->file_info.base_file_name;
                        if(fileName.find(".mp4") == string::npos || is_ext_cam_file(fileName) ){  // correct timestamp of non mp4 files before writing to DB.
                            LOG_I("DRP", "ADD_FILE_DB: %s", add_file_msg->file_info.base_file_name);
                            if(add_file_msg->file_info.time == 0) {
                                add_file_msg->file_info.time = extractTimestamp(fileName); 
                                LOG_D("DRP", "add_file_msg->file_info.time: %lld CIRC_BUFF_ctx->current_time: %lld", add_file_msg->file_info.time,  CIRC_BUFF_ctx->current_time );
                            }
                            if( abs(add_file_msg->file_info.time - CIRC_BUFF_ctx->current_time) > ( 5 * minutes_in_millisec ) ) { // correct timestamp of mp4 files before writing to DB.
                                add_file_msg->file_info.time = get_timeStamp_from_DB(sessionCount_from_file(add_file_msg->file_info.base_file_name));      
                                LOG_D("DRP", "add_file_msg->file_info.time: %lld CIRC_BUFF_ctx->current_time: %lld", add_file_msg->file_info.time,  CIRC_BUFF_ctx->current_time );
                            }
                            if( ( CIRC_BUFF_ctx->current_time > 0 ) && ( fileName.find(".zip") != string::npos )) { // Increment time once in a minute and when it is valid
                                CIRC_BUFF_ctx->current_time =  get_system_time() ;
                            }
                            LOG_I("DRP", "ADD_FILE_DB: %s file.time: %lld", add_file_msg->file_info.base_file_name, add_file_msg->file_info.time);
                        }
                        else  {
                            pthread_mutex_lock(&CIRC_BUFF_ctx->current_time_mtx);
                            CIRC_BUFF_ctx->current_time = updated_timestamp;
                            pthread_mutex_unlock(&CIRC_BUFF_ctx->current_time_mtx);
                            LOG_I("DRP", "add_file_msg->file_info.time: %lld CIRC_BUFF_ctx->current_time: %lld", add_file_msg->file_info.time,  CIRC_BUFF_ctx->current_time );
                        }
                    }
                    ret = add_file_DB(add_file_msg->file_info);
                    fill_map_add_file_healthstats(hs_session, "sdcard", ret);
                    if(ret == false){
                        LOG_E(TAG, "Error in add_file_DB");
                        //CIRC_BUFF_ctx->close_db(CIRC_BUFF_ctx->db_handle);
                        break;
                    }
                    if(nullptr != add_file_msg ){
                        signal_process_file_size_check(add_file_msg->file_info.base_file_name, add_file_msg->file_info.file_size);
                    }
                }
                LOG_I(TAG, "Success in add_file_DB");
                break;
            }
            case REQ_CIRCULAR_BUFFER_ADD_FILE_DB_PYTHON:
                    LOG_I(TAG, "entered REQ_CIRCULAR_BUFFER_ADD_FILE_DB_PYTHON");
                    circular_buffer_add_file_db_msg_python_t *add_file_msg;
                    circular_buffer_add_file_db_msg_t new_msg;
                    add_file_msg = (circular_buffer_add_file_db_msg_python_t *)g_msg;
                    clone_convert_python_message(add_file_msg, &new_msg, REQ_CIRCULAR_BUFFER_ADD_FILE_DB);
                    /*LOG_I(TAG, "CIRCULAR_BUFFER_ADD_FILE_DB_PYTHON received");
                    LOG_I(TAG, "calling add_file_with_priority for %s, priority NORMAL",
                         add_file_msg->file_info.base_file_name );
                    LOG_I(TAG, "msg_type = %d", add_file_msg->msg_type);
                    LOG_I(TAG, "length = %d", add_file_msg->length);
                    LOG_I(TAG, "cl id = %s", add_file_msg->client_id);
                    LOG_I(TAG, "msg idx = %d", add_file_msg->msg_idx);
                    LOG_I(TAG, "res = %d", add_file_msg->res_reqd);
                    LOG_I(TAG, "time = %s", add_file_msg->file_info.time);
                    LOG_I(TAG, "duration = %d", add_file_msg->file_info.duration);
                    LOG_I(TAG, "file_size = %d", add_file_msg->file_info.file_size);
                    LOG_I(TAG, "file_type = %d", add_file_msg->file_info.file_type);
                    LOG_I(TAG, "camtype = %d", add_file_msg->file_info.camtype);
                    LOG_I(TAG, "**************************************");
                    LOG_I(TAG, "msg_type = %d", new_msg.msg_type);
                    LOG_I(TAG, "length = %d", new_msg.length);
                    LOG_I(TAG, "cl id = %s", new_msg.client_id);
                    LOG_I(TAG, "msg idx = %d", new_msg.msg_idx);
                    LOG_I(TAG, "res = %d", new_msg.res_reqd);
                    LOG_I(TAG, "time = %lld", new_msg.file_info.time);
                    LOG_I(TAG, "duration = %d", new_msg.file_info.duration);
                    LOG_I(TAG, "file_size = %d", new_msg.file_info.file_size);
                    LOG_I(TAG, "file_type = %d", new_msg.file_info.file_type);
                    LOG_I(TAG, "camtype = %d", new_msg.file_info.camtype);*/
                    send_msg((generic_msg_t*)&new_msg, REQ_CIRCULAR_BUFFER_ADD_FILE_DB, sizeof(new_msg), CIRC_BUFF_ctx->circular_buffer_q_name, CIRC_BUFF_ctx->circular_buffer_q_name, 0);
                break;

            case REQ_CIRCULAR_BUFFER_UPDATE_FILE_DB:
            {
                update_file_msg = (circular_buffer_update_file_db_msg_t *)g_msg;
                string file_name(update_file_msg->file_info.base_file_name);
                validate_filename(file_name);
                nd_strncpy(update_file_msg->file_info.base_file_name, file_name.c_str(), ((file_name.length() + 1) < FNAME_LEN) ? (file_name.length() + 1) : FNAME_LEN);
                LOG_I(TAG, "CIRCULAR_BUFFER_UPDATE_FILE_DB received. file_info.time: %lld, file :%s:", update_file_msg->file_info.time, update_file_msg->file_info.base_file_name );
                {
                    string sdcard_path_file = nd_device_obj->get_external_eMMC_mount_path() + "/" + update_file_msg->file_info.base_file_name;

                    int sdc_file_size;
                    sdc_file_size = file_size(sdcard_path_file);

                    update_file_msg->file_info.file_size = sdc_file_size;
                }

                if(drp_enabled){
                    string fileName = update_file_msg->file_info.base_file_name;
                    if(fileName.find(".mp4") == string::npos || is_ext_cam_file(fileName) ){  // correct timestamp of non mp4 files before writing to DB.
                        if(update_file_msg->file_info.time == 0) {
                            update_file_msg->file_info.time = extractTimestamp(fileName);
                            LOG_D("DRP", "update_file_msg->file_info.time: %lld CIRC_BUFF_ctx->current_time: %lld", update_file_msg->file_info.time,  CIRC_BUFF_ctx->current_time );
                        }
                        if( abs(update_file_msg->file_info.time - CIRC_BUFF_ctx->current_time) > 5 * minutes_in_millisec ) { // correct timestamp of mp4 files before writing to DB.
                            update_file_msg->file_info.time = get_timeStamp_from_DB(sessionCount_from_file(update_file_msg->file_info.base_file_name));
                            LOG_D("DRP", "update_file_msg->file_info.time: %lld CIRC_BUFF_ctx->current_time: %lld", update_file_msg->file_info.time,  CIRC_BUFF_ctx->current_time );
                        }
                    }
                }
                ret = update_file_DB(update_file_msg->file_info);
                if(ret == false){
                    LOG_E(TAG, "Error in update_file_DB");
                    //CIRC_BUFF_ctx->close_db(CIRC_BUFF_ctx->db_handle);
                    break;
                }
                if(nullptr != update_file_msg ){
                    signal_process_file_size_check(update_file_msg->file_info.base_file_name, update_file_msg->file_info.file_size);
                }
                break;
            }
            case REQ_CIRCULAR_BUFFER_UPDATE_FILE_DB_PYTHON:
                {
                    LOG_I(TAG, "REQ_CIRCULAR_BUFFER_UPDATE_FILE_DB_PYTHON");
                    circular_buffer_update_file_db_msg_python_t *update_file_msg;
                    circular_buffer_update_file_db_msg_t new_msg;
                    update_file_msg = (circular_buffer_update_file_db_msg_python_t *)g_msg;
                    clone_convert_python_message(update_file_msg, &new_msg, REQ_CIRCULAR_BUFFER_UPDATE_FILE_DB);
                    send_msg((generic_msg_t*)&new_msg, REQ_CIRCULAR_BUFFER_UPDATE_FILE_DB, sizeof(new_msg), CIRC_BUFF_ctx->circular_buffer_q_name, CIRC_BUFF_ctx->circular_buffer_q_name, 0);
                }
                break;

            case REQ_CIRCBUFF_SDCARD_MOUNT:
                sdcard_mount_msg = (circular_buffer_sdcard_mount_msg_t *)g_msg;
                LOG_D(TAG, "CIRCBUFF_SDCARD_MOUNT received");
                LOG_D(TAG, "sdcard_mount_msg->client_id %s force flag %d", sdcard_mount_msg->client_id, sdcard_mount_msg->force_mount);

                if(sdcard_mount_msg->length != sizeof(circular_buffer_sdcard_mount_msg_t)){
                    LOG_E(TAG, "sdcard_mount_msg->length != sizeof(circular_buffer_sdcard_mount_msg_t)");
                    break;
                }
                if( sdcard_mount_msg->time < (sdcard_remount_req_time + sdcard_remount_req_ignore_time) ) {
                    LOG_I(TAG, "duplicate REQ_CIRCBUFF_SDCARD_MOUNT received; ignoring this request");
                    LOG_I(TAG, " sdcard_mount_msg->time %lld sdcard_remount_req_time %lld", 
                                sdcard_mount_msg->time, sdcard_remount_req_time );
                    break;
                }
                sdcard_remount_req_time = sdcard_mount_msg->time;
                /*if(MOUNT_SDCARD == true) {
                    if(sdcard_recovery_check()) {
                        LOG_D(TAG, "success in sdcard_recovery_check");
                    }
                }*/
                break;
            
            // update file compression info
            case REQ_CIRCULAR_BUFFER_FILE_COMPRESSION:
                file_compression_msg = (circular_buffer_file_compression_msg_t *) g_msg;

                if (file_compression_msg->compression_type >= CIRCULAR_BUFFER_COMPRESSION_ERROR){
                    LOG_E(TAG, "Invalid file compression value (%d) provided for %s. Not updating DB", 
                                    file_compression_msg->compression_type, file_compression_msg->base_file_name);
                    break;
                }
                if (file_compression_msg->compression_type == CIRCULAR_BUFFER_MEDIUM_COMPRESSION || file_compression_msg->compression_type == CIRCULAR_BUFFER_HIGH_COMPRESSION){
                    // not required to update file compression except adjecent file of dms camera
                    if( store_dms_file == false ) {
                        file_compression_msg->base_file_name[0] = '8'; //rename file name to side cam 8 video file
                        if(file_is_present(nd_device_obj->get_external_eMMC_mount_path() + file_compression_msg->base_file_name) == true) {
                            circular_buffer_fileinfo_t file_info;
                            strcpy(file_info.base_file_name, file_compression_msg->base_file_name);
                            update_udid_sessionCount_from_filename(file_info);
                            LOG_D(TAG, "file: %s,  sessionCount: %lld" , file_info.base_file_name , file_info.sessionCount );
                            if( sessionCount_alert_file + 1 == file_info.sessionCount ) {
                                file_compression_msg->compression_type = CIRCULAR_BUFFER_NO_COMPRESSION_ADJ ;
                                sessionCount_alert_file = 0;
                                LOG_I(TAG, "file: %s is next ADJECENT to alert file compresion type: %d" , file_compression_msg->base_file_name , file_compression_msg->compression_type );
                                if (!update_file_compression_DB(file_compression_msg->base_file_name, file_compression_msg->compression_type)){
                                    LOG_E(TAG, "Failed to update %s file compression to %d in DB",
                                            file_compression_msg->base_file_name, (int)file_compression_msg->compression_type);
                                }
                            }
                        }
                    }
                    break;
                }
                check_and_delete_alert_hq_file(CIRC_BUFF_ctx->db_handle);
                check_and_delete_alert_dms_file(CIRC_BUFF_ctx->db_handle);

                /* Update file compression for both inward and outward */
                {
                    string file_compression_str = file_compression_to_string(file_compression_msg->compression_type);
                    LOG_I(TAG, "Received FILE_COMPRESSION_MSG: %s compression_type: %s %d. Updating inward and outward",
                            file_compression_msg->base_file_name, file_compression_str.c_str(), file_compression_msg->compression_type );
                }
                if (!update_file_compression_DB(file_compression_msg->base_file_name, file_compression_msg->compression_type)){
                    LOG_E(TAG, "Failed to update %s file compression to %d in DB",
                                file_compression_msg->base_file_name, (int)file_compression_msg->compression_type);
                }
                // Update the file compression type for outward LD as well, this will be used to retain alert in LD as well, doing this onyl for outward and inward video
                if (!update_file_compression_DB((std::string(file_compression_msg->base_file_name) + ld_extn).c_str(), CIRCULAR_BUFFER_NO_COMPRESSION_ADJ)){
                    LOG_E(TAG, "Failed to update %s file compression to %d in DB",
                                file_compression_msg->base_file_name, (int)CIRCULAR_BUFFER_NO_COMPRESSION_ADJ);
                }
                //Update the file_compression type for inward videos as well if it exists
                file_compression_msg->base_file_name[0] = '1'; //rename file name to inward video file
                if (!update_file_compression_DB(file_compression_msg->base_file_name, file_compression_msg->compression_type)){
                    LOG_E(TAG, "Failed to update %s file compression to %d in DB",
                                file_compression_msg->base_file_name, (int)file_compression_msg->compression_type);
                }
                // Update the file compression type for outward LD as well, this will be used to retain alert in LD as well, doing this onyl for outward and inward video
                if (!update_file_compression_DB((std::string(file_compression_msg->base_file_name) + ld_extn).c_str(), CIRCULAR_BUFFER_NO_COMPRESSION_ADJ)){
                    LOG_E(TAG, "Failed to update %s file compression to %d in DB",
                                file_compression_msg->base_file_name, (int)CIRCULAR_BUFFER_NO_COMPRESSION_ADJ);
                }
                // Update the file_compression type for side cameras videos as well if supported
                if(true==nd_device_obj->is_side_cameras_supported()) {
                    file_compression_msg->base_file_name[0] = '2'; //rename file name to left side camera video file
                    if (!update_file_compression_DB(file_compression_msg->base_file_name, file_compression_msg->compression_type)){
                        LOG_E(TAG, "Failed to update %s file compression to %d in DB",
                                    file_compression_msg->base_file_name, (int)file_compression_msg->compression_type);
                    }
                    file_compression_msg->base_file_name[0] = '3'; //rename file name to right side camera video file
                    if (!update_file_compression_DB(file_compression_msg->base_file_name, file_compression_msg->compression_type)){
                        LOG_E(TAG, "Failed to update %s file compression to %d in DB",
                                    file_compression_msg->base_file_name, (int)file_compression_msg->compression_type);
                    }
                }
                // Alert update for DMS file. current file + adjecent file.
                file_compression_msg->base_file_name[0] = '8'; //rename file name to side cam 8 video file
		// file presence check is required for dms ld file. HQ file might get deleted if num_dms_videos_max = 0
                if(file_is_present(nd_device_obj->get_external_eMMC_mount_path() + file_compression_msg->base_file_name + ld_extn) == true || 
			file_is_present(nd_device_obj->get_external_eMMC_mount_path() + file_compression_msg->base_file_name ) == true	) 
                {
                    if(store_dms_file == false) {
                        if(file_compression_msg->compression_type == CIRCULAR_BUFFER_NO_COMPRESSION) {
                            circular_buffer_fileinfo_t file_info;
                            strcpy(file_info.base_file_name, file_compression_msg->base_file_name);
                            update_udid_sessionCount_from_filename(file_info);
                            LOG_D(TAG, "file: %s,  sessionCount: %lld" , file_info.base_file_name , file_info.sessionCount );
                            // call update_file_compression_DB() for previous dms file.
                            int prev_DMS_file_index =  get_previous_DMS_file_index(file_info.sessionCount ) ;
                            if (!update_file_compression_DB(prev_DMS_file_index, CIRCULAR_BUFFER_NO_COMPRESSION_ADJ)){
                                LOG_E(TAG, "Failed to update previous file of %s, compression to %d in DB",
                                            file_compression_msg->base_file_name, (int) CIRCULAR_BUFFER_NO_COMPRESSION_ADJ );
                            }
                            sessionCount_alert_file = file_info.sessionCount;
                        }
                    }
		    else {   // Need to save LQ file for alert. So update file compression for LQ file.
			    string dms_lq_alert_file = file_compression_msg->base_file_name + ld_extn ; 
			    if (!update_file_compression_DB(dms_lq_alert_file.c_str() , file_compression_msg->compression_type)){
				LOG_E(TAG, "Failed to update %s file compression to %d in DB",
					dms_lq_alert_file.c_str(), (int)file_compression_msg->compression_type);
			    }

		    }
                    if (!update_file_compression_DB(file_compression_msg->base_file_name, file_compression_msg->compression_type)){
                        LOG_E(TAG, "Failed to update %s file compression to %d in DB",
                                file_compression_msg->base_file_name, (int)file_compression_msg->compression_type);
                    }
                    /*
                    else { // delete LQ file
                        file_delete(SDCARD_MOUNT_PATH + file_compression_msg->base_file_name + ld_extn);
                        LOG_I(TAG, "lq file  %s deleted for file compression to %d in DB",
                                    (SDCARD_MOUNT_PATH + file_compression_msg->base_file_name + ld_extn).c_str() , (int)file_compression_msg->compression_type);
                    }
                    */
                }
                break;

#ifndef NO_SDCARD // only for bagheera
            case REQ_CIRCULAR_BUFFER_REMOVE_DELETE_TIME_CSV:
                LOG_I(TAG, "Received REQ_CIRCULAR_BUFFER_REMOVE_DELETE_TIME_CSV");
                clear_cb_delete_time_csv();
                break;
            case REQ_CIRCULAR_BUFFER_REMOVE_COPY_TIME_CSV:
                LOG_I(TAG, "Received REQ_CIRCULAR_BUFFER_REMOVE_COPY_TIME_CSV");
                clear_cb_copy_time_csv();
                break;
#endif
            case REQ_CIRCULAR_BUFFER_SDCARD_FSCK_CHECK:
#ifdef SDCARD_FSCK
                LOG_I(TAG, "creating thread to run fsck command to check for filesystem errors");
                handle_fsck_check_msg() ;
#endif
                break;
            case REQ_CIRCULAR_BUFFER_NOTIFY_VIDEO_LIST_TO_CLOUD:
                LOG_I(TAG, "Received REQ_CIRCULAR_BUFFER_NOTIFY_VIDEO_LIST_TO_CLOUD");
                {
                    bool res = send_video_list();
                    LOG_I(TAG, "send_video_list result: %d", res);
                }
                break;
            case REQ_STORAGE_INFO:
	 	        LOG_I(TAG, "Received REQ_STORAGE_INFO");

                mutex_health_msg.lock();
                req_health_msg = (req_storage_health_msg_t*)g_msg;
                mutex_health_msg.unlock();
                send_storage_info_ready = true;
                send_storage_info_cond.notify_one();
                //send_storage_info(req_health_msg->time);
                break;

	    case ERROR:
            default:
                LOG_E(TAG, "Unknown message %d received", g_msg->type);
                break;
        }
#if !NEW_Q
        delete msg;
#else
        free(g_msg);
#endif
    }
    return ;
}

/* 
 * collect_notify_videolist function has been modified to reuse for sending number of files
 * added and deleted to health data.
 * [1] If collect_notify_videolist called by health - files_added and files_deleted will have
 *     the address memory address, in this case, CB will not update the DB files entry with CIRCULAR_BUFFER_STATUS_NOTIFIED.
 * [2] If collect_notify_videolist called by send_video_list, files_added and
 *     files_deleted will be NULL.
 *
 */

bool collect_notify_videolist(pair < vector< json_add_del >, vector< json_add_del > > &add_del_files, int& files_added, int& files_deleted)
{
    string add_del_json = "", db_identifier = "";
    int64_t db_creation_time = -1;
    bool ret;
    int oldest_video_index = -1;
    static int added = 0, deleted = 0;
    int f_added = -1, f_deleted = -1;
    bool cloud_notify = false;
    string emmc_path = nd_device_obj->get_external_eMMC_mount_path();

    if((files_added == CLOUD_NOTIFY) && (files_deleted == CLOUD_NOTIFY)) {
        cloud_notify = true;   
    }
    ret = find_add_del_files(add_del_files, oldest_video_index, db_identifier, db_creation_time);
    if(ret == false){
        LOG_E(TAG, "Some thing wrong with find_add_del_files");
        add_del_files.first.clear();
        add_del_files.second.clear();
        return false;
    }
    pair < vector< json_add_del >, vector< json_add_del > > add_del_files_updated;

    // scan through the files and prepare new vector with only LD files and extcam files
    for(int i = 0; i < add_del_files.first.size(); i++) {
	    // Add dms HQ file 
	    if(add_del_files.first[i].file_name.find("8_trip") != add_del_files.first[i].file_name.npos) { 
		if(add_del_files.first[i].file_name.find(ld_extn) == add_del_files.first[i].file_name.npos) {
		    add_del_files_updated.first.push_back(add_del_files.first[i]);
		}

	    }
	    else {
	    // Add ext cam and side camera files as it is
		    if(( add_del_files.first[i].file_name.find("0_trip") == add_del_files.first[i].file_name.npos ) && 
				    ( add_del_files.first[i].file_name.find("1_trip") == add_del_files.first[i].file_name.npos )) {
			    add_del_files_updated.first.push_back(add_del_files.first[i]);
		    }// HQ files of 0, 1 cams
		    else if( (( add_del_files.first[i].file_name.find("0_trip") != add_del_files.first[i].file_name.npos ) ||
				    ( add_del_files.first[i].file_name.find("1_trip") != add_del_files.first[i].file_name.npos))
                    && add_del_files.first[i].file_name.find(ld_extn) == add_del_files.first[i].file_name.npos ) {
			    add_del_files_updated.first.push_back(add_del_files.first[i]);
            }//LD files of 0,1 cams if not present in HQ
            else if( (( add_del_files.first[i].file_name.find("0_trip") != add_del_files.first[i].file_name.npos ) ||
				    ( add_del_files.first[i].file_name.find("1_trip") != add_del_files.first[i].file_name.npos)) &&
                    add_del_files.first[i].file_name.find(ld_extn) != add_del_files.first[i].file_name.npos ) {
                string hq_file_name = add_del_files.first[i].file_name.substr(0, add_del_files.first[i].file_name.length() - ld_extn.length());
                LOG_D(TAG, "Checking presence of HQ file %s for LD file %s, path %s", hq_file_name.c_str(), add_del_files.first[i].file_name.c_str(), emmc_path.c_str());
                int status = CIRCULAR_BUFFER_STATUS_ERROR;
                if((file_is_present(emmc_path + hq_file_name) == false) || (get_file_xattr(emmc_path + hq_file_name, FileMetadataKey::status, &status, sizeof(status)) != XATTR_OK)) {
                    // add LD file only if HQ file is not present, or if file is present and xattr read fails means file is not added to DB
                    add_del_files_updated.first.push_back(add_del_files.first[i]);
                }
            }
	    }
    }

    // scan through the new vector and change the name of LD to HD

    if(cloud_notify == true) {
        LOG_I(TAG, "Below is the added file list");
    }
    for(int i = 0; i < add_del_files_updated.first.size(); i++) {
        if( add_del_files_updated.first[i].file_name.find(ld_extn) != add_del_files_updated.first[i].file_name.npos ) {
            string file_name_updated = add_del_files_updated.first[i].file_name.substr(0, add_del_files_updated.first[i].file_name.length() - ld_extn.length());
            add_del_files_updated.first[i].file_name = file_name_updated;
        }
        if(cloud_notify == true) {
            LOG_I(TAG, "file = %s, fc_type = %d", add_del_files_updated.first[i].file_name.c_str(), add_del_files_updated.first[i].alert_type);
        }
    }


    // scan through the files and prepare new vector with only LD files and extcam files
    for(int i = 0; i < add_del_files.second.size(); i++) {
	    // Add ext cam and side camera files as it is
	// Add ext cam and side camera files as it is
       if(( add_del_files.second[i].file_name.find("0_trip") == add_del_files.second[i].file_name.npos ) &&
          ( add_del_files.second[i].file_name.find("1_trip") == add_del_files.second[i].file_name.npos )) {
           add_del_files_updated.second.push_back(add_del_files.second[i]);
       }
       else if(((add_del_files.second[i].file_name.find("0_trip") != add_del_files.second[i].file_name.npos)
                || (add_del_files.second[i].file_name.find("1_trip") != add_del_files.second[i].file_name.npos))
                    && (add_del_files.second[i].file_name.find(ld_extn) == add_del_files.second[i].file_name.npos)
                    && ((add_del_files.second[i].alert_type == CIRCULAR_BUFFER_NO_COMPRESSION)
                        || (add_del_files.second[i].alert_type == CIRCULAR_BUFFER_NO_COMPRESSION_NO_LQ)
                        || (add_del_files.second[i].alert_type == CIRCULAR_BUFFER_NO_COMPRESSION_ADJ))) {
            // In this scenario HQ file is already deleted, so only add it to deleted list if LD also not present
            if(file_is_present(emmc_path + add_del_files.second[i].file_name + ld_extn) == false) {
                add_del_files_updated.second.push_back(add_del_files.second[i]);
            }
       }
       else  if( add_del_files.second[i].file_name.find(ld_extn) != add_del_files.second[i].file_name.npos ) {
            // In this scenario LD file is already deleted, so only add it to deleted list if HQ also not present
            string hq_file_name = add_del_files.second[i].file_name.substr(0, add_del_files.second[i].file_name.length() - ld_extn.length());
            if(file_is_present(emmc_path + hq_file_name) == false){
                add_del_files_updated.second.push_back(add_del_files.second[i]);
            }
       }

    }

    if(cloud_notify == true) {
        LOG_I(TAG, "Below is the deleted file list");
    }
    // scan through the new vector and change the name of LD to HD
    for(int i = 0; i < add_del_files_updated.second.size(); i++) {
        if( add_del_files_updated.second[i].file_name.find(ld_extn) != add_del_files_updated.second[i].file_name.npos ) {
            string file_name_updated = add_del_files_updated.second[i].file_name.substr(0, add_del_files_updated.second[i].file_name.length() - ld_extn.length());
            add_del_files_updated.second[i].file_name = file_name_updated;
        }
        if(cloud_notify == true) {
            LOG_I(TAG, "file = %s, fc_type = %d", add_del_files_updated.second[i].file_name.c_str(), add_del_files_updated.second[i].alert_type);
        }
    }

/* 
 *  if cloud_notify is true, CB will send the list of added and deleted files to
 *  cloud
 *
 */

    if(cloud_notify == true) {
        LOG_I(TAG,"Video list to notify cloud: Number of files added = %d, Number of files deleted = %d",
                add_del_files_updated.first.size(), add_del_files_updated.second.size());
        ret = form_Json(add_del_json, add_del_files_updated, oldest_video_index, db_identifier, db_creation_time);
        if(ret == false){
            LOG_E(TAG, "Some thing wrong with form_Json");
            add_del_files.first.clear();
            add_del_files.second.clear();
            return false;
        }
        ret = notify_cloud(add_del_json);
        if(ret == false){
            LOG_E(TAG, "Some thing wrong with notify_cloud");
            add_del_files.first.clear();
            add_del_files.second.clear();
            return false;
        }
        video_list_sent = true;
    } else {

/*
 *  if cloud_notify is false, CB will count the number of files added and
 *  deleted, here video_list_sent flag has been used which stats if the video
 *  list has been sent to cloud or not. After video list sent to cloud, all the
 *  listed files status will be updated to CIRCULAR_BUFFER_STATUS_NOTIFIED.
 *  This function will persist the previous count of added and deleted files, in
 *  case the video list has not been sent to cloud, db will not get updated with
 *  proper status.
 *  It will help to send the accurate value to the health data.
 *
 */
        f_added = add_del_files_updated.first.size();
        f_deleted = add_del_files_updated.second.size();

        if(!video_list_sent) {
            if(added == 0 && deleted == 0) {
                added = f_added;
                deleted = f_deleted;
                files_added = f_added;
                files_deleted = f_deleted;
            } else {
                if(f_added < added) {
                    files_added = f_added;
                    added = f_added;
                } else {
                    files_added = f_added - added;
                    added = f_added;
                }
                if(f_deleted < deleted) {
                    files_deleted = f_deleted;
                    deleted = f_deleted;
                } else {
                    files_deleted = f_deleted - deleted;
                    deleted = f_deleted;
                }
            }
        } else {
            added = 0;
            deleted = 0;
            files_added = f_added;
            files_deleted = f_deleted;
            video_list_sent = false;
        }

        LOG_I(TAG, "Files added : %d", files_added);
        LOG_I(TAG, "Files deleted : %d", files_deleted);
    }
    return true;
}

void* cloud_notifier_main(void* args)
{
    sleep(15);
    int count = 0;
    bool last_attempt_failed = false;
    int cloud_notify_retry_count = 0;
    while(1){

        count++;
        sleep(60); // sleep for 1 min

        LOG_I( TAG, "count%NOTIFY_DURATION = %d", count%NOTIFY_DURATION );
        // LOGIC FOR VIDEO_LIST
        if( (count%NOTIFY_DURATION == 1) || ((true == last_attempt_failed) && (cloud_notify_retry_count < MAX_CLOUD_NOTIFY_RETRY)) )
        {
            bool res = send_video_list();
            if(res == false){
                LOG_E(TAG, "send_video_list failed");
                last_attempt_failed = true;
                cloud_notify_retry_count++;
            } else {
                last_attempt_failed = false;
                cloud_notify_retry_count = 0;
            }
        }

        send_sdcard_mount_circbuffer(false);
#ifdef ROUTE_LOGS
        if(count%(LOG_FILE_DURATION) == 0)
        {
            route_logs( log_dir.c_str() );
        }
#endif
    }
    return 0;
}

void* uploader_query_main(void* args)
{
    while(1){
        if( is_msg_q_created(CIRC_BUFF_ctx->q_name_to_upl) == false ) {
            sleep(1);
            LOG_D(TAG, "waiting in q_name_to_upl @@ is_msg_q_created");
            continue;
        }
        LOG_I(TAG, "Success in q_name_to_upl @@ is_msg_q_created");
        break;
    }

    CIRC_BUFF_ctx->msg_q_cb_to_upl = nd_msgq_t::get_msgq( CIRC_BUFF_ctx->q_name_to_upl,
		     nd_msgq_t::ND_MSGQ_CLIENT);
    while( CIRC_BUFF_ctx->msg_q_cb_to_upl == NULL )
    {
        string str_msg = "CIRC_BUFF_ctx->msg_q_cb_to_upl == NULL; will check after 60 seconds.";
        LOG_C(TAG, str_msg.c_str() );
        sleep(60);
        CIRC_BUFF_ctx->msg_q_cb_to_upl = nd_msgq_t::get_msgq( CIRC_BUFF_ctx->q_name_to_upl,
                nd_msgq_t::ND_MSGQ_CLIENT);
        static bool msg_sent = false;
        if(msg_sent == false && CIRC_BUFF_ctx->msg_q_cb_to_upl == NULL) {
            nd_service_obj->send_err_msg(SM_E_CB_MSG_QUEUE_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
            msg_sent = true;
        }
    }

    while(1){
        nd_msgq_t::nd_msg_t *msg;
        if( (msg = CIRC_BUFF_ctx->msg_q_upl_to_cb->receive( )) == NULL ) {
            LOG_C(TAG, "Receive message failed");
            break;
        }
        uploader_query_to_cb_t *query_msg = (uploader_query_to_cb_t*)msg->get_buffer();
        cb_response_to_upl_query_t resp_msg;
        LOG_I(TAG,"Received request from uploader for %s and type %d",query_msg->base_file_name,query_msg->file_type);
        resp_msg.query_id = query_msg->query_id;
        resp_msg.reason = CB_UPLOAD;
        string fileName = query_msg->base_file_name;
        // Check for DRP if enabled
        // Priority is to check DRP first, if file follows drp then only go for privacy check
        if(true == drp_enabled){
            LOG_I(TAG, "DRP query for file: %s", query_msg->base_file_name);
            int64_t fileCreationTime = drp_calcualte_timestamp_uploader(fileName.c_str());
            LOG_I( TAG, "file creation time of file %s might be: %lld",fileName.c_str(), fileCreationTime  );

            static int64_t drp_clock_milliseconds = convert_time_unit(nd_time_unit::time_in_hours,
                    nd_time_unit::time_in_milliseconds, drp_clock_hours) ;
            LOG_I(TAG, "drp_clock_hours: %d drp_clock_milliseconds : %lld ", drp_clock_hours, drp_clock_milliseconds );
            if(drp_clock_milliseconds <= 0 ){
                LOG_E(TAG, "drp_clock_hours: %d drp_clock_milliseconds : %lld ", drp_clock_hours, drp_clock_milliseconds );
                nd_service_obj->send_err_msg(SM_E_CB_INVALID_TIME_RESULT, drp_clock_milliseconds, "Invalid time drp_clock_milliseconds");
                drp_clock_milliseconds = drp_clock_hours*hours_to_milliseconds;
            }
            if(fileCreationTime == -1){
                resp_msg.reason = CB_DRP_UNKNOWN_TIMESTAMP;
            }
            else if((get_system_time() - fileCreationTime) >= drp_clock_milliseconds) {
                resp_msg.reason = CB_DRP_DO_NOT_UPLOAD;
            }
        }
        //Check for privacy if the file type is UPLOAD_FILE_TYPE_VIDEO
        if(query_msg->file_type == UPLOAD_FILE_TYPE_VIDEO and resp_msg.reason == CB_UPLOAD){
            resp_msg.reason = check_video_upload_enabled_based_on_privacy(fileName);
            LOG_I(TAG,"privacy reason after checking privacy status for vod request %s %d",query_msg->base_file_name,resp_msg.reason);
        }
        //Take decision to upload according to the reason
        if(resp_msg.reason != CB_UPLOAD){
            resp_msg.upload = false;
        }else{
            resp_msg.upload = true;
        }

        // If below file is present then sleep for that much time before sending response to uploader
        // This is added only for testing purpose, to simulate delay in CB response to uploader
        if(file_is_present(file_path_delay_cb_response)){
            int sleep_duration = 5; // default sleep duration
            if(read_from_dev_shm_nd_xattr_file(file_path_delay_cb_response, sleep_duration)){
                LOG_I(TAG, "Read sleep duration from %s : %d seconds", file_path_delay_cb_response.c_str(), sleep_duration);
            }
            sleep(sleep_duration);
        }

        LOG_I( TAG, "Sending msg to upl for file %s type %d : upload -> %d, reason -> %d",query_msg->base_file_name,query_msg->file_type,resp_msg.upload,resp_msg.reason);
        //set xattr for drp status
        if (CIRC_BUFF_ctx->extended_attr_enabled){
            if((true == drp_enabled) && (file_is_present(nd_device_obj->get_external_eMMC_mount_path() + fileName))){
                string filepath = nd_device_obj->get_external_eMMC_mount_path() + fileName;
                if(set_file_xattr(filepath, FileMetadataKey::drp_status, &resp_msg.upload, sizeof(resp_msg.upload)) == XATTR_OK){
                    LOG_D(TAG, "Successfully set drp_status xattr for file: %s", filepath.c_str());
                }else{
                    LOG_E(TAG, "Failed to set drp_status xattr for file: %s", filepath.c_str());
                }
            }
            if((true == drp_enabled) && (file_is_present(nd_device_obj->get_external_eMMC_mount_path() + fileName + ld_extn))){
                string filepath = nd_device_obj->get_external_eMMC_mount_path() + fileName + ld_extn;
                if(set_file_xattr(filepath, FileMetadataKey::drp_status, &resp_msg.upload, sizeof(resp_msg.upload)) == XATTR_OK){
                    LOG_D(TAG, "Successfully set drp_status xattr for file: %s", filepath.c_str());
                }else{
                    LOG_E(TAG, "Failed to set drp_status xattr for file: %s", filepath.c_str());
                }
            }
            if((true == drp_enabled) && is_ext_cam((circular_buffer_camtype_t)get_cam_num_from_filename(fileName))){
                // Write this information in outward camera file also
                string outward_filename = fileName;
                outward_filename[0] = '0' + CIRCULAR_BUFFER_CAM_FRONT; // rename to outward camera file
                // HQ File
                if(file_is_present(nd_device_obj->get_external_eMMC_mount_path() + outward_filename)){
                    string filepath = nd_device_obj->get_external_eMMC_mount_path() + outward_filename;
                    if(set_file_xattr(filepath, FileMetadataKey::ext_drp_status, &resp_msg.upload, sizeof(resp_msg.upload)) == XATTR_OK){
                        LOG_D(TAG, "Successfully set drp_status xattr for file: %s", filepath.c_str());
                    }else{
                        LOG_E(TAG, "Failed to set drp_status xattr for file: %s", filepath.c_str());
                    }
                }
                // LQ file
                if(file_is_present(nd_device_obj->get_external_eMMC_mount_path() + outward_filename + ld_extn)){
                    string filepath = nd_device_obj->get_external_eMMC_mount_path() + outward_filename + ld_extn;
                    if(set_file_xattr(filepath, FileMetadataKey::ext_drp_status, &resp_msg.upload, sizeof(resp_msg.upload)) == XATTR_OK){
                        LOG_D(TAG, "Successfully set drp_status xattr for file: %s", filepath.c_str());
                    }else{
                        LOG_E(TAG, "Failed to set drp_status xattr for file: %s", filepath.c_str());
                    }
                }
            }
        }
        //write upload reason into dev shm nd_xattr for dummy file
        string devshm_filepath = DEV_SHM + nd_xattr + fileName + "_" + to_string(query_msg->file_type);
        upl_to_cb_query_result_t upload_status_value = resp_msg.reason;
        if((true == drp_enabled) && (upload_status_value == CB_UPLOAD)){
            upload_status_value = CB_UPLOAD_FOR_DRP;
        }
        if(write_into_dev_shm_nd_xattr_file(devshm_filepath, upload_status_value)){
            LOG_D(TAG, "Successfully wrote upload_status xattr for dummy file in /dev/shm/nd_xattr : %s", devshm_filepath.c_str());
        }else{
            LOG_E(TAG, "Failed to write upload_status xattr for dummy file in /dev/shm/nd_xattr : %s", devshm_filepath.c_str());
        }
        // Clear previous pending message.
        int rc = -1;
        struct msqid_ds buf;
        rc = msgctl(CIRC_BUFF_ctx->msg_q_cb_to_upl->get_qid(), IPC_STAT, &buf);
        if(rc == -1){
            LOG_E(TAG,"msgctl failed with error no -> %d",errno);
        }else{
            int  num_messages = buf.msg_qnum;
            LOG_I( TAG, " Num of Previous messages still pening:  %d", num_messages );
            while( num_messages > 0 ) {
                LOG_I(TAG,"poping out previous message still pending with UPL" );
                msg = CIRC_BUFF_ctx->msg_q_cb_to_upl->receive(IPC_NOWAIT) ;
                delete msg;
                num_messages-- ;
            }
            bool msg_status = send_msg((generic_msg_t*)&resp_msg, UPLOADER_QUERY_RESPONSE_FROM_CB, sizeof(resp_msg), Q_NAME_TO_CB, Q_NAME_TO_UPL, 0);
            if(false == msg_status){
                LOG_E(TAG,"Failed to send upload_query response send to uploder");
            }
        }
        delete query_msg;
    }

}

bool get_dir_details(string folder, vector<file_data_str_t> & allfiles_dir, int &audio_file_cnt)
{
    bool status = false;
    string file_path = "";
    file_data_str_t filedata;
    vector <dir_data_s> vec;
    
    string extn;
    if(!get_directory_all(folder, vec)) {
        LOG_E(TAG, "Failed to get file list from all directory");
        return false;
    }
    string path = nd_device_obj->get_external_eMMC_mount_path();
    for( vector<dir_data_s>::iterator iter= vec.begin(), end = vec.end();
                           iter!=end; iter++ ) {

        if((*iter).file_type == 2){
            LOG_I(TAG, "Detected directory %s. Not taking any action on it", ((*iter).file_name.c_str()));
            status = true;
            continue;
        }

        if((*iter).file_type == 1)
        {
            if ((*iter).file_size == 0 )
            {
                file_path.clear();
                file_path = get_file_path((*iter).file_name);
                if(file_path == "") {
                    LOG_E(TAG, "File is not available");
                }
		    string full_path = file_path + "/" + (*iter).file_name;
    		if(file_size( full_path )  == 0) {
			LOG_I(TAG, "Detected %s with 0 bytes size, Not adding to files list so that this gets deleted from DB", ((*iter).file_name.c_str()));
			LOG_I(TAG, "deleting %s", full_path.c_str());
			file_delete(full_path);
			continue;
		}
            }
            else {
                //allfiles_dir will contain only directories and files
                filedata = {0};
                filedata.index_id = (*iter).serial_no;
                filedata.file_name = (*iter).file_name;
                filedata.file_type = (*iter).file_type;
                filedata.file_size = (*iter).file_size;
                // This check is not needed as all files present in directory will need to be checked, if corresponding entry is present in DB or not
                string fullfile_name = path + filedata.file_name;
                if(CIRC_BUFF_ctx->extended_attr_enabled){
                    bool success = true;
                    if(get_file_xattr(fullfile_name, FileMetadataKey::sid, &filedata.sessionCount, sizeof(filedata.sessionCount)) != XATTR_OK){
                        filedata.sessionCount = sessionCount_from_file(filedata.file_name);
                        success = false;
                    }
                    if(get_file_xattr(fullfile_name, FileMetadataKey::udid, &filedata.udid, sizeof(filedata.udid)) != XATTR_OK){
                        filedata.udid = udid_from_file(filedata.file_name);
                        success = false;
                    }
                    filedata.xattr_present = success;
                }
                else{
                    filedata.sessionCount = sessionCount_from_file(filedata.file_name);
                    filedata.udid = udid_from_file(filedata.file_name);
                    filedata.xattr_present = false;
                }
                bool add_file_to_list = true;
                circular_buffer_filestatus_t file_status = CIRCULAR_BUFFER_STATUS_ERROR;
                if((fullfile_name.find(".zip") != string::npos) && (get_file_xattr(fullfile_name, FileMetadataKey::status, &file_status, sizeof(file_status)) == XATTR_OK)){
                    if(file_status == CIRCULAR_BUFFER_STATUS_DELETE){
                        add_file_to_list = false;
                    }
                }

                if(add_file_to_list){
                    allfiles_dir.push_back(filedata);
                    status = true;
                }
                get_extension_from_filename(filedata.file_name, extn);
                if(extn == ".aac"){  
                    audio_file_cnt++ ;
                }
            }
        }
    }
    return status;
}

bool compareBySessionCount_and_udid(const file_data_str_t& a, const file_data_str_t& b) {

    if(a.udid == b.udid){
        return (a.sessionCount < b.sessionCount); 
    }

    return (a.udid < b.udid);
}

bool clean_memorycard(vector<file_data_str_t> & allfiles_dir, vector<file_data_str_db_t>& allfiles_db)
{
    //sort allfiles_dir by sessionCount and UDID (in ascending)
    if(file_is_present(CB_DB_CORRUPTION_FILE)){
        LOG_I(TAG, "Sort allfiles_dir by sessionCount and UDID");
        sort(allfiles_dir.begin(), allfiles_dir.end(), compareBySessionCount_and_udid);
        file_delete(CB_DB_CORRUPTION_FILE);
    }
    // check for valid entry for all files in DIR
    int dir_size = allfiles_dir.size();
    int db_size = allfiles_db.size();
    int dir_count = 0, db_count = 0;
    // Map will filename as the key and pair of file_size and rec_vid_enabled as the value, this will help to quickly search for file in DB and also check the file size and rec_vid_enabled status if file is present in DB
    unordered_map<string, file_data_str_db_t> db_file_unordered_map;
    unordered_map<string, file_data_str_db_t>::const_iterator itr;
    int result ;
    while(db_count < db_size) {
        db_file_unordered_map.insert(std::make_pair(allfiles_db[db_count].file_data.file_name, allfiles_db[db_count]));
        db_count++;
    }
    unordered_map<string, file_data_str_db_t>::const_iterator itr_end = db_file_unordered_map.end();
    while(dir_count < dir_size) {
        if(allfiles_dir[dir_count].file_type != 1) {
            LOG_E(TAG, "Found a different file_type @@%s@@ take action HERE", 
                        allfiles_dir[dir_count].file_name.c_str());
            dir_count++;
            continue;
        }
        // search for entry in DB
        string searchfor_file = allfiles_dir[dir_count].file_name;
        itr = db_file_unordered_map.find(searchfor_file);
        if(itr == itr_end ){
            result =  -1;
        }
        else{
            result = 1;
        }

        // Result -1 means file is present in DIR but not in DB.
        if(result == -1){
            LOG_I(TAG, "Detected a MISCS file in DIR; adding %s", searchfor_file.c_str());
            bool ret = adding_miscs_file_to_DB(searchfor_file);
            if(ret == false) {
                LOG_C(TAG, "Failed in inserting Misc file %s to DB", searchfor_file.c_str());
#if 0
                // for all false cases, the error message will be logged by the
                // adding_miscs_file_to_DB function
                // Note: in this instance the file will be deleted because there
                // may be no entry for the file in the database and if it is not
                // deleted, it can lead to a pile up of unknown files in the sdcard
                bool ret = delete_file_from_sdcard(searchfor_file);
                if(ret == false) {
                    LOG_E(TAG, "delete_file_from_sdcard returned error");
                }
#endif
            } else {
            }
        }
        else {

            //file exists in db. check file sizes
            if(((*itr).second.file_data.file_size != allfiles_dir[dir_count].file_size) && ((*itr).second.rec_vid_enabled != REC_VID_ENABLED_RESET)){//Only updating the size if file is not a privacy file
                LOG_E(TAG, "file_size mismatch %s : db file size = %lld, dir_file_size = %lld",
                        searchfor_file.c_str(), (*itr).second.file_data.file_size, allfiles_dir[dir_count].file_size);
                //update file size in db
                LOG_D(TAG, "file size mismatch %s : updating file size", searchfor_file.c_str());
                //bool ret = adding_miscs_file_to_DB(searchfor_file);
                bool ret = update_file_size_db(searchfor_file, allfiles_dir[dir_count].file_size);
                if(ret == false) {
                    LOG_E(TAG, "update_file_size_db returned error. Unable to update file size for %s",
                        searchfor_file.c_str());
                    // not deleting file because adding_miscs_file_to_DB should fail in rare cases
                    // deletion will result in loss of data
                    // In this case, there is an entry in the db for the file, which means that
                    // it will be handled eventually i.e. deleted to make space. So there is no
                    // issue of unknown files getting piled up
                }

            }

            if((true == CIRC_BUFF_ctx->extended_attr_enabled) && (allfiles_dir[dir_count].xattr_present == false)){
                // This means that file is present in directory and DB but xattr is not present on file, this can happen in case of old files which were added to DB before enabling extended attribute, so adding xattr to these files
                LOG_I(TAG, "Adding xattr to file %s as it is missing, this can happen for old files which were added to DB before enabling extended attribute", searchfor_file.c_str());
                circular_buffer_fileinfo_t file_info = {0};
                populate_add_file_msg_from_db_entry((*itr).second, file_info);
                bool read_back_xattrs=false;
                bool success = write_addfile_metadata_to_xattrs(file_info, read_back_xattrs);
                if(success == false) {
                    LOG_E(TAG, "Failed to add xattr for file %s", searchfor_file.c_str());
                }
                // Write status and file compression as these will not written as part of write_addfile_metadata_to_xattrs as these are updated later in the flow, so writing these separately to make sure that all the required xattrs are present on file
                string filepath = nd_device_obj->get_external_eMMC_mount_path() + searchfor_file;
                circular_buffer_filestatus_t status= (*itr).second.status;
                if(set_file_xattr(filepath, FileMetadataKey::status, &status, sizeof(status)) != XATTR_OK) {
                    LOG_E(TAG, "Failed to update status xattr for file: %s", filepath.c_str());
                }
                circular_buffer_file_compression_t compression = (*itr).second.file_compression;
                if(set_file_xattr(filepath, FileMetadataKey::compr_type, &compression, sizeof(compression)) != XATTR_OK) {
                    LOG_E(TAG, "Failed to update compression xattr for file: %s", filepath.c_str());
                }
            }

            LOG_D(TAG, "found file %s; moving to next file", searchfor_file.c_str());
        }

        dir_count++;
    }
    // Call insert_batch_file_DB, just to flush the vector used for batch insertion, it is called with DUMMY file info structure
    // As we want to flush the vector if some files are left to be added to DB, as these files will not get added until the vector is flushed, and we don't want these files to be missed in DB
    circular_buffer_fileinfo_t file_info = {0};
    bool flush_ok = insert_batch_file_DB(file_info, BATCH_MODE_FLUSH);
    if(flush_ok == false) {
        LOG_E(TAG, "Failed to flush batch vector used for DB insertion");
    }
    return true;
}

static void populate_add_file_msg_from_db_entry(const file_data_str_db_t& db_entry,
                                                circular_buffer_fileinfo_t& file_info)
{
    file_info = {0};
    nd_strncpy(file_info.base_file_name,
               db_entry.file_data.file_name.c_str(),
               FNAME_LEN);
    file_info.file_size = db_entry.file_data.file_size;
    file_info.file_type = (circular_buffer_filetype_t)db_entry.file_data.file_type;
    file_info.time = db_entry.time;
    file_info.camtype = db_entry.camtype;
    file_info.tc_status = db_entry.tc_status;
    file_info.udid = db_entry.file_data.udid;
    file_info.sessionCount = db_entry.file_data.sessionCount;
    file_info.duration = 60000;
    file_info.rec_vid_enabled = db_entry.rec_vid_enabled;
    file_info.upl_vid_enabled = db_entry.upl_vid_enabled;
}

bool clean_db(vector<file_data_str_t> & allfiles_dir, vector<file_data_str_db_t>& allfiles_db)
{
    // check for valid entry for all files in DIR
    int dir_size = allfiles_dir.size();
    int db_size = allfiles_db.size();
    int dir_count = 0, db_count = 0;
    string file_path = "";

    unordered_set<string> dir_file_unordered_set;
    unordered_set<string>::const_iterator itr;
    int result ;
    while(dir_count < dir_size) {    
        dir_file_unordered_set.insert(allfiles_dir[dir_count++].file_name);
    }
    unordered_set<string>::const_iterator itr_end = dir_file_unordered_set.end();
    // get the mount path for eMMC
    string path = nd_device_obj->get_external_eMMC_mount_path();
    while(db_count < db_size) {
        // search for entry in DB
        string searchfor_file = allfiles_db[db_count].file_data.file_name;
        itr = dir_file_unordered_set.find(searchfor_file);
        if(itr == itr_end ){
            result =  -1;
        }
        else{
            result = 1;
        }

        if(result == -1){
            LOG_I(TAG, "Detected a MISC entry in DB; deleting %s", searchfor_file.c_str());
            if( is_ext_cam_file(searchfor_file) && save_ext_cam_files_in_dhub)
            {
                LOG_I(TAG, "Not Deleting Entry From DB As It Is An External Camera File And save_ext_cam_files_in_dhub Is True");
            }else if(allfiles_db[db_count].rec_vid_enabled == REC_VID_ENABLED_RESET){
                LOG_I(TAG, "Not deleting this entry from DB as Record Privacy is enabled, %s",searchfor_file.c_str());
                //old ota privacy files which are not present in nd_sdcard 
                if(CIRC_BUFF_ctx->extended_attr_enabled) {
                    string filepath = path + searchfor_file;
                    if(!file_is_present(filepath)) {
                        // Reconstruct full file metadata from DB so dummy file xattrs mirror original DB state.
                        circular_buffer_fileinfo_t file_info;
                        populate_add_file_msg_from_db_entry(allfiles_db[db_count], file_info);
                        bool read_back_xattrs=false;
                        write_addfile_metadata_to_xattrs(file_info, read_back_xattrs); 
                        // updating status xattr to cloud notified
                        // as these are old files updating status to notified
                        // Status will fetched from DB for notifying to cloud
                        circular_buffer_filestatus_t status= allfiles_db[db_count].status;
                        if(set_file_xattr(filepath, FileMetadataKey::status, &status, sizeof(status)) == XATTR_OK) {
                            LOG_D(TAG, "Successfully updated status xattr for file: %s", filepath.c_str());
                        }
                        circular_buffer_file_compression_t compression = allfiles_db[db_count].file_compression;
                        if(set_file_xattr(filepath, FileMetadataKey::compr_type, &compression, sizeof(compression)) != XATTR_OK) {
                            LOG_E(TAG, "Failed to update compression xattr for file: %s", filepath.c_str());
                        }
                    }    
                }
            }
            else
            {
                if( searchfor_file.find(ld_extn) != string::npos ) {
                    string hd_file_name = searchfor_file.substr(0, searchfor_file.length() - ld_extn.length());

                    if(get_file_path(hd_file_name) != "") {
                        LOG_I(TAG, "hd file is present. hard_delete_entry_from_db");
                        hard_delete_entry_from_db(searchfor_file);
                    }
                    else {
                        LOG_I(TAG, "delete_entry_from_db");
                        delete_entry_from_db(allfiles_db[db_count].file_data.index_id, allfiles_db[db_count].file_data.file_type);
                        allfiles_db[db_count].file_data.file_type = CIRCULAR_BUFFER_STATUS_DELETE;
                    }
                }
                else if( searchfor_file.find("mp4") != string::npos ) {
                    string ld_file_name = searchfor_file +  ld_extn ;
                    if(get_file_path(ld_file_name) != "") {
                        LOG_I(TAG, "ld file is present. hard_delete_entry_from_db");
                        hard_delete_entry_from_db(searchfor_file);
                    }
                    else {
                        LOG_I(TAG, "delete_entry_from_db");
                        delete_entry_from_db(allfiles_db[db_count].file_data.index_id, allfiles_db[db_count].file_data.file_type);
                        allfiles_db[db_count].file_data.file_type = CIRCULAR_BUFFER_STATUS_DELETE;
                    }
                }
                else { // aac file or .zip file
                    LOG_I(TAG, "delete_entry_from_db");
                    delete_entry_from_db(allfiles_db[db_count].file_data.index_id, allfiles_db[db_count].file_data.file_type);
                    allfiles_db[db_count].file_data.file_type = CIRCULAR_BUFFER_STATUS_DELETE;

                }
            }
        }
        else {
            LOG_D(TAG, "found entry %s; moving to next entry", searchfor_file.c_str());
        }

        db_count++;
    }
    return false;
}

bool get_details(vector<file_data_str_t>& all_files_dir,vector<file_data_str_db_t>& all_files_db)
{
    int audio_file_cnt_dir = 0 ;
    if (get_dir_details(nd_device_obj->get_external_eMMC_mount_path(), all_files_dir, audio_file_cnt_dir) == false) {
        LOG_E(TAG, "failed in get_dir_details");
        return false;
    }
    LOG_I(TAG, "total files found in DIR %d", all_files_dir.size());
    LOG_I(TAG, "total Audio files found in DIR %d", audio_file_cnt_dir );

    if (!get_db_details(all_files_db)){
        LOG_E(TAG, "failed to get_db_details");
        return false;
    }
    LOG_I(TAG, "total files found in DB  %d", all_files_db.size());

    return true;
}

bool circular_buffer_cleanup(vector<file_data_str_t>& all_files_dir,vector<file_data_str_db_t>& all_files_db)
{
    LOG_I(TAG, "circular_buffer_cleanup: clean_memorycard");
    clean_memorycard(all_files_dir, all_files_db);
    /*
     * After clean_memorycard() operation, there can be new entries added to
     * the db (file were copied to sdcard but db was not updated due to abrupt
     * shutdown)
     * In addition clean_memorycard() function will also rename tmp transcoded
     * files to the original file name (if original file is deleted) / delete
     * the existing transcoded file if original file is present.
     * This change is required to fix BHAGEERA-2589 and BHAGEERA-2618
     */
    LOG_I(TAG, "circular_buffer_cleanup: after clean_memorycard: getting db details again and dir details again");
    all_files_dir.clear();
    all_files_db.clear();
    if (!get_details(all_files_dir, all_files_db)){
        LOG_E(TAG, "circular_buffer_cleanup: failed to get dir and db details after clean_memorycard() function");
        // Notify health mon
        string str_msg = "get_details failed inside circular_buffer_cleanup";
        LOG_E(TAG, str_msg.c_str() );
        nd_service_obj->send_err_msg(SM_E_CB_SD_CARD_GET_FILE_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
        return false;
    }
    LOG_I(TAG, "circular_buffer_cleanup: after clean_memorycard: clean_db");
    clean_db(all_files_dir, all_files_db);
    return true;
}

bool fill_circular_buffer_header()
{
    if(init_config() == false)
    {
        LOG_E(TAG, "init_config() failed in fill_circular_buffer_header(). Default values set.");
    }

    string config_temp;

    config_temp = CIRC_BUFF_ctx->device_config->getConfig("identity","deviceId",DEF_INI_DEVICE_ID);
    if( config_temp == "" ) {
        config_temp = CIRC_BUFF_ctx->device_config->getConfig("identity","deviceid",DEF_INI_DEVICE_ID);
        if( config_temp == "" ) {
            LOG_C(TAG,"cannot find deviceId");
        }
    }
    LOG_I(TAG, "deviceid %s length %d", config_temp.c_str(), config_temp.length());
    CIRC_BUFF_ctx->header.device_id = config_temp;

    config_temp = CIRC_BUFF_ctx->device_config->getConfig("identity","sessionId",DEF_INI_SESSION_ID);
    if( config_temp == "" ) {
        config_temp = CIRC_BUFF_ctx->device_config->getConfig("identity","sessionid",DEF_INI_SESSION_ID);
        if( config_temp == "" ) {
            LOG_C(TAG,"cannot find sessionid");
        }
    }
    LOG_I(TAG, "sessionid %s length %d", config_temp.c_str(), config_temp.length());
    CIRC_BUFF_ctx->header.session_id = config_temp;

    config_temp = CIRC_BUFF_ctx->nd_config->getConfig("version","ndDevice",DEF_INI_DEVICE_VERSION);
    if( config_temp == "" ) {
        config_temp = CIRC_BUFF_ctx->nd_config->getConfig("version","nddevice",DEF_INI_DEVICE_VERSION);
        if( config_temp == "" ) {
            LOG_C(TAG,"cannot find ndDevice");
        }
    }
    LOG_I(TAG, "nddevice version %s length %d", config_temp.c_str(), config_temp.length());
    CIRC_BUFF_ctx->header.device_version = config_temp;
    CIRC_BUFF_ctx->header.version = config_temp;

    config_temp = CIRC_BUFF_ctx->device_config->getConfig("identity","deviceType",DEF_INI_DEVICE_TYPE);
    if( config_temp == "" ) {
        config_temp = CIRC_BUFF_ctx->device_config->getConfig("identity","devicetype",DEF_INI_DEVICE_TYPE);
        if( config_temp == "" ) {
            LOG_C(TAG,"cannot find devicetype");
        }
    }
    LOG_I(TAG, "devicetype %s length %d", config_temp.c_str(), config_temp.length());
    CIRC_BUFF_ctx->header.devicetype = config_temp;

    CIRC_BUFF_ctx->header.dafault_clip_durtn = 60000;
    CIRC_BUFF_ctx->header.iscomplete = false;

    // clear s
    deinit_config();

    return true;
}

bool init_sdcard_config(void) {

    old_path = nd_device_obj->get_external_eMMC_old_mount_path();
    sdcard_img_path = nd_device_obj->get_external_eMMC_loop_mount_path();
    return true;
}
/**
 * @brief Checks if a given config variable is enabled under a specific section in the INI file.
 *
 * @param config_str The configuration key to check (e.g., "store_dms_file").
 * @param section The section in the INI file to look under (e.g., "Transcode").
 * @return true if the config is enabled ("true") in the given section, false otherwise.
 */
bool read_store_dms_file_config(const std::string& section, const std::string& config_str, const std::string& default_confg_str) {
    Config_parser c(BAGHEERA_CONFIG_INI);
    if (!c.getParseStatus()) {
        LOG_E(TAG, "Can't parse %s", BAGHEERA_CONFIG_INI);
        return (default_confg_str == std::string("true"));
    }

    bool get_override_val = true;
    bool is_val_overridden = false;

    if (c.isPresent(section, config_str)) {
        std::string value = c.getConfig(section, config_str, default_confg_str, get_override_val, is_val_overridden);
        if (value == "false") {
            LOG_I(TAG, "%s is disabled under section [%s] in config file", config_str.c_str(), section.c_str());
            return false;
        } else if (value == "true") {
            LOG_I(TAG, "%s is enabled under section [%s] in config file", config_str.c_str(), section.c_str());
            return true;
        }
	else {
        	return (default_confg_str == std::string("true"));
	}
    } else {
        LOG_E(TAG, "Config entry [%s:%s] not found in config file", section.c_str(), config_str.c_str());
    }

    return (default_confg_str == std::string("true"));
}
bool read_deleteUnknownFile_config() {
    Config_parser c(BAGHEERA_CONFIG_INI);
    if (c.getParseStatus() != true)
    {
        LOG_E (TAG,"Can't parse %s", BAGHEERA_CONFIG_INI);
        return false;
    }
    bool get_override_val = true;
    bool is_val_overridden = false;
    if(c.isPresent("sdcard", "deleteUnknownFile") ) {
        string cnfg_str =  c.getConfig("sdcard", "deleteUnknownFile", "false", get_override_val, is_val_overridden);
        if( "false" == cnfg_str) {
            LOG_I (TAG,"deleteUnknownFile to sdcard is disabled in config file");
            return true;
        }
        else if( "true" == cnfg_str ) {
            LOG_I (TAG,"deleteUnknownFile to sdcard is enabled in config file");
            deleteUnkownFileType = true;
            return true;
        }
        else {
            LOG_I (TAG,"deleteUnknownFile to sdcard is disabled in config file");
            nd_service_obj->send_err_msg(SM_E_CB_CONFIG_READING_FAIL, NDService::UNUSED_ERR_AUX_CODE, "deleteUnknownFile: config value: " + cnfg_str );
            return true;
        }
    }
    else {
        LOG_E (TAG,"sdcard:deleteUnknownFile not present in config file");
    }

    return true;

}
bool read_root_filesystem_monitor_config() {
    Config_parser cfg_prsr(BAGHEERA_CONFIG_INI);
    if (!cfg_prsr.getParseStatus()) {
        LOG_E(TAG, "Can't parse %s for root filesystem monitoring config", BAGHEERA_CONFIG_INI);
        return false;
    }
    bool get_override_val = true;
    bool is_val_overridden = false;
    std::string root_monitor_str = cfg_prsr.getConfig("sdcard", "root_ro_correct", "false", get_override_val, is_val_overridden);
    if (root_monitor_str == "true") {
        root_monitor_enabled = true;
        LOG_I(TAG, "Root filesystem correction is ENABLED");
        return true;
    } else {
        root_monitor_enabled = false;
        LOG_I(TAG, "Root filesystem correction is DISABLED");
        return false;
    }}
    bool request_reboot_via_svc(const string& reason) 
    {
    LOG_I(TAG, "Requesting system reboot via power monitor - Reason: %s", reason.c_str());
    if(send_powermon_to_reboot(CIRC_BUFF_ctx->circular_buffer_q_name, REQ_POWERMON_SVC_TO_REBOOT) == false) {
        LOG_E(TAG, "Power monitor reboot failed, using direct system reboot");
        system_reboot();
        return false;
    }
    
    LOG_I(TAG, "Reboot request sent to power monitor successfully");
    return true;
    }
void root_filesystem_status_check_repair()
{
    
        string done_file_path = "/data/done";
            if (file_is_present(done_file_path)) {
                LOG_I(TAG, "DONE file found (%s) - skipping root filesystem repair check", done_file_path.c_str());
                return; // EXIT - only when DONE file is present
            }
        LOG_I(TAG, "Performing root filesystem health check");
          if (nd_factory_utils::is_readonly()) {
        string critical_msg = "Root filesystem is RO";
        LOG_C(TAG, "%s", critical_msg.c_str());
        
        // Send critical error to Service Manager
        nd_service_obj->send_err_msg(SM_E_CB_ROOT_FS_READ_ONLY, NDService::UNUSED_ERR_AUX_CODE, critical_msg);
             power_crank_levels_t crank_level = nd_device_obj->get_crank_level();
            if (crank_level != CRANK_LOW) {
                LOG_I(TAG, "Skipping KRAIT slot repair script - crank level is not safe (%d)", (int)crank_level);
                
                string skip_msg = "KRAIT slot repair script skipped - crank level not safe (level: " + 
                                std::to_string((int)crank_level) + ")";
                nd_service_obj->send_err_msg(SM_E_CB_ROOT_FS_READ_ONLY, NDService::UNUSED_ERR_AUX_CODE, skip_msg);
                return; // EXIT WITHOUT TRIGGERING SCRIPT
            }
            LOG_I(TAG, "Crank level is safe (CRANK_LOW) - proceeding with slot repair");
            
            string slotchangerofix_path = "/home/ubuntu/.nddevice/latest/service/circular_buffer/slotchangerofix.sh";
            if (file_is_present(slotchangerofix_path)) {
                LOG_C(TAG, "KRAIT device detected with safe crank level - triggering slot repair script");
                
                // Pass "false" for service-managed mode so script returns exit 10
                string script_args = " false";  
                string repair_cmd = "/bin/bash " + slotchangerofix_path + script_args;
                
                LOG_I(TAG, "Executing script with CB management: %s", repair_cmd.c_str());
                
                int result = system(repair_cmd.c_str());
                
                if (result == -1) {
                    LOG_E(TAG, "CRITICAL: system() call failed - errno: %d (%s)", errno, strerror(errno));
                    
                    string failure_msg = "FAILED to execute KRAIT slot repair script - system call failed";
                    nd_service_obj->send_err_msg(SM_E_CB_ROOT_FS_READ_ONLY, NDService::UNUSED_ERR_AUX_CODE, failure_msg);
                    return;
                }
                
                // Check if process executed normally and extract exit code

                if (WIFEXITED(result)) {
                    int exit_code = WEXITSTATUS(result);
                    
                    LOG_I(TAG, "KRAIT script completed with exit code: %d", exit_code);
                    
                    if (exit_code == 10) {
                        // SERVICE-MANAGED MODE: CB handles reboot
                        LOG_I(TAG, "KRAIT Stage 1 completed successfully - CB should handle reboot to Slot B");
                        
                        string stage1_msg = "KRAIT Stage 1 completed, CB handling reboot to Slot B (Crank: SAFE)";
                        nd_service_obj->send_err_msg(SM_E_CB_ROOT_FS_READ_ONLY, NDService::UNUSED_ERR_AUX_CODE, stage1_msg);
                        
                        // Send reboot request to SVC
                        if (request_reboot_via_svc("KRAIT Stage 1 completed successfully")) {
                            LOG_I(TAG, "Reboot request sent to SVC successfully");
                        } else {
                            LOG_E(TAG, "Failed to send reboot request to SVC");
                        }
                        
                    } else if (exit_code == 0) {
                        // SCRIPT-MANAGED MODE: Script handled reboot itself
                        LOG_I(TAG, "KRAIT script completed in script-managed mode - script handled reboot directly");
                        
                        string self_managed_msg = "KRAIT script completed in script-managed mode";
                        nd_service_obj->send_err_msg(SM_E_CB_ROOT_FS_READ_ONLY, NDService::UNUSED_ERR_AUX_CODE, self_managed_msg);
                        
                    } else {
                        // ERROR CASES: Any other exit code
                        LOG_E(TAG, "KRAIT slot repair script failed with exit code: %d", exit_code);
                        
                    }
                    
                }
                else {
                    LOG_E(TAG, "KRAIT slot repair script did not terminate normally");
                }
            
        
        } else {
                    LOG_E(TAG, "KRAIT slot repair script not found: %s", slotchangerofix_path.c_str());
            
            
        }
    }
        else {
            LOG_I(TAG, "Root filesystem is RW");
        }
} 
int main(int argc, char *argv[])
{
    nd_service_obj = NDService::get_service_obj(TAG);
    service_start_time = get_system_monotonic_time();    
    printf("initilizing logger\n");
    bool status_log = nd_log_init( log_dir.c_str() );
    if(status_log == false) {
        printf("unable to init logger ; Exiting from circ_buff");
        nd_service_obj->send_err_msg(SM_E_CB_LOG_INIT_FAIL, NDService::UNUSED_ERR_AUX_CODE, 
            "unable to init logger :: Exiting from main" );
        return 1;
    }

#ifdef ROUTE_LOGS
    route_logs( log_dir.c_str() );
#endif
    LOG_C(TAG, "#####Starting Circular_Buffer#####");
    nd_device_obj_init();
    init_sdcard_config();
    CIRC_BUFF_ctx->db_main_msg_q =  nd_msgq_t::get_msgq( CIRC_BUFF_ctx->circular_buffer_q_name,
                                                         nd_msgq_t::ND_MSGQ_SERVER);
    if( CIRC_BUFF_ctx->db_main_msg_q == NULL ) {   
        // Notify health mon     
        string str_msg = "failed in db_main_msg_q get_msgq ; Exiting from circ_buff";
        LOG_C(TAG, str_msg.c_str() );
        nd_service_obj->send_err_msg(SM_E_CB_MSG_QUEUE_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
        return false;
    }
    LOG_I(TAG, "Success in db_main_msg_q\n");

    Config_parser nd_config_parser(ND_CONFIG_ANALYTICS);
    bool get_override_val = true;
    bool is_val_overridden = false;

    int dms_drowsy = 0;
    if (nd_config_parser.getParseStatus()) {
        string_to_integer(nd_config_parser.getConfig("dms_drowsy", "enabled", "0", get_override_val, is_val_overridden), dms_drowsy);
        if (dms_drowsy) {
            LOG_I(TAG, "dms_drowsy is enabled in nd_config.ini, enabling DMS camera");
            dms_camera_enabled = true;
        }
    }
    else {
        LOG_E(TAG, "Failed to parse %s", ND_CONFIG_ANALYTICS);
    }
    if (!dms_drowsy) {
        LOG_I(TAG, "dms_drowsy is disabled in nd_config.ini");
        dms_camera_enabled = read_store_dms_file_config("dms_camera", "enabled", "false" ) ;
    }

    if(dms_camera_enabled){
        LOG_I(TAG, "DMS camera is enabled");
        store_lq_dms_file = read_store_dms_file_config("Transcode", store_lq_dms_file_config_str, "false") ;
        LOG_I(TAG, "store_dms_file : %d, store_lq_dms_file: %d", store_dms_file, store_lq_dms_file);
    }
    else{
        LOG_I(TAG, "DMS camera is disabled");
    }

    read_deleteUnknownFile_config() ;

    if (pthread_mutex_init(&CIRC_BUFF_ctx->db_handle_mutex, NULL) != 0){
        // Notify health mon
        string str_msg = "mutex init failed ; Exiting from circ_buff";
        LOG_C(TAG, str_msg.c_str() );
        nd_service_obj->send_err_msg(SM_E_CB_MUTEX_INIT_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
        return false;
    }
    LOG_I(TAG, "success mutex init");

    bool ret;
    string db_path_file = nd_device_obj->get_db_base_path() + "/" + CIRCULAR_BUFF_DBFILE_NAME;
    ret = CIRC_BUFF_ctx->open_db(db_path_file.c_str(), &CIRC_BUFF_ctx->db_handle);
    if(ret == false)    {
        // Notify health mon
        string str_msg = "Failed to open DB";
        LOG_C(TAG, str_msg.c_str() );
        nd_service_obj->send_err_msg(SM_E_CB_OPEN_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
        return false;
    }
    LOG_I(TAG, "success in open DB");

    ret = CIRC_BUFF_ctx->create_table_db( CIRC_BUFF_ctx->db_handle);
    if(ret == false)    {
        // Notify health mon
        string str_msg = "Failed to create_table_db";
        LOG_C(TAG, str_msg.c_str() );
        nd_service_obj->send_err_msg(SM_E_CB_CREATION_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
        return false;
    }
    
    ret = CIRC_BUFF_ctx->create_table_db_details( CIRC_BUFF_ctx->db_handle );
    if(ret == false)    {
        // Notify health mon
        string str_msg = "Failed to create_table_db details";
        LOG_C(TAG, str_msg.c_str() );
        nd_service_obj->send_err_msg(SM_E_CB_CREATION_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
        return false;
    }
    LOG_I(TAG, "success in create_table_db");

    string table_name = "VIDFILES";
    string col_name = "UDID";
    bool add_col_status = check_and_add_column(CIRC_BUFF_ctx->db_handle, table_name, col_name);
    if(add_col_status == false) {
        LOG_E(TAG, "failed to add udid column to db");
        nd_service_obj->send_err_msg(SM_E_CB_DB_UDID_COL_ADD_FAIL, NDService::UNUSED_ERR_AUX_CODE,
                                        "failed to add udid column to db");
    }
    table_name = "VIDFILES";
    col_name = "SESSIONCOUNT";
    add_col_status = check_and_add_column(CIRC_BUFF_ctx->db_handle, table_name, col_name);
    if(add_col_status == false) {
        LOG_E(TAG, "failed to add sessionCount column to db");
        nd_service_obj->send_err_msg(SM_E_CB_DB_SESSIONCOUNT_COL_ADD_FAIL, NDService::UNUSED_ERR_AUX_CODE,
                                        "failed to add sessionCount column to db");
    }

    table_name = "VIDFILES";
    col_name = "UPL_VID_ENABLED";
    add_col_status = check_and_add_column(CIRC_BUFF_ctx->db_handle, table_name, col_name, DEFAULT_VALUE_UPL_VID_ENABLED_COLUMN);
    if (add_col_status == false) {
        LOG_E(TAG, "failed to add UPL_VID_ENABLED column to DB");
        nd_service_obj->send_err_msg(SM_E_CB_DB_UPL_VID_ENABLED_COL_ADD_FAIL, NDService::UNUSED_ERR_AUX_CODE,
                                        "failed to add UPL_VID_ENABLED column to DB");
    }

    table_name = "VIDFILES"; // This table name can be defined as a macro and used elsewhere as well
    col_name = "REC_VID_ENABLED"; // Same column names can be defined as macros
    add_col_status = check_and_add_column(CIRC_BUFF_ctx->db_handle, table_name, col_name, DEFAULT_VALUE_REC_VID_ENABLED_COLUMN);
    if(false == add_col_status) {
        LOG_E(TAG,"Failed to add REC_VID_ENABLED columns to VIDFILES DB");
        nd_service_obj->send_err_msg(SM_E_CB_DB_REC_VID_ENABLED_COL_ADD_FAIL, add_col_status, "Failed to add REC_VID_ENABLED columns to VIDFILES DB");
    }
#if 0 // LOGIC to update DB
    // temp fix for filename updation from old to new
    ret = update_table_old2new();
    if(ret == false) {
        LOG_C(TAG, "Failed to update_table_old2new");
        return false;
    }
    LOG_I(TAG, "success in update_table_old2new");
#endif

#if NDQ_DEPENDENCY
    while(1){
        if( is_msg_q_created(CIRC_BUFF_ctx->nd_central_q_name) == false ) {
            sleep(1);
            LOG_E(TAG, "waiting in nd_central_msg_q @@ is_msg_q_created");
            continue;
        }
        LOG_I(TAG, "Success in nd_central_msg_q @@ is_msg_q_created");
        break;
    }

    CIRC_BUFF_ctx->nd_central_msg_q = nd_msgq_t::get_msgq( CIRC_BUFF_ctx->nd_central_q_name, 
                                                            nd_msgq_t::ND_MSGQ_CLIENT);
    if( CIRC_BUFF_ctx->nd_central_msg_q == NULL )
    {
        // Notify health mon
        string str_msg = "CIRC_BUFF_ctx->nd_central_msg_q == NULL; Exiting from circ_buff";
        LOG_C(TAG, str_msg.c_str() );
        nd_service_obj->send_err_msg(SM_E_CB_MSG_QUEUE_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
        return false;
    }
#endif

    if( fill_circular_buffer_header() == false)
    {
        // Notify health mon
        string str_msg = "fill_circular_buffer_header failed ; Exiting from circ_buff";
        LOG_C(TAG, str_msg.c_str() );
        nd_service_obj->send_err_msg(SM_E_CB_FILL_CIRC_BUFFER_HDR_FAIL , NDService::UNUSED_ERR_AUX_CODE, str_msg ); 
        return false;
    } 
    read_payload_dump_config();
    if(nd_factory_utils::is_readonly_repair_supported()){
    LOG_I(TAG, "Reading root filesystem monitoring configuration...");
        if (read_root_filesystem_monitor_config()) {
            LOG_I(TAG, "Root filesystem monitoring configuration loaded successfully");
        } else {
            LOG_I(TAG, "Root filesystem monitoring is disabled or config failed to load");
        }
    }
        if (root_monitor_enabled) {
          root_filesystem_status_check_repair();  // Function handles all safety checks internally
          }
#if NDQ_DEPENDENCY
    ndc_get_circular_buffer_qname_msg_t q_name;
    q_name.msg_type = REQ_CIRCULAR_BUFFER_Q_NAME;
    q_name.length = sizeof( ndc_get_circular_buffer_qname_msg_t );
    const char* c_string = CIRC_BUFF_ctx->circular_buffer_q_name.c_str();
    strncpy( q_name.q_name, c_string, sizeof(q_name.q_name)-1 );
    LOG_I(TAG, "q_name.q_name %s sizeof(q_name.q_name) %d", q_name.q_name, sizeof(q_name.q_name));
    nd_msgq_t::nd_msg_t msg((char *)&q_name, sizeof(q_name), false); 
    CIRC_BUFF_ctx->nd_central_msg_q->send(msg, nd_msgq_t::ND_MSG_MED);
#endif
    //// end of post

    // post to circular buffer for cleanup
    // this will be executed only after parsing all the queed messages
    circular_buffer_generic_msg_t clean_db;
    clean_db.type = REQ_CIRCULAR_BUFFER_CLEAN_DB;
    clean_db.len = sizeof( circular_buffer_generic_msg_t );
    nd_msgq_t::nd_msg_t msg_clean((char *)&clean_db, sizeof(clean_db), false); 
    CIRC_BUFF_ctx->db_main_msg_q->send(msg_clean, nd_msgq_t::ND_MSG_NORM);
    
    if(init_config() == false)
    {
        LOG_E(TAG, "init_config() failed in init_config(). Default values set.");
    }
    string mount_sdcard;
    mount_sdcard = CIRC_BUFF_ctx->bagheera_config->getConfig("sdcard","mount",DEF_INI_MOUNT_SDCARD);
    MOUNT_SDCARD = ("true" == mount_sdcard);
    LOG_I(TAG, "MOUNT_SDCARD %d", MOUNT_SDCARD);
    if (false == MOUNT_SDCARD){ 
        if(sdcard_img_path != "") {
            if(file_is_present(sdcard_img_path) == true) {
                string mount_cmd = "mount " + sdcard_img_path + " " + old_path;
                LOG_W(TAG, "sdcard mount is false, thus using mount %s", mount_cmd.c_str());
                system_execute("mount_internal", mount_cmd.c_str());
            }
        }
    }

    CIRC_BUFF_ctx->buffer_mount_src = nd_device_obj->get_external_eMMC_dev_node();

    read_save_ext_camera_files_in_dhub_config(save_ext_cam_files_in_dhub);
    int max_alert_hq_file_config = MAX_ALERT_HQ_FILE;
    string max_alert_hq_file_str  = to_string(MAX_ALERT_HQ_FILE);
    if(!nd_config_parser.getParseStatus()){
	    max_alert_hq_file_str =  to_string(MAX_ALERT_HQ_FILE);
    }
    else {
        // Get max alert hq file count from nd_config
        max_alert_hq_file_str = nd_config_parser.getConfig("do_not_transcode","limit_alert_HD_count", to_string(MAX_ALERT_HQ_FILE));
        if(string_to_integer(max_alert_hq_file_str, max_alert_hq_file_config) == false) {
            max_alert_hq_file_config =  MAX_ALERT_HQ_FILE;
        }
        get_override_val = true;
        is_val_overridden = false;
        // Get override value if present
        max_alert_hq_file_str = nd_config_parser.getConfig("do_not_transcode","limit_alert_HD_count", to_string(MAX_ALERT_HQ_FILE), get_override_val, is_val_overridden);
    }
    string_to_integer(max_alert_hq_file_str, max_alert_hq_file);
    if((max_alert_hq_file < 0) || (max_alert_hq_file > max_alert_hq_file_config)) { // Valid range from 0 to nd_config value
        max_alert_hq_file = max_alert_hq_file_config;
    }
    LOG_I(TAG, " %s max_alert_hq_file: %d", max_alert_hq_file_str.c_str(), max_alert_hq_file);
    string drp_config_str  ="0";
    get_override_val = true;
    is_val_overridden = false;
    drp_config_str = CIRC_BUFF_ctx->bagheera_config->getConfig("drp","enabled",
                    drp_config_str, get_override_val, is_val_overridden);
    if(drp_config_str == "1" ) 
        drp_enabled = true;
    LOG_I(TAG, "drp_config_str: %s drp_enabled: %d", drp_config_str.c_str(), drp_enabled);
    drp_config_str  ="72";
    get_override_val = true;
    is_val_overridden = false;
    drp_config_str = CIRC_BUFF_ctx->bagheera_config->getConfig("drp","clock_hours",
                    drp_config_str, get_override_val, is_val_overridden);

    string_to_int64(drp_config_str, drp_clock_hours);
    CIRC_BUFF_ctx->msg_q_upl_to_cb =  nd_msgq_t::get_msgq( CIRC_BUFF_ctx->q_name_to_cb,
            nd_msgq_t::ND_MSGQ_SERVER);
    if( CIRC_BUFF_ctx->msg_q_upl_to_cb == NULL ) {
        string str_msg = "failed in msg_q_upl_to_cb ";
        LOG_C(TAG, str_msg.c_str() );
        nd_service_obj->send_err_msg(SM_E_CB_MSG_QUEUE_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
    }
    LOG_I(TAG, "Success in msg_q_upl_to_cb");

    nd::utils::drp_clock_hour_range_check(drp_clock_hours);
    LOG_I(TAG, "drp_config_str: %s , clock_hours: %lld",drp_config_str.c_str(),  drp_clock_hours);
    //use extended attributes configuration reading
    get_override_val = true;
    is_val_overridden = false;
    CIRC_BUFF_ctx->extended_attr_enabled = read_extended_attr_config();
    if (CIRC_BUFF_ctx->extended_attr_enabled) {
        LOG_I(TAG, "Extended attributes enabled");
    } else {
        LOG_I(TAG, "Extended attributes not enabled");
    }
    // Read config if want to use unlink
    string use_unlink_str  = DEFAULT_USE_UNLIKE_STR;
    get_override_val = true;
    is_val_overridden = false;
    use_unlink_str = CIRC_BUFF_ctx->bagheera_config->getConfig("sdcard","use_unlink",use_unlink_str, get_override_val, is_val_overridden);
    if( use_unlink_str == "true" ) {
        CIRC_BUFF_ctx->use_unlink = true;
        LOG_I(TAG, "use_unlink to delete file is true, val %d", CIRC_BUFF_ctx->use_unlink);
    }
    else {
        CIRC_BUFF_ctx->use_unlink = false;
        LOG_I(TAG, "use_unlink to delete file is false, val %d", CIRC_BUFF_ctx->use_unlink);
    }
     
    server.create_topic(TOPIC_OLDEST_UPLOADABLE_FILE);
    pthread_t cloud_notifier_thread, storage_monitor_thread, sdcard_recovery_thread, uploader_query_thread;
    pthread_t sysv_messageq_thread, sdcard_copy_file_and_discard_thread, send_storage_info_thread;
    pthread_create(&sdcard_recovery_thread, NULL, sdcard_recovery_thread_main, NULL);
    pthread_create(&send_storage_info_thread, NULL, send_storage_info_main, NULL);
    int null_row_cnt = check_and_delete_null_row_db();
    if(null_row_cnt > 0 ){
        LOG_E(TAG, "NULL value found in CB DB, total null_row_cnt: %d", null_row_cnt);
        nd_service_obj->send_err_msg(SM_E_CB_EMPTY_VALUE_IN_DB, null_row_cnt, "NULL value found in CB DB" );
    
    }
    // get size from SDCARD and calculate FILLING_LIMIT
    while( card_stats() == false )
    {
            LOG_C(TAG, "Failed in card_stats; Sleeping and Not Exiting from circ_buff");
            sleep(10);
    }
    deinit_config();
    if(FILLING_LIMIT <= get_total_backup_space())
    {
        string str_msg = "FILLING_LIMIT less than total backup space. FILLING_LIMIT: " + to_string(FILLING_LIMIT) + " Total backup space: " + to_string(get_total_backup_space()) ;
        LOG_C(TAG, str_msg.c_str() );
        nd_service_obj->send_err_msg(SM_E_CB_LESS_SPACE_FOR_FILLING, FILLING_LIMIT, str_msg );
        // Commenting this, as circular_buffer should always run for cleaning space.
        // return false;
    }
    /// end of space(FILLING_LIMIT) calculation

    // pthread_mutex_t init for current_time_mtx
    if(pthread_mutex_init(&CIRC_BUFF_ctx->current_time_mtx, NULL) != 0) {
        LOG_E(TAG, "Failed to init mutex for current_time");
    }
    prop_data_t rtc_entry;
    if(get_property_DB("rtcValidTime", &rtc_entry)) {
        string rtcValidTime_string = rtc_entry.value;
        int64_t savedRtcTime, currTime = get_system_time() ;
        string_to_int64(rtcValidTime_string, savedRtcTime);
        if(currTime < savedRtcTime || currTime > (savedRtcTime + month_in_seconds * 1000 ) ) {
            CIRC_BUFF_ctx->current_time =  0 ;
        }
        else
            CIRC_BUFF_ctx->current_time =  currTime ;
    }
    //// post circular_buffer_db name to nd_central
    pthread_create(&cloud_notifier_thread, NULL, cloud_notifier_main, NULL);
#if NEW_Q
    pthread_create(&sysv_messageq_thread, NULL, sysv_messageq_thread_main, NULL);
#endif
    pthread_create(&storage_monitor_thread, NULL, storage_monitor_main, NULL);

    pthread_create(&uploader_query_thread, NULL, uploader_query_main, NULL);
    if(file_is_present(circ_buff_reboot_token_file) == true) {
        LOG_E(TAG, "%s is already present. circular_buffer must have a crash and start", circ_buff_reboot_token_file.c_str());
        first_after_boot = false;
    }
    else {
        file_touch(circ_buff_reboot_token_file);
    }
    
    if(file_is_present(ND_UDID_SID) == true) {
        LOG_E(TAG, "%s is already present.", ND_UDID_SID.c_str());
        int64_t udid = -1, sid = -1;
        if(get_file_xattr(ND_UDID_SID, FileMetadataKey::udid, &udid, sizeof(udid))!= XATTR_OK){
            LOG_E(TAG, "Read fail. udid is: %lld", udid);
        }
        if(get_file_xattr(ND_UDID_SID, FileMetadataKey::sid, &sid, sizeof(sid))!= XATTR_OK){
            LOG_E(TAG, "Read fail. sid is: %lld", sid);
        }
        LOG_C(TAG, "%s attr :: udid: %lld, sid: %lld", ND_UDID_SID.c_str(), udid, sid);
    }
    else {
        file_touch(ND_UDID_SID);
        sync();
    }
    circular_buffer_msg_loop();
    // CB suppose to exit process incase it comes out of msg_loop
    // so not waiting for other threads to join
    //pthread_join(cloud_notifier_thread, NULL);
    pthread_join(uploader_query_thread, NULL);
    pthread_join(storage_monitor_thread, NULL);
    //return true;
    CIRC_BUFF_ctx->close_db(CIRC_BUFF_ctx->db_handle);

    LOG_C(TAG,"Done with circ_buff ; Exiting from circ_buff");
    
    nd_service_obj->release_service_obj();
    return 0;
}
