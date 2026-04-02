/* Copyright (C) 2016 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Y Suresh Kumar <suresh.kumar@netradyne.com>, November 2016
 */

#ifndef CIRCULAR_BUFFER_H
#define CIRCULAR_BUFFER_H

#include <sstream> 
#include <cstring>

#include <inttypes.h>

#include <sqlite3.h> 

#include <string>
#include <iostream>

#include <component.h>
#include <error.h>
#include<unordered_map>

#ifdef __cplusplus  
extern "C" { 
#endif 
#include <log.h>
#ifdef __cplusplus 
} 
#endif
#include <config_parser.h>
#include <nd_msgq.h>
#include <nd_msg_types.h>

//for file_delete function
#include <nd_file_utils.h>
#include <storage_utils.h>
#include <sdcard_utils.h>
#include <nd_time.h>
#include "service_utils.h"
//#include "nd_paths.h"
#include <uuid/uuid.h>
#include <queue>
#include "nd_factory.h"
#include <ndmb/nd_msg_interface.h>
#include <ndmb/nd_mbserver.h>
#include <condition_variable>
#include "nd_cb_utils.h"
#include <mutex>
#define FNAME_LEN     256  //Length for filename. Should match with nd_msg_types.h

using namespace std;
static const string log_dir = "/home/ubuntu/.nddevice/log/circ_buff"; 
static const int VIDLIST_CURL_TIMEOUT = 60L; /* MAX timeout */ // YSK
static const string circular_buffer_q_name = "q_circular_buffer";
static const string diagnostic_q_name = "DIAGNOSTIC";

//static const int ALERT_VID_MAX_BUFF = 50; // max alert videos that can be stored
//static const int START_VID_MAX_BUFF = 30; // max start videos that can be stored
//static const int STOP_VID_MAX_BUFF = 40; // max stop videos that can be stored

static const int NOTIFY_DURATION = 5; // notify to cloud for every 15 mins

#define ROAD_CAM_SIZEPERMIN 60
#define CAM123_SIZEPERMIN   30
#define DMS_CAM_SIZEPERMIN 60

// leave a backup place for 5 mins of recordings in bytes
static const int64_t FIVE_SESSION_BACKUP_SPACE = ((int64_t)5)*(DMS_CAM_SIZEPERMIN + ROAD_CAM_SIZEPERMIN+3*CAM123_SIZEPERMIN+1)*1024*1024 ;
//static const string VIDLIST_URL = "https://idms-staging.netradyne.com/restserver/api/v1/upload/videolist" ;
static const string VIDLIST_URL = "upload/videolist" ;


#define CB_DB_CORRUPTION_FILE "/home/ubuntu/.nddevice/cb_db_corruption"
#define DEVICE_CONFIG_INI "/home/ubuntu/config/deviceconfig.ini"
#define ND_DEVICE_INI "/home/ubuntu/.nddevice/nddevice.ini"
#define BAGHEERA_CONFIG_INI "/home/ubuntu/.nddevice/latest/bagheera_config.ini"
#define ND_CONFIG_ANALYTICS "/home/ubuntu/.nddevice/latest/nd_config.ini"
#define CLOUD_CONFIG_INI "/home/ubuntu/.nddevice/latest/cloudconfig.ini"
#define DEFAULT_VALUE_STRING_TO_INTEGER_FAILED -1

static const string CIRCULAR_BUFF_DBFILE_NAME = "circular_buffer.db";
void* db_main(void* args);

typedef struct json_add_del {
    int index_id;
    string file_name;
    int duration;
    circular_buffer_filetype_t file_type;
    circular_buffer_file_compression_t alert_type;  //CIRCULAR_BUFFER_NO_COMPRESSION -> for  ALERT's configured in [do_not_transcode] key in nd_config.ini
    int64_t udid;
    int64_t sessionCount;
}json_add_del_t;

typedef struct alldata_str {
    int index_id;
    string file_name;
}alldata_str_t;

typedef struct file_data_str {
    int index_id;
    int file_type;
    int64_t file_size;
    string file_name;
    int64_t sessionCount;
    int64_t udid;
    bool xattr_present; // This flag will be set if xattr is present on file, if not present the metadata present in DB will be written to xattr, this can happen for old files which were added to DB before enabling extended attribute
}file_data_str_t;

// This struct is used for storing file info from DB along with rec and upl privacy flags
typedef struct file_data_str_db {
    file_data_str_t file_data;
    int64_t time;
    int rec_vid_enabled;
    int upl_vid_enabled;
    circular_buffer_camtype_t camtype;
    circular_buffer_transcode_status_t tc_status;
    circular_buffer_filestatus_t status;
    circular_buffer_file_compression_t file_compression;
}file_data_str_db_t;

typedef struct file_info_str {
    int index_id;
    int file_type;
    int file_compression;
    int64_t file_size;
    string file_name;
}file_info_str_t;

typedef struct count_size_data {
    int32_t count;
    float size;
}count_size_data_t;

struct circular_buffer_valid_file_size_t{
    int min_file_size;
    int max_file_size;
};

class circular_buffer_vid_ext_t{
public:
    int64_t time;
    char base_file_name[FNAME_LEN];
    int duration;
    int file_size;
    circular_buffer_filetype_t file_type;
    circular_buffer_camtype_t camtype;
};
typedef struct circular_buffer_header
{
    string  session_id;
    string  device_id;
    string  key;
    string  device_version;
    string  devicetype;
    string  version;
    int  dafault_clip_durtn;
    bool    iscomplete;

}circular_buffer_header_t;

// Select query to get File_size, status, upl_vid_enabled, rec_vid_enabled column index
enum select_query_add_file_db_result{
    RES_ENTRY_COUNT_IND = 0, // RES = RESULT
    RES_FILE_SIZE_IND,
    RES_STATUS_IND,
    RES_UPL_VID_ENABLED_IND,
    RES_REC_VID_ENABLED_IND
};

// struct for storing privacy check query result
typedef struct circular_buffer_privacy_check_query_result {
    int row_count;
    int upl_vid_enabled;
    int rec_vid_enabled;
}circular_buffer_privacy_check_query_result_t;
//struct for storing number of video files in db
typedef struct num_video_file_data {
    circular_buffer_camtype_t cam_type; 
    circular_buffer_transcode_status_t tc_status;
    int64_t num_files;
} num_video_file_data_t;


class circular_buffer_file_t: public circular_buffer_vid_ext_t
{
public:
    int index_id;
    int64_t sessionCount;
    int64_t udid;
    circular_buffer_filestatus_t status;
    circular_buffer_transcode_status_t tc_status;
    circular_buffer_file_compression_t file_compression;
};

typedef sqlite3 db_handle_t;

typedef struct fsck_config {
    bool runFsckOnMountFail;
    bool runFsckOnResizeFail;
} fsck_config_t;

constexpr int BATCH_SIZE_FOR_DB_INSERTION = 20;
constexpr int BATCH_MODE_NONE = 0;
constexpr int BATCH_MODE_ADD = 1;
constexpr int BATCH_MODE_FLUSH = 2;

class circular_buffer_vid: public circular_buffer_vid_ext_t
{
public:

    //circular_buffer_file_t file_info;
    bool init_circular_buffer(circular_buffer_header_t header);
    circular_buffer_header_t header;

    // queue related variables
    // every component should know this name
    string          nd_central_q_name = "q_nd_central"; 
    string          power_mon_q_name = "q_power_monitor";
    string          circular_buffer_q_name = "q_circular_buffer";
    string          q_name_to_upl = "Q_TO_UPL";
    string          q_name_to_cb = "Q_TO_CB";
    nd_msgq_t       *nd_central_msg_q;
    nd_msgq_t       *db_main_msg_q;
    nd_msgq_t       *msg_q_cb_to_upl;
    nd_msgq_t       *msg_q_upl_to_cb;

    Config_parser   *device_config;
    Config_parser   *nd_config;
    Config_parser   *bagheera_config;
    Config_parser   *cloud_config;
    std::queue<char *> generic_queue;
    std::queue<char *> generic_queue_priority;

    static circular_buffer_vid* get_circular_buffer(string db_name, string table_format);
    bool delete_circular_buffer( circular_buffer_vid* circular_buffer );

    // DB handler
    db_handle_t* db_handle;
    // mutex lock for db_handle
    pthread_mutex_t db_handle_mutex;

    int64_t current_time;
    // mutex lock for current_time
    pthread_mutex_t current_time_mtx;

    //// db related functions
    bool open_db(string bd_file, db_handle_t** db_handle);
    bool create_table_db(db_handle_t* db_handle);
    bool create_table_db_details(db_handle_t* db_handle);
    bool exec_cmd_db(db_handle_t* db_handle, const string command, 
    int (*callback)(void*,int,char**,char**), void* cb_data, bool sql_busy_retry=true );
    void handle_db_corruption(db_handle_t* db_handle,int rc);

    bool close_db(db_handle_t* db_handle);
    string buffer_mount_src;
    fsck_config_t fsck_config;

    // Map to store valid file range for all file types getting added in DB
    // This range will be used to validate file size for a particular file type
    // If file size does not falls in this range a critical event will be raised
    // The size range is calculated by using bitrate and duration, then an error rate of +- 10% is considered
    std::unordered_map<std::string, circular_buffer_valid_file_size_t> camera_video_file_size_bytes = {
        {"0"    , {40500000, 49500000}}, // 45000000 +- 10%
        {"0_ld" , { 6750000,  8250000}}, //  7500000 +- 10%
        {"0_zip", {    1000,   250000}}, //    15000 to 30000 but seen 3000 and 99000 as well
        {"0_aac", {  300000,   600000}},
        {"0_jpeg",{    8000,    15000}},
        {"1"    , {13500000, 16500000}}, // 15000000 +- 10%
        {"1_ld" , { 3375000,  4125000}}, //  3750000 +- 10%
        {"1_zip", {    1000,   250000}}, //    15000 to 30000 but seen 3000 and 99000 as well
        {"1_aac", {  300000,   600000}},
        {"1_jpeg",{    8000,    15000}},
        {"2"    , { 3375000,  4125000}}, //  3750000 +- 10%
        {"3"    , { 3375000,  4125000}}, //  3750000 +- 10%
        {"4"    , {    1000,100000000}}, // As dicussed with EXT_CAM team max size can be 100MB
        {"5"    , {    1000,100000000}},
        {"6"    , {    1000,100000000}},
        {"7"    , {    1000,100000000}},
        {"8"    , {54000000, 66000000}}, // 60000000 +- 10%
        {"8_ld" , {  6750000,  8250000}} //  7500000 +- 10%
    };

    // For only updating the file size in db
    bool update_file_size_db(string filename, int64_t file_size);
    // Queue to process file size check already present in db
    queue<pair<string,int64_t>> file_size_check_queue;
    const int64_t MAX_FILE_SIZE_CHECK_QUEUE = 100;
    const int64_t MIN_PROCESS_FILE_SIZE_CHECK_QUEUE = 30; // Minimum number of entries to process the queue
    std::mutex mutex_process_file_size_check_queue;
    bool use_unlink = false; // use unlink to delete files instead of remove, this will be configurable parameter, by default false
    bool extended_attr_enabled=false;
private:

    circular_buffer_vid( string db, string table_format );
    ~circular_buffer_vid();
};

bool get_dir_details(const char* d, vector<file_data_str_t> & allfiles_dir);

//// sqlite finctions
static int callback_count_entry(void* data, int argc, char** argv, char** azColName);
bool get_db_details(vector<file_data_str_db_t> & allfiles_db);
bool delete_entry_from_db(int index_id, int file_type);
bool delete_entry_from_db(string filename, int file_type);
bool hard_delete_entry_from_db(string input_file_ld);
bool update_add_del_files_db(pair < vector< json_add_del >, vector< json_add_del > > add_del_files);
bool find_add_del_files(pair < vector< json_add_del >, vector< json_add_del > > &add_del_files,
                            int &oldest_video_index, string &db_identifier, int64_t &db_creation_time);
bool add_file_DB(circular_buffer_fileinfo_t fileinfo, int batch_mode = BATCH_MODE_NONE, circular_buffer_filestatus_t status = CIRCULAR_BUFFER_STATUS_NEW, circular_buffer_file_compression_t fc_type = CIRCULAR_BUFFER_MEDIUM_COMPRESSION);
bool insert_batch_file_DB(circular_buffer_fileinfo_t fileinfo, int batch_mode = BATCH_MODE_ADD, circular_buffer_filestatus_t status = CIRCULAR_BUFFER_STATUS_NEW, circular_buffer_file_compression_t fc_type = CIRCULAR_BUFFER_MEDIUM_COMPRESSION);
bool update_file_DB(circular_buffer_fileinfo_t fileinfo);
bool insert_file_DB(circular_buffer_fileinfo_t fileinfo);
bool update_type_DB(int64_t time, circular_buffer_filetype_t file_type);
bool add_file_normal(sqlite3 *db, char* base_file_name, int file_size, int file_deleted = 0);


bool get_details( vector<file_data_str_t>& all_files_dir,
                  vector<file_data_str_db_t>& all_files_db);
bool circular_buffer_cleanup(   vector<file_data_str_t>& all_files_dir,
                                vector<file_data_str_db_t>& all_files_db);
bool adjust_filling_limit(vector<file_data_str_db_t> all_files_db);
bool fill_circular_buffer_header();
bool delete_file_from_sdcard(string src_file);

int get_num_lq_vid_files(int32_t &front, int32_t &back, int32_t &ext, float& front_size, float& back_size, float& ext_size);
int get_num_hq_vid_files(int32_t &front, int32_t &back, int32_t &left, int32_t &right, int32_t &ext, float& front_size, float& back_size, float& left_size, float& right_size, float& ext_size);
int get_num_audio_files(int32_t &num_aud_files , float &size);
int get_Observation_files_details(int32_t& count, float& size);
int get_oldest_video_details(int64_t &ts, float &lat, float &lon, string &udid, string &fileName);
int get_available_video_storage_min(void);
int get_num_files_added_deleted(int &files_added, int& files_deleted);
int get_partial_files_info(int32_t &front, int32_t &back, int32_t &left, int32_t &right, float& front_size, float& back_size, float& left_size, float& right_size);

//callback function used by both circular_buffer thread and transcode thread
//function code in cirbuf_sqlrequests.cpp
int callback_complete_fileinfo(void *fileinfo, int argc, char **argv, char **azColName);
// functions added for transcode functionality

void* storage_monitor_main(void* args);



bool get_num_hq_videos_db(int64_t* num_hq_files_db, circular_buffer_camtype_t cam_type);  // return HQ video count
bool get_num_lq_videos_db(int64_t* num_lq_files_db, circular_buffer_camtype_t cam_type);  // return LQ video count
bool get_file_to_transcode(circular_buffer_file_t* fileinfo, circular_buffer_camtype_t cam_type);
bool get_oldest_dms_file(circular_buffer_file_t* fileinfo, bool hq_file);
bool update_name_type_status_DB(circular_buffer_file_t update_fileinfo);
bool update_tc_status_db_cleanup();

bool update_tc_status_DB(int indexid, circular_buffer_transcode_status_t status);
bool update_tc_status_DB(const char * file_name, circular_buffer_transcode_status_t status);
bool update_tc_status_filesize_DB(int indexid, int filesize, circular_buffer_transcode_status_t status);
bool sanitize_db_for_tc();
bool check_delete_status(int64_t indexid);
bool get_fileinfo_from_filename(circular_buffer_file_t* fileinfo, const char* base_file_name);
bool get_num_video_audio_files(vector<num_video_file_data_t>& video_file_data);
bool check_file_transcode_status(int64_t indexid, circular_buffer_transcode_status_t tc_status);
bool update_file_compression_DB(const char* base_file_name, circular_buffer_file_compression_t fc_type);
bool update_file_compression_DB(int, circular_buffer_file_compression_t fc_type);
bool check_file_compression_type(int64_t indexid, circular_buffer_file_compression_t fc_type);
bool delete_oldest_entry_with_filename(std::string filename);
struct circular_buffer_generic_msg_t {
    msg_type_t type;
    int len;
};


struct circular_buffer_header_msg_t
{
   GENERIC_MSG

   circular_buffer_header_t header;
};


// enum to string 

string cam_type_to_string (circular_buffer_camtype_t cam_type);
string transcode_status_to_string(circular_buffer_transcode_status_t tc_status);
string file_compression_to_string(circular_buffer_file_compression_t fc);
// function to get if upload is enabled by checking privacy
upl_to_cb_query_result_t check_video_upload_enabled_based_on_privacy(string filename);
constexpr int POS_TIMESTAMP_IN_FILENAME = 6; // Position of timestamp in filename if splitted on '_'
constexpr int POS_LATITUDE_IN_FILENAME = 3; // Position of UDID number in filename if splitted on '_'
constexpr int POS_LONGITUDE_IN_FILENAME = 4; // Position of UDID number in filename if splitted on '_'
constexpr int POS_UDID_IN_FILENAME = 1; // Position of UDID number in filename if splitted on '_'
constexpr int POS_CAMERA_ID_IN_FILENAME = 0; // Position of camera id in filename if splitted on '_'
// 0 - 1
// 1 - trip10ab
// 2 - part00021a
// 3 - 12.9825
// 4 - 77.7511
// 5 - 0.0
// 6 - 1749564901907
// 7 - y.mp4
constexpr int FIELDS_PRESENT_IN_FILENAME = 8; // Number of fields present in filename if splitted on '_'
void validate_filename(string &org_filename);
constexpr int MAX_SESSION_TO_DELETE_FOR_ADD_FILE_DB = 2; // Max number of session that can be deleted while adding a file in DB, if filling limit is reached
constexpr int MAX_NO_OF_FILE_TO_DELETE_AT_ONE_GO = 10; // Max number of files to be deleted at one time, to avoid long lock on DB
bool delete_whole_session_from_db_and_sdcard(string base_file_name); // Function to delte whole session from db and sdcard based on base filename
constexpr int BACKUP_SPACE_THRESHOLD_PERCENT = 60;
int64_t get_total_backup_space(); // Implement in krait.api / bagheera2.api cpp based on different backup space requirements

// Enum for min, max, count query types
typedef enum {
    QUERY_TYPE_MIN_INDEX = 0,
    QUERY_TYPE_MAX_INDEX,
    QUERY_TYPE_COUNT,
    QUERY_TYPE_CAM0_COUNT
}query_type_t;
//function to update the rec and upl privacy flags in extended attributes of the file
bool update_privacy_xattrs(string filename, int rec_vid_enabled, int upl_vid_enabled );
#endif
