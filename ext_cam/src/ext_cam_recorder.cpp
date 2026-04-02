/* Copyright (C) 2016 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Karthik Dumpala <karthik.dumpala@netradyne.com>, October 2018
*/

#include <algorithm>
#include <fstream>
#include <sys/time.h>
#include <sstream>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <nd_time.h>
#include <nd_file_utils.h>
#include <nd_msgq.h>
#include <nd_msg_utils.h>
#include <nd_msg_types.h>
#include <storage_utils.h>
#include <log.h>
#include "service_utils.h"
#include "ext_cam_recorder.h"
#include <sys/ioctl.h>
#include <sqlite3.h>
#include <vector>
#include <nd_ext_cam_utils.h>
#include <nd_auth_openssl.h>
#include "config_parser.h"
#include <system_utils.h>
#include <nd_db_utils.h>
#include <nd_factory.h>
#include <nd_cb_utils.h>
#include<cstdlib>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <utils.h>
#include "nd_app_timer.h"
#include <future>
#include <atomic>

using namespace std;

#define ROUTE_LOGS
#define TAG "EXT_CAM"

NDService *nd_service_obj; //nd service object, to detect crashes
ND_DeviceFactory *nd_device_obj = NULL;
extern int time_offset_msecs;   // Default -2secs offset
extern int cam_status_array[4]; // External camera status array
extern bool mdvrFirmwareVerCheckDone;   /* MDVR Firmware Version Check Done - To Avoid Multiple Time Validations */
static const string Q_SPEED = "SPEED";
static const string Q_POWER = "q_power_monitor";
static int msg_idx = 0;
static int driver_login_speed = 30;
static int driver_login_time  = 10;
static int auto_config_time  = 15;
extern int curr_speed;
extern bool report_config_mismatch_enabled;
static int auto_configure_speed = 30;
static int curr_session = 0;
static int curr_udid = 0;

static bool is_automation_enabled = false;
static bool report_mdvr_info_to_cloud = false;
static const string log_dir = "/home/ubuntu/.nddevice/log/ext_cam";
static const int MAX_RETRY_COUNT = 3;
static const int SLEEP_DURATION_DEFAULT = 5;
static const int SLEEP_DURATION_NO_ROWS = 10;
static const int SLEEP_DURATION_FILE_EMPTY = 30;
static const int SLEEP_DURATION_MDVR_SYNC = 5;
static const int HUNDRED_MS = 100000;
static const int MAX_TCP_COMM_RETRY_COUNT = 3; // Maximum Retry Count For TCP_COMM Error
static int time_zone = -800;   // Default for PST time
static int max_retry_file_empty = 3;   // Default 1.5 mins. 3*30 secs
static const string DB_PATH     = "/home/ubuntu/.nddevice/";
static int ext_cam_video_duration_request_ms = 60000;
static int max_retries_to_set_dhub_sta_mode = 5;
static int dhub_sta_timeout = 10800; //3 hours in seconds
extern bool channels_enabled_config[4];
bool mdvr_read_config = false;
bool DHUB_recovery_flag = true;
static const string Q_INSTALLER_APP = "installer_queue";
static bool upload_ext_cam_video = true;
static const int SLEEP_DURATION_DHUB_MAINTENCE = 200;
static const int SLEEP_DURATION_DHUB_CONN_CHECK = 3;
static const int DHUB_LAG_TIME_THRESHOLD_MS = 3000; // 3 seconds in milliseconds
static const string LATEST_VOD_TIME_FILE = "/home/ubuntu/.nddevice/ext_cam_latest_vod_time.txt";
static const int64_t THIRTY_ONE_DAYS_US = 31LL * 24 * 60 * 60 * 1000 * 1000; // 31 days in microseconds

static const string DBFILE_NAME = "ext_cam.db";
typedef sqlite3 db_handle_t;

static string CIRCULAR_BUFFER_PATH = "";
static const int copy_retry_time =5;

static bool video_encryption = true;

static bool save_ext_camera_files_in_dhub = false;
static bool mdvr_firmware_upgrade = false;
bool log_mdvr_rec_setup_config = false;
bool dhub_time_set = false;
bool rgb_analysis = false;
static bool report_black_videos[4];
static bool set_dhub_to_sta_mode = true;
static bool iosix_enabled = false;
static bool set_max_lpw_count_dhub = true;
static bool set_dhub_login_details = true;
bool dhub_auto_config = false;
string dhub_server_ip = "10.10.10.254"; //NOSONAR
#ifdef AUTOMATION
bool get_mstream_config = false;
static string dhub_connection_file = "/dev/shm/dhub_connected.txt";
#endif
bool set_dhub_ap_mode = false;
bool check_dhub_disk_status = true;
bool report_dhub_disk_status = false;

static int mdvr_firmware_upgrade_speed = 30;

static const int64_t MIN_FREE_SPACE = 256 * 1024 * 1024; //256 MB of minimum free space for ext_cam to run
static const int DELAY_MAIN_EXIT = 2 * 60;
int64_t pair_time;

static const int OPERATE_DELTA_SIZE = 80; // File size increase amount when operated
//variable to keep track of number of rows updated. this is used in the db
//update hook
int num_rows_updated = 0;
pthread_mutex_t ext_cam_db_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t ext_cam_config_db_mutex = PTHREAD_MUTEX_INITIALIZER;
int MAX_DB_LIMIT = 5000;
int MAX_NUM_THREADS = 1;
// DB handler
db_handle_t* db_handle;
string insert_str = "INSERT INTO EXTCAM (CAM_NO,FILENAME,STARTTIME,ENDTIME,FRAMERATE,CH_NO,STATE,AUDIO,UDID,SESSIONCOUNT,PULL,VOD_ID,PRIORITY,REQUEST_TIME,VOD_STATUS,ACK_TIME,IS_CANCELLED,FAILURE_REASON,RGB_STATUS) VALUES ";
pthread_mutex_t file_dld_mutex = PTHREAD_MUTEX_INITIALIZER;
bool* file_dld_thread_active;  
typedef struct {
    int db_index_id;
    int thread_index_id;
    ext_request_details req;
}thread_param_t;

//IB and CB Queue names
static const string Q_CIRC_BUFF = "q_circular_buffer";
static const string Q_WIFI_MGR = "WIFI_MGR";
static const string Q_UPL = "UniUpload";
static const string Q_NDCENTRAL = "q_nd_central";

//Server Queue Name
static const string Q_NAME = "EXT_CAM";
static bool mdvr_time_set = false;
static const string DHUB_WIFI_MODE_CLIENTS[] = {Q_WIFI_MGR, Q_UPL, Q_POWER};
static const int NUM_DHUB_WIFI_MODE_CLIENTS = sizeof (DHUB_WIFI_MODE_CLIENTS)/sizeof (string);

static const string mdvrRebootCommandFile = "/dev/shm/mdvrReboot";
static bool db_integrity_check =false;
bool mdvr_firm_upgrade_speed_achieved = false;
bool auto_config_speed_achieved = false;
static const int DELAY_NO_FILES_FOLDER = 30;
int DHUB_HW_VER = 0;
string hotspot_ssid;
string hotspot_password;
static const string corrupted_ap_ssid = "ssx123456";
static const string corrupted_ap_password = "88888888";

extern string mdvr_conf_ssid;
extern string bag_conf_ssid;
extern bool auto_config_ssid_sent;
extern bool no_of_cams_enabled[4];
int ext_cam_frame_rate[4];
extern bool ext_cam_audio_enabled[4];

extern int total_partial_files;
extern float total_partial_files_size;
extern std::atomic<int> firmware_upgrade_status;

#define MAX_TEXT_LENGTH 100
class DHubInfo {
public:
    DHubInfo(std::string serialNumber = "NA", std::string mode = "NA", std::string ip = "NA")
        : serialNumber(serialNumber), mode(mode), ip(ip) {
            setSerialNumber();
            setMode();
            setIp();
            setFwVersion();
        }

    void setSerialNumber() {
        if(getDHUBSerialNo(this->serialNumber) == false) {
            this->serialNumber = "NA";
        }
    }

    void setFwVersion()
    {
        if(getFirmwareVersion(this->fwVersion) == false)
        {
            this->fwVersion = "NA";
        }
    }

    void setMode() {
        wifi_mode_t mode_enum = getCurrDHUBWifiModeFromDB();
        if(mode_enum == eAPMode) {
            mode = "AP";
        } else if(mode_enum == eSTAMode) {
            mode = "STA";
        }
        else {
            mode = "NA";
        }
        this->mode = mode;
    }

    void setIp() {
        if(get_dhub_ip(this->ip) == false) { //get dhub ip returns true and gives DHUB ip only if we are connected to it
            this->ip = "NA";
        }
        if(this->mode == "AP") { //since this info is going to be sent to installer_app, if DHUB mode is in AP hardcording ip to 10.10.10.254
            this->ip = dhub_server_ip;
        }
    }

    void print() const {
        LOG_I(TAG, "Print into File");
        nlohmann::json j;
        j["serialNumber"] = serialNumber;
        j["mode"] = mode;
        j["ip"] = ip;
        j["fwVersion"] = fwVersion;
        std::ofstream file("/dev/shm/dhubInfo.json");
        if (file.is_open()) {
            file << j.dump(4); // Pretty print with 4 spaces
            file.close();
        }
    }

public:
    std::string serialNumber;
    std::string mode;
    std::string ip;
    std::string fwVersion;
};

struct compare : public std::binary_function<ext_request_details, ext_request_details, bool>
{
    bool operator()(const ext_request_details a, const ext_request_details b) const
    {
        //If priority is not same then higher priority request is at the top of the priority queue, if priority is same then check for request time
        if(a.priority == b.priority)
        {   
            /* If the priority and request time is the same for both requests then the order in which the request is added doesnt matter 
               In this case it returns false  which means then the request is added to the priority queue in the order it is received*/
            if(a.priority != LOWEST_P)
            {
                return (a.request_time > b.request_time);  // If the priority is not the lowest priority then the request with the oldest request time is at the top of the priority queue
            }
            else
            {
                return (a.request_time < b.request_time); // If the priority is the lowest priority then the request with the newest request time is at the top of the priority queue
            }
        }
        return (a.priority > b.priority);
    }
};

priority_queue<ext_request_details, vector<ext_request_details>, compare> pq_ext_req;
unordered_map<string, uint64_t> ext_req_cancel_map;

recursive_mutex pq_ext_req_mutex;

static string get_msgq_name() {
    return Q_NAME;
}


static string get_circ_buff_q() {
    return Q_CIRC_BUFF;
};

//Server Queue pointer
static nd_msgq_t *server_q;

static bool init_msgq() {

    //Create message queue
    server_q = nd_msgq_t::get_msgq( get_msgq_name(), nd_msgq_t::ND_MSGQ_SERVER );

    if( server_q == NULL ) {
        LOG_E(TAG, "Cannot create message queue");
        return false;
    }

    LOG_I(TAG, "Message queue created");
    return true;
}

static int get_delta_size(string file_name)
{
	if( ( true == video_encryption  ) &&
	    ( file_name.find("mp4") != file_name.npos) )
	{
		return OPERATE_DELTA_SIZE;
	}
	return 0;
}

bool send_file_size_to_bagheera(string file_name) {
    // Get the full file path
    string fullfile_name = nd_device_obj->get_external_eMMC_mount_path() + file_name;

    // Get the file size
    int64_t file_size = get_file_size(fullfile_name);
    if (file_size == -1) {
        LOG_E(TAG, "Unable to find the file %s/GetFileSize returned -1", file_name.c_str());
        return false;
    }

    LOG_I(TAG, "Sending file size to Bagheera, file: %s, size: %lld", file_name.c_str(), file_size);
   
    file_info_msg_t file_info_bag;
    file_info_bag.file_size = file_size;
    //file_info_bag.file_name = fullfile_name;
    nd_strncpy(file_info_bag.file_name, file_name.c_str(), sizeof(file_info_bag.file_name));

    // Send the message to Bagheera service
    if (!send_msg((generic_msg_t *)&file_info_bag, REQ_BAGHEERA_ADD_FILE,
                sizeof(file_info_msg_t), get_msgq_name(), Q_NDCENTRAL, 0)) {
        LOG_E(TAG, "Failed to send message to Bagheera service for file: %s", file_name.c_str());
        return false;
    }

    LOG_I(TAG, "Successfully sent file size to Bagheera for file: %s", file_name.c_str());
    return true;
}

#if 0
bool ext_cam_post_circular_buffer_for_save_ext_camera_files_in_dhub(string file_name, int file_type, int cam_type, int64_t udid, int64_t sessionCount){

    circular_buffer_add_file_db_msg_t addfile_msg;

    addfile_msg.file_info.time           = 0;
    strcpy(addfile_msg.file_info.base_file_name, file_name.c_str());

#ifdef KRAIT
	string fullfile_name = CIRCULAR_BUFFER_PATH + "/" + addfile_msg.file_info.base_file_name;
#else
	string fullfile_name = nd_device_obj->get_external_eMMC_mount_path() + "/" + file_name;
#endif
    addfile_msg.file_info.file_size       = get_file_size(fullfile_name);
    addfile_msg.tc_status                 = CIRCULAR_BUFFER_TC_STATUS_IGNORE;
    LOG_I(TAG, "post_circular_buffer_add_file_db, file: %s, GetFileSize: %d",
                            file_name.c_str(), addfile_msg.file_info.file_size );

    addfile_msg.file_info.file_type      = (circular_buffer_filetype_t)file_type;
    addfile_msg.file_info.camtype        = (circular_buffer_camtype_t)cam_type;
    addfile_msg.file_info.duration       = 60000;
    addfile_msg.file_info.tc_status      = CIRCULAR_BUFFER_TC_STATUS_TRANSCODED;
    addfile_msg.file_info.udid       = udid ;
    addfile_msg.file_info.sessionCount       = sessionCount;
    addfile_msg.file_info.upl_vid_enabled = upload_ext_cam_video;
    LOG_I(TAG, "udid: %lld  sessionCount: %lld file_type: %d cam_type: %d ", addfile_msg.file_info.udid, addfile_msg.file_info.sessionCount, file_type, cam_type);

    //send msg to CB
    send_msg((generic_msg_t *)&addfile_msg, REQ_CIRCULAR_BUFFER_ADD_FILE_DB_FOR_SAVE_EXT_CAMERA_FILES_IN_DHUB,
            sizeof( circular_buffer_add_file_db_msg_t ), get_msgq_name(), get_circ_buff_q(), 0);

    return true;
}

bool ext_cam_post_circular_buffer(string file_name, int file_type, int cam_type, int64_t udid, int64_t sessionCount) {

    circular_buffer_add_file_db_msg_t addfile_msg;

    addfile_msg.file_info.time           = 0;
    strcpy(addfile_msg.file_info.base_file_name, file_name.c_str());

#ifdef KRAIT
	string fullfile_name = CIRCULAR_BUFFER_PATH + "/" + file_name;
#else
	string fullfile_name = nd_device_obj->get_external_eMMC_mount_path() + file_name;
#endif
    addfile_msg.file_info.file_size       = get_file_size(fullfile_name);
    if( addfile_msg.file_info.file_size == -1 ){
        LOG_C(TAG, "unable to find the file %s/GetFileSize returned -1", file_name.c_str());
        return false;
    }
    LOG_I(TAG, "post_circular_buffer_add_file_db, file: %s, GetFileSize: %d", 
                            file_name.c_str(), addfile_msg.file_info.file_size );
    addfile_msg.file_info.file_type      = (circular_buffer_filetype_t)file_type;
    addfile_msg.file_info.camtype        = (circular_buffer_camtype_t)cam_type;
    addfile_msg.file_info.duration       = 60000;
    addfile_msg.file_info.tc_status      = CIRCULAR_BUFFER_TC_STATUS_TRANSCODED;
    addfile_msg.file_info.udid       = udid ;
    addfile_msg.file_info.sessionCount       = sessionCount;
    addfile_msg.file_info.upl_vid_enabled = upload_ext_cam_video;
    LOG_I(TAG, "udid: %lld  sessionCount: %lld file_type: %d cam_type: %d ", addfile_msg.file_info.udid, addfile_msg.file_info.sessionCount, file_type, cam_type);

    //send msg to CB

    send_msg((generic_msg_t *)&addfile_msg, REQ_CIRCULAR_BUFFER_ADD_FILE_DB,
                sizeof( circular_buffer_add_file_db_msg_t ), get_msgq_name(), get_circ_buff_q(), 0);

    return true;
}
#endif

void copy_and_post_circular_buffer(string mp4_filename, int cam_num, int64_t udid, int64_t sessionCount) {
    string folder_name, fname;
    
    if(mp4_filename == "") {
        LOG_E(TAG, "filename given for convert empty");
        return;
    }

    if(!get_folder_file_names(mp4_filename, folder_name, fname)) {
        LOG_E(TAG, "Failed to get folder and file names from given path");
        return;
    }

#ifdef KRAIT
	string final_video_full_name = CIRCULAR_BUFFER_PATH + "/" + fname;
#else
	string final_video_full_name = nd_device_obj->get_external_eMMC_mount_path() + fname;
#endif

   bool copy_status = true;
   int copy_retry = 1;

   do {
	   if( false == copy_status)
	   {
		   LOG_E(TAG, "copying to destination failed try one more time after 5 sec");
		   file_delete(final_video_full_name);
		   sleep(copy_retry_time);
		   copy_retry--;
	   }

	   if( video_encryption )
	   {
		   copy_status = operate_if_video_and_copy(mp4_filename.c_str(), final_video_full_name.c_str());
	   }
	   else
	   {
		   copy_status = file_copy(mp4_filename.c_str(), final_video_full_name.c_str());
	   }
	   if(true == copy_status)
		   break;

   }while(copy_retry > 0);


   if(false == copy_status )
   {
	   LOG_E(TAG, " failed to copy in retry as well: %s to %s; ", 
				   mp4_filename.c_str(), final_video_full_name.c_str());

	   //delete the source file previously rename was used so exclusive delete was not needed
	   file_delete(mp4_filename);
	   return;
   }

    LOG_I(TAG, "moving file IB from %s to %s successful and deleting %s file", mp4_filename.c_str(), final_video_full_name.c_str(), mp4_filename.c_str());

    int32_t file_size_mp4   = get_file_size(mp4_filename);
    int32_t file_size_final = get_file_size(final_video_full_name);
    int32_t operate_delta_size = get_delta_size(mp4_filename); 	
    if ( file_size_mp4 <= 0) {
        LOG_E (TAG, "GetFileSize for %s file returned < 0 ", mp4_filename.c_str());
	file_delete(mp4_filename);
        return;
    }

    if (file_size_final <= 0) {
        LOG_E (TAG, "GetFileSize for %s file returned < 0 ", final_video_full_name.c_str());
	file_delete(mp4_filename);
        return;
    }

   if( file_size_final < ( file_size_mp4 + operate_delta_size ) )
   {
	   file_delete(final_video_full_name);
	   sleep(copy_retry_time);

	   if( video_encryption )
	   {
		   copy_status = operate_if_video_and_copy(mp4_filename.c_str(), final_video_full_name.c_str());
	   }
	   else
	   {
		   copy_status = file_copy(mp4_filename.c_str(), final_video_full_name.c_str());
	   }
	   if ( false == copy_status )
	   {
		   LOG_E(TAG, " failed to copy file second time: %s to %s; ignoring this request", 
				   mp4_filename.c_str(), final_video_full_name.c_str());
		   file_delete(mp4_filename);
		   return;
	   }
   }

    if(file_fd_sync(final_video_full_name) == false) {
        LOG_E(TAG, "failed to sync file after move");
    }

    bool res = send_file_size_to_bagheera(fname);
    if(res == false)
    {
        LOG_E(TAG,"Failed to send size to NDCentral");
    }
#if 0
    bool res = ext_cam_post_circular_buffer(fname, CIRCULAR_BUFFER_TYPE_NORMAL, cam_num, udid, sessionCount);
    if(res == false) {
        LOG_E(TAG, "post_circular_buffer for %s failed", fname);
    }
#endif
    //delete the source file previously rename was used so exclusive delete was not needed
    file_delete(mp4_filename);
}

bool exec_cmd_db(const string command,
        int (*callback)(void*,int,char**,char**), void* cb_data, char** errmsgs ) {
    int rc;
    pthread_mutex_lock(&ext_cam_db_mutex);
    rc = sqlite3_exec(db_handle, command.c_str(), callback, cb_data, errmsgs);
    pthread_mutex_unlock(&ext_cam_db_mutex);
    if( rc != SQLITE_OK ){
      LOG_E(TAG, "SQL error: %s", *errmsgs);
      sqlite3_free(*errmsgs);
      return false;
    }
      
    LOG_D(TAG, "Success in exec_cmd_db");
    return true;
}
bool exec_cmd_extcam_config_db(const string command,
        int (*callback)(void*,int,char**,char**), void* cb_data, char** errmsgs,db_handle_t* ext_cam_config_db_handle ) {
    int rc;
    pthread_mutex_lock(&ext_cam_config_db_mutex);
    rc = sqlite3_exec(ext_cam_config_db_handle, command.c_str(), callback, cb_data, errmsgs);
    pthread_mutex_unlock(&ext_cam_config_db_mutex);
    if( rc != SQLITE_OK ){
      LOG_E(TAG, "SQL error: %s", *errmsgs);
      sqlite3_free(*errmsgs);
      return false;
    }
      
    LOG_D(TAG, "Success in exec_cmd_extcam_config_db");
    return true;
}

bool open_db(string db_file, db_handle_t** db_handle) {
   int rc;

   rc = sqlite3_open(db_file.c_str(), db_handle);
   if( rc ) {
      LOG_E(TAG, "Can't open database: %s", sqlite3_errmsg(*db_handle));
      return false;
   }

   rc = sqlite3_busy_timeout((*db_handle), SQLITE3_BUSY_TIMEOUT); //setting timeout
   if (rc) {
       LOG_E(TAG, "Unable to set timeout for db. Timeout is set to default value = 0");
   } else {
       LOG_I(TAG, "Set db timeout to %dms", SQLITE3_BUSY_TIMEOUT);
   }

   return true;
}

bool close_db(db_handle_t* db_handle) {
    if(db_handle == NULL) {
        return false;
    }

    sqlite3_close(db_handle);
    LOG_I(TAG, "Closing DB");
    return true;
}

bool create_table_db(db_handle_t* db_handle) {
    char *zErrMsg = 0;
    int  rc;
    string sql = "CREATE TABLE EXTCAM(" \
                 "INDEXID INTEGER PRIMARY KEY  AUTOINCREMENT," \
                 "CAM_NO           INT     NOT NULL," \
                 "FILENAME         TEXT    NOT NULL," \
                 "STARTTIME        INT     NOT NULL," \
                 "ENDTIME          INT     NOT NULL," \
                 "FRAMERATE        INT     NOT NULL," \
                 "CH_NO            INT     NOT NULL," \
                 "STATE            TEXT    NOT NULL," \
                 "AUDIO            INT     NOT NULL," \
                 "UDID             INT     NOT NULL," \
                 "SESSIONCOUNT     INT     NOT NULL," \
                 "PULL             INT     NOT NULL);" ;

    pthread_mutex_lock(&ext_cam_db_mutex);
    rc = sqlite3_exec(db_handle, sql.c_str(), NULL, 0, &zErrMsg);
    pthread_mutex_unlock(&ext_cam_db_mutex);
    //// parse the error message
    if( rc != SQLITE_OK ) {
        if(strstr(zErrMsg, "already exists") != NULL) {
            LOG_I(TAG, "SQL error@ %s", zErrMsg);
            LOG_I(TAG, "Ignoring error since table already exists");
            sqlite3_free(zErrMsg);
            return true;
        }
        LOG_E(TAG, "SQL error@ %s", zErrMsg);
        sqlite3_free(zErrMsg);
        return false;
    }

    return true;
}

bool get_index_id_from_db(int &index_id) {
    bool ret = false;
    int rc;
    sqlite3_stmt *stmt;
    pthread_mutex_lock(&ext_cam_db_mutex);
    rc = sqlite3_prepare_v2(db_handle, "SELECT * from EXTCAM WHERE STATE IN ('INIT') limit 1 OFFSET (SELECT COUNT(*) FROM EXTCAM WHERE STATE IN ('INIT'))-1", -1, &stmt, NULL);

    do {
        LOG_D(TAG, "Checking for rows in db");
        if (rc != SQLITE_OK) {
            LOG_E(TAG,"Failed to prepare statement: %s",sqlite3_errmsg(db_handle));
            break;
        }
        int rc = sqlite3_step(stmt);
        if (rc != SQLITE_ROW) {
            LOG_I(TAG, "Found no rows in db");
            break;
        }
        else if(rc == SQLITE_ERROR){
            LOG_E(TAG, "Failed to read data from db: %s", sqlite3_errmsg(db_handle));
        }

        int num_cols = sqlite3_column_count(stmt);

        if(num_cols == 0) {
            LOG_E(TAG, "num cols 0 !!!!");
            break;
        }

        index_id = sqlite3_column_int(stmt, 0);
        ret = true;
    } while (false);

    if(stmt)
    {
        sqlite3_finalize(stmt);
    }
    pthread_mutex_unlock(&ext_cam_db_mutex);
    return ret;
}

void get_row_count_from_db(int &row_count) {
    sqlite3_stmt *stmt;
    int ret;
    pthread_mutex_lock(&ext_cam_db_mutex);
    ret = sqlite3_prepare_v2(db_handle, "SELECT COUNT(*) from EXTCAM", -1, &stmt, NULL);

    LOG_I(TAG, "Trying to get row count from db");
    do {
        if (ret != SQLITE_OK) {
            LOG_E(TAG,"Failed to prepare statement: %s",sqlite3_errmsg(db_handle));
            break;
        }
        int rc = sqlite3_step(stmt);
        if (rc != SQLITE_ROW) {
            LOG_E(TAG, "Failed to get row count from db: %s", sqlite3_errmsg(db_handle));
            break;
        }

        row_count = sqlite3_column_int(stmt, 0);
        LOG_I(TAG, "Number of rows in db: %d", row_count);
    } while(false);

    if(stmt)
    {
        sqlite3_finalize(stmt);
    }
    pthread_mutex_unlock(&ext_cam_db_mutex);
}

bool delete_all_ext_vods_from_db() 
{
    char *zErrMsg = 0;
    stringstream del_stmt;
    del_stmt << "DELETE FROM EXTCAM WHERE NOT LIKE '%bagheera%'";
    string delete_stmt = del_stmt.str();
    if(exec_cmd_db(delete_stmt, NULL, 0, &zErrMsg) == true) {
        LOG_D(TAG, "Deleted All VOD Request From DB");
        return true;
    }
    return false;
}

// Add this function to read the latest VOD time from file
int64_t read_latest_vod_time_from_file() {
    ifstream file(LATEST_VOD_TIME_FILE);
    if (!file.is_open()) {
        LOG_I(TAG, "Latest VOD time file doesn't exist, returning 0");
        return 0;
    }
    
    int64_t latest_time = 0;
    file >> latest_time;
    file.close();
    
    LOG_I(TAG, "Read latest VOD time from file: %lld", latest_time);
    return latest_time;
}

// Add this function to write the latest VOD time to file
bool write_latest_vod_time_to_file(int64_t latest_time) {
    ofstream file(LATEST_VOD_TIME_FILE);
    if (!file.is_open()) {
        LOG_E(TAG, "Failed to open latest VOD time file for writing");
        return false;
    }
    
    file << latest_time;
    file.close();
    
    LOG_I(TAG, "Written latest VOD time to file: %lld", latest_time);
    return true;
}

// Add this function to get the latest VOD request time from database
int64_t get_latest_vod_request_time_from_db() {
    sqlite3_stmt *stmt;
    int rc;
    int64_t latest_time = 0;
    
    pthread_mutex_lock(&ext_cam_db_mutex);
    
    const char* sql = "SELECT MAX(REQUEST_TIME) FROM EXTCAM WHERE VOD_ID NOT LIKE 'bagheera%' AND VOD_ID != ''";
    rc = sqlite3_prepare_v2(db_handle, sql, -1, &stmt, NULL);
    
    if (rc != SQLITE_OK) {
        LOG_E(TAG, "Failed to prepare statement for latest VOD time: %s", sqlite3_errmsg(db_handle));
        pthread_mutex_unlock(&ext_cam_db_mutex);
        return 0;
    }
    
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        latest_time = sqlite3_column_int64(stmt, 0);
        LOG_I(TAG, "Latest VOD request time from DB: %lld", latest_time);
    } else {
        LOG_I(TAG, "No VOD requests found in database");
    }
    
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&ext_cam_db_mutex);
    
    return latest_time;
}

// Add this function to delete old VODs
bool delete_old_vods(int64_t cutoff_time) {
    char *zErrMsg = 0;
    stringstream del_stmt;
    
    del_stmt << "DELETE FROM EXTCAM WHERE VOD_ID NOT LIKE 'bagheera%' AND VOD_ID != '' AND REQUEST_TIME < " << cutoff_time;
    string delete_stmt = del_stmt.str();
    
    LOG_I(TAG, "Deleting old VODs with query: %s", delete_stmt.c_str());
    
    if (exec_cmd_db(delete_stmt, NULL, 0, &zErrMsg) == true) {
        int changes = sqlite3_changes(db_handle);
        LOG_I(TAG, "Successfully deleted %d old VOD requests from DB", changes);
        return true;
    } else {
        LOG_E(TAG, "Failed to delete old VOD requests from DB");
        return false;
    }
}

// Add this function to perform the cleanup at boot
bool cleanup_old_vods_at_boot() {
    LOG_I(TAG, "Starting VOD cleanup at boot");
    
    // Get the latest VOD time from file (from previous boot)
    int64_t stored_latest_time = read_latest_vod_time_from_file();
    
    // Get the current latest VOD time from database
    int64_t current_latest_time = get_latest_vod_request_time_from_db();
    
    // Use the maximum of the two as our reference point
    int64_t reference_time = max(stored_latest_time, current_latest_time);
    
    if (reference_time == 0) {
        LOG_I(TAG, "No VOD requests found, skipping cleanup");
        return true;
    }
    
    // Calculate cutoff time (31 days before the reference time)
    int64_t cutoff_time = reference_time - THIRTY_ONE_DAYS_US;
    
    LOG_I(TAG, "VOD cleanup: reference_time=%lld, cutoff_time=%lld", reference_time, cutoff_time);
    
    // Delete VODs older than the cutoff time
    bool cleanup_result = delete_old_vods(cutoff_time);
    
    // Update the stored latest time with current value
    write_latest_vod_time_to_file(current_latest_time);
    
    return cleanup_result;
}

// Helper function to check if this is a valid VOD request (not bagheera)
bool is_valid_vod_request(const ext_request_details& req) {
    return (req.vod_id.find("bagheera") == string::npos && !req.vod_id.empty());
}

/*static bool register_for_speed_with_speed_for_auto_configuration(const bool regSpeed)
{

    if(regSpeed)
    {
        req_speed_reg_msg_t req;
        req.reg_type = SPEED_REG_AUTO_CONFIG;
        req.speed = auto_configure_speed;
        req.contig_secs = auto_config_time;

        if( false == send_msg( (generic_msg_t *)&req, REQ_SPEED_REG, sizeof(req), get_msgq_name(), Q_SPEED, msg_idx++ ) ) {
            LOG_E(TAG, "Not Able To Register With Speed Service");
            return false;
        }
    }
    else
    {
        req_speed_unreg_msg_t req;

        if( false == send_msg( (generic_msg_t *)&req, REQ_SPEED_UNREG, sizeof(req), get_msgq_name(), Q_SPEED, msg_idx++ ) ) {
            LOG_E(TAG, "Not Able To Un-Register With Speed Service");
            return false;
        }
    }
    return true;
}*/

static bool register_for_speed_with_speed(const bool regSpeed)
{

    if(regSpeed)
    {
        req_speed_reg_msg_t req;
        req.reg_type = SPEED_REG_DRV_LOGIN;
        req.speed = 0;
        req.contig_secs = auto_config_time;

        if( false == send_msg( (generic_msg_t *)&req, REQ_SPEED_REG, sizeof(req), get_msgq_name(), Q_SPEED, msg_idx++ ) ) {
            LOG_E(TAG, "Not Able To Register With Speed Service");
            return false;
        }
    }
    else
    {
        req_speed_unreg_msg_t req;

        if( false == send_msg( (generic_msg_t *)&req, REQ_SPEED_UNREG, sizeof(req), get_msgq_name(), Q_SPEED, msg_idx++ ) ) {
            LOG_E(TAG, "Not Able To Un-Register With Speed Service");
            return false;
        }
    }
    return true;
}

static bool check_for_service(string Q_NAME)
{
    int serviceAvail = true;
    int timeout = 0;
    LOG_I(TAG, "Checking For %s Service",Q_NAME.c_str());
    while( ! is_msg_q_created(Q_NAME) )
    {
        sleep(1);
        if(timeout == 10)
        {
            serviceAvail = false;
            break;
        }
        timeout++;
    }

    if(serviceAvail)
        LOG_I(TAG, "%s Client Message Q Created",Q_NAME.c_str());
    else
        LOG_E(TAG, "%s Client Message Q Not Created",Q_NAME.c_str());

    return serviceAvail;
}

bool req_ignition_status()
{
    generic_msg_t req_igni_status;
    if (!send_msg (&req_igni_status, GET_POWERMON_IGNITION_STATUS, sizeof (req_igni_status), get_msgq_name(), Q_POWER, msg_idx++)) {
        LOG_E (TAG,"Failed to Send Ignition Status Request Message");
        return false;
    }
        return true;
}

void syncTimeToDriveriHub(const bool& write_config)
{
    set_time_zone(time_zone);
    sleep(SLEEP_DURATION_DEFAULT);

    int lag_time = get_lag_val();
    if(abs(lag_time) < DHUB_LAG_TIME_THRESHOLD_MS && !write_config)
    {
        LOG_I(TAG, "No Config Change And DHUB Lag Time less Than %d Seconds not setting time or config", DHUB_LAG_TIME_THRESHOLD_MS / 1000);
        return;
    }

    LOG_I(TAG,"Time Setting in Progress , Lag Time: %d", lag_time); 
    string time_sync_msg = "Time Setting In Progress, Lag Time: " + to_string(lag_time);
    nd_service_obj->send_err_msg(SM_E_EXT_CAM_TIME_SET, curr_speed, time_sync_msg);
    mdvr_time_set = set_mdvr_time(write_config);
    if(mdvr_time_set == false) {
        LOG_E(TAG, "failed to set mdvr time");
        nd_service_obj->send_err_msg(SM_E_EXT_CAM_TIME_SET_FAIL, curr_speed, "Time Setting Failed");
    }
    else
    {
        nd_service_obj->send_err_msg(SM_E_EXT_CAM_TIME_SET_FAIL, curr_speed, "Time Setting Successful");
        LOG_I(TAG, "successfully set mdvr time");
    }
}

void* extcam_auto_configure_and_speed_mon_thread(void *ptr)
{
    //register_for_speed_with_speed_for_auto_configuration(true);
    LOG_I(TAG,"Auto Configuration Thread - Started");
     while(1)
    {
        if((false == isMDVRConnected()) && (curr_speed >= auto_configure_speed))
        {
            string discover_mdvrs, msg;
            discover_non_configured_and_corrupted_mdvrs(discover_mdvrs, hotspot_ssid, corrupted_ap_ssid);

            if(discover_mdvrs.find("fther") != string::npos || discover_mdvrs.find(corrupted_ap_ssid) != string::npos || discover_mdvrs.find(hotspot_ssid) != string::npos)
            {
                LOG_I(TAG,"Non Configured MDVRs : %s",discover_mdvrs.c_str());

                vector <string> mdvr_ssids_discovered;

                // stringstream class check1
                stringstream response(discover_mdvrs);

                string temp;

                // Tokenizing w.r.t. space ' '
                while(getline(response, temp, ' '))
                {   
                    mdvr_ssids_discovered.push_back(temp);
                }

                mdvr_auto_conf_t conf;
                if(mdvr_ssids_discovered.size() == 1)
                {
                    string ssid = mdvr_ssids_discovered[0];
                    ssid.erase(std::remove(ssid.begin(), ssid.end(), '\n'), ssid.end());
                    strcpy(conf.ssid, ssid.c_str());

                    if(conf.ssid == corrupted_ap_ssid || conf.ssid == hotspot_ssid )
                    {
                        LOG_I(TAG,"DHUB AP Config Corrupted, Sending Message To WIFI_MGR To Correct It");

                        if(conf.ssid == hotspot_ssid)
                        {
                            nd_strncpy(conf.pwd, hotspot_password.c_str(), MAX_PWD_LENGTH);
                        }
                        else
                        {
                            nd_strncpy(conf.pwd, corrupted_ap_password.c_str(), MAX_PWD_LENGTH);
                        }
                        
                        bool corrupt_config_send_msg_status = send_msg( (generic_msg_t *)&conf, (msg_type_t)DHUB_AP_CONFIG_CORRUPTED, sizeof(conf), get_msgq_name() , Q_WIFI_MGR, 0);
                        if(!corrupt_config_send_msg_status)
                        {
                            LOG_I(TAG,"Failed To Send DHUB AP Config Corrupted Message To WIFI_MGR");
                        }
                        else
                        {
                            LOG_I(TAG,"Successfully Sent DHUB AP Config Corrupted Message To WIFI_MGR");
                        }
                    }
                    else
                    {
                        ssid.replace(ssid.find("fther"), 5, "H9v@Dc!4");
                        strcpy(conf.pwd, ssid.c_str());

                        conf.speed = curr_speed;
                        LOG_I(TAG, " Discovered SSID = %s, SPEED = %d", conf.ssid, conf.speed);

                        if(dhub_auto_config == true)
                        {
                            if(!send_msg( (generic_msg_t *)&conf, (msg_type_t)AUTO_CONFIG_MDVR, sizeof(conf), get_msgq_name() , Q_WIFI_MGR, 0))
                            {
                                LOG_I(TAG,"Failed To Send Auto Configure Message To WIFI_MGR");
                            }
                            else
                            {
                                LOG_I(TAG,"Successfully Sent Auto Configure Message To WIFI_MGR");
                            }
                        }

                        if(auto_config_ssid_sent == false)
                        {
                            auto_config_ssid_sent = true;
                            msg = ("DHUB Wifi Scan: ") + discover_mdvrs;
                            nd_service_obj->send_err_msg(SM_E_EXTCAM_AUTO_CONFIGURE_SCAN, curr_speed, msg);
                        }
                    }
                }
            }
        }
          sleep(300);
    }
}

void* extcam_maintainance_thread(void *ptr)
{
    LOG_I(TAG, "extcam_maintainance_thread - Started");

    int conn_check_counter = 0;
    while(1)
    {
        bool dhub_connected = isMDVRConnected();
        
        if(dhub_connected) // checking connectivity of DHUB by requesting curr dhub time, so that there is transfer of packets and the connection is not stale
        {
            LOG_I(TAG,"DHUB Is Connected");
        }
        if(conn_check_counter % SLEEP_DURATION_DHUB_MAINTENCE == 0)
        {
            if(dhub_connected)
            {
                if(file_is_present(mdvrRebootCommandFile))
                {
                    LOG_I(TAG, " Cloud Device Action Command Received For MDVR Reboot");
                    if(reboot_mdvr())
                    {
                        if(file_delete(mdvrRebootCommandFile))
                        {
                            LOG_I(TAG, "Successfully Deleted - Cloud Device Action MDVR Reboot File ");
                        }
                        else
                        {
                            LOG_E(TAG, " Failed to Delete - Cloud Device Action MDVR Reboot File");
                        }
                    }
                    else
                    {
                        LOG_E(TAG, "MDVR Reboot Failed For Cloud Device Action Command");
                    }
                }

                LOG_I(TAG, "Syncing Time to Hub");
                syncTimeToDriveriHub(false);
            }
            else
            {
                string discover_mdvrs;
                discover_non_configured_mdvrs(discover_mdvrs);
                LOG_I(TAG,"Non Configured MDVRs are:%s",discover_mdvrs.c_str());
                string message = "";

                if(mdvr_conf_ssid != "" || bag_conf_ssid != "")
                {
                    message += "DHUB Offline -  ";
                }

                message += "MC:" + mdvr_conf_ssid + ", BC:" + bag_conf_ssid;

                if(discover_mdvrs.find("Not-Found") != string::npos)
                {
                    message += ", Found:None";
                }
                else
                {
                    message += ", Found:" + discover_mdvrs;
                }
                    if(discover_mdvrs.size() <= 0)
                    {
                        sleep(SLEEP_DURATION_DHUB_CONN_CHECK);
                        continue;
                    }
                    nd_service_obj->send_err_msg(SM_E_EXTCAM_OFFLINE_DETECTED, curr_speed, message);

                    if(file_is_present(mdvrRebootCommandFile))
                    {
                        if(file_delete(mdvrRebootCommandFile))
                        {
                            LOG_I(TAG, "MDVR Offline - Successfully Deleted - Cloud Device Action MDVR Reboot File ");
                        }
                        else
                        {
                            LOG_E(TAG, "MDVR Offline - Failed to Delete - Cloud Device Action MDVR Reboot File");
                        }
                    }
            }
        }
        sleep(SLEEP_DURATION_DHUB_CONN_CHECK);
        conn_check_counter++;
    }
}

stream_file_resp_t query_for_session_file(int64_t rec_start_time,int64_t rec_end_time,int ch_num,int cam_num,
        string filename)
{
    if(isMDVRConnected())
    {
        stream_file_resp_t resp = query_for_session_info(rec_start_time, rec_end_time, ch_num,
                filename);

        if(resp != FILE_QUERY_SUCCESS)
        {
            LOG_E(TAG, "Query to MDVR Failed / Session File Not Found");
        }
        else
        {
            LOG_I(TAG, "Query Success - Video Session Found ");
        }

        return resp;
    }
    else
    {
        LOG_I(TAG,"DHUB Oflline Will Try To Query Again Later");
        return TCP_COMM_ERROR;
    }
}

void add_pend_req_pq(ext_request_details req)
{
    lock_guard<recursive_mutex> lock(pq_ext_req_mutex);
    pq_ext_req.push(req);
    LOG_D(TAG,"Added To PQ %s ", req.filename.c_str());
}

void remove_req_pq()
{
    lock_guard<recursive_mutex> lock(pq_ext_req_mutex);
    LOG_I(TAG,"Removing File: %s From PQ", ((pq_ext_req.top()).filename).c_str());
    pq_ext_req.pop(); 
}

bool send_fetch_response_uploader(ext_request_details req)
{
    string ext_vod_dir;
    string ext_vod_fname;
    res_ext_vod_msg_t res_msg;
    bool ret = true;

    if(!get_folder_file_names(req.filename, ext_vod_dir, ext_vod_fname)) {
        LOG_E(TAG, "Failed to get folder and file names from given path");
        ret = false;
    }
    
    do
    {
        ret = stringToCharArray(ext_vod_fname, res_msg.fname,FNAME_LEN);
        if(!ret)
        {
            LOG_I(TAG,"Failed To Copy ext_vod_fname To Char Array");
            break;
        }

        ret = stringToCharArray(req.vod_id, res_msg.vod_id, VOD_ID_LEN);
        if(!ret)
        {
            LOG_I(TAG,"Failed To Copy vod_id To Char Array");
            break;
        }
        ret = stringToCharArray(req.failure_reason , res_msg.failure_reason, EXT_CAM_FAILURE_REASON_LEN);
        if(!ret)
        {
            LOG_I(TAG,"Failed To Copy failure_reason To Char Array");
            break;
        }
        ret = stringToCharArray(req.rgb_status, res_msg.rgb_status, EXT_CAM_RGB_STATUS_LEN);
        if(!ret)
        {
            LOG_I(TAG,"Failed To Copy rgb_status To Char Array");
            break;
        }
     }
     while(false);
    
    res_msg.vod_status = req.vod_status;

    sleep(2);
    if( false == send_msg( (generic_msg_t *)&res_msg, RES_FETCH_EXT_VOD, sizeof(res_msg), get_msgq_name(), Q_UPL, msg_idx++ ) ) 
    {
        LOG_E(TAG, "Not Able To Send EXT VOD Response Message");
        ret = false;
    }
    LOG_I(TAG, "Successfully Sent EXT VOD Response Message,VOD Id = %s, Filename = %s, Priority = %d",req.vod_id.c_str(), req.filename.c_str(), req.priority);
    return ret;
}

bool send_ack_to_uploader_and_update_db(ext_request_details vod_req)
{
    vod_req.failure_reason = "NA";
    vod_req.rgb_status = "NA";
    vod_req.vod_status = EXT_VOD_ACK;

    send_fetch_response_uploader(vod_req);

    stringstream updt_stmt;
    char *zErrMsg = 0;
    updt_stmt << "UPDATE EXTCAM SET VOD_STATUS=1, ACK_TIME=" << get_system_time() << " WHERE VOD_ID='";
    updt_stmt << vod_req.vod_id << "' AND FILENAME='" << vod_req.filename << "' AND REQUEST_TIME=" << vod_req.request_time;
    string update_stmt = updt_stmt.str();

    if(exec_cmd_db(update_stmt, NULL, 0, &zErrMsg) == false)
    {
        LOG_E(TAG, "Failed To Update VOD Status To ACK");
        return false;
    }
    else
    {
        LOG_I(TAG, "Updated VOD Status To ACK For VOD Id = %s, Filename = %s, Priority = %d",vod_req.vod_id.c_str(), vod_req.filename.c_str(), vod_req.priority);
        return true;
    }
}

bool update_vod_status_to_success(int idx_id, const ext_request_details& req)
{
    stringstream updt_stmt;
    char *zErrMsg = 0;
    updt_stmt << "UPDATE EXTCAM SET VOD_STATUS=" << EXT_VOD_SUCCESS << ", FAILURE_REASON='" << req.failure_reason << "', RGB_STATUS='" << req.rgb_status << "' WHERE INDEXID=" << idx_id;
    string update_stmt = updt_stmt.str();

    if(exec_cmd_db(update_stmt, NULL, 0, &zErrMsg) == false)
    {
        LOG_E(TAG, "Failed To Update VOD Status To Success for ID = %d", idx_id);
        return false;
    }
    else
    {
        LOG_I(TAG, "Updated VOD Status To Success, ID = %d, VOD Id = %s, Filename = %s", idx_id, req.vod_id.c_str(), req.filename.c_str());
        return true;
    }
}

bool handle_send_response_failure(ext_request_details req)
{

    stringstream updt_stmt;
    char *zErrMsg = 0;
    updt_stmt << "UPDATE EXTCAM SET VOD_STATUS=" << EXT_VOD_FETCHED_UPLOADER_NOTIFY_FAILED << ", FAILURE_REASON='" << req.failure_reason << "', RGB_STATUS='" << req.rgb_status << "' WHERE VOD_ID='";
    updt_stmt << req.vod_id << "' AND FILENAME='" << req.filename << "' AND REQUEST_TIME=" << req.request_time;
    string update_stmt = updt_stmt.str();

    if(exec_cmd_db(update_stmt, NULL, 0, &zErrMsg) == false)
    {
        LOG_E(TAG, "Failed To Update VOD Status To Fetched Uploader Notify Failed");
        return false;
    }
    else
    {
        LOG_I(TAG, "Updated VOD Status To Fetched Uploader Notify Failed, VOD Id = %s, Filename = %s, Priority = %d",req.vod_id.c_str(), req.filename.c_str(), req.priority);
        return true;
    }

}

bool check_and_manage_db_max_limit()
{
    char *zErrMsg = 0;
    int num_rows = 0;
    get_row_count_from_db(num_rows);
    set_pen_req_val(num_rows);
    // Check for max row limit and proceed to insert
    if(num_rows > MAX_DB_LIMIT)
    {
        // Find oldest row that is not a vod req
        string find_oldest_cmd = "SELECT INDEXID FROM EXTCAM WHERE VOD_ID LIKE 'bagheera%' ORDER BY INDEXID ASC LIMIT 1";
        sqlite3_stmt *stmt;
        int rc = sqlite3_prepare_v2(db_handle, find_oldest_cmd.c_str(), -1, &stmt, NULL);
        if (rc != SQLITE_OK)
        {
            LOG_E(TAG,"Failed to prepare statement: %s",sqlite3_errmsg(db_handle));
            return false;
        }
        int index_id;
        if(sqlite3_step(stmt) == SQLITE_ROW)
        {
            index_id = sqlite3_column_int(stmt, 0);
            LOG_I(TAG,"Deleting Bagheera Entry with INDEXID: %d", index_id);
        }
        sqlite3_finalize(stmt);
        // Make space by deleting oldest row
        stringstream delete_cmd;
        delete_cmd << "DELETE FROM EXTCAM WHERE INDEXID = " << index_id;
        if(exec_cmd_db(delete_cmd.str(), NULL, 0, &zErrMsg) == false)
        {
            return false;
        }
    }
    return true;
}

bool check_if_active_vods_exist_for_file(const string& filename)
{
    const char *select_query = "SELECT COUNT(*) FROM EXTCAM WHERE IS_CANCELLED != 1 AND FILENAME = ?;";
    sqlite3_stmt *stmt;
    int rc;
    int count = 0;
    bool ret = false;

    pthread_mutex_lock(&ext_cam_db_mutex);

    rc = sqlite3_prepare_v2(db_handle, select_query, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOG_E(TAG, "Failed to prepare statement: %s", sqlite3_errmsg(db_handle));
        pthread_mutex_unlock(&ext_cam_db_mutex);
        return false;
    }

    // Bind the filename parameter
    rc = sqlite3_bind_text(stmt, 1, filename.c_str(), -1, SQLITE_STATIC);
    if (rc != SQLITE_OK) {
        LOG_E(TAG, "Failed to bind filename parameter: %s", sqlite3_errmsg(db_handle));
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&ext_cam_db_mutex);
        return false;
    }

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
        ret = true;
    } else {
        LOG_E(TAG, "Failed to execute query: %s", sqlite3_errmsg(db_handle));
    }

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&ext_cam_db_mutex);

    if (ret && count > 0) {
        LOG_I(TAG, "Active VODs present for the file, not deleting: %s", filename.c_str());
        return true;
    }

    return false;
}

void handle_cancellation_completion_req(ext_request_details vod_req, uint64_t req_time)
{
    char *zErrMsg = 0;
    bool rc;

    string update_query = "UPDATE EXTCAM SET IS_CANCELLED = 1 WHERE VOD_ID ='" + vod_req.vod_id + "' AND FILENAME= '" + vod_req.filename + "';";
    ext_req_cancel_map[vod_req.filename] = req_time;
    rc = exec_cmd_db(update_query, NULL, 0, &zErrMsg);

    if(rc == false)
    {
        LOG_E(TAG, "failed to execute exec_cmd_db");
    }
    else
    {
        LOG_I(TAG, "Updated Cancellation In db for VOD Id = %s, Filename = %s, Priority = %d",vod_req.vod_id.c_str(), vod_req.filename.c_str(), vod_req.priority);
    }

    string filename;
    if((vod_req.filename.find(CAMREC_FILE_PREFIX)) == 0)
    {
        filename = CIRCULAR_BUFFER_PATH + vod_req.filename.substr(CAMREC_FILE_PREFIX.length());
    }
    else
    {
        LOG_E(TAG,"File Didnt Contain Camera Record Folder Prefix, Invalid File: %s", filename.c_str());
        return;
    }

    if(save_ext_camera_files_in_dhub)
    {
        if(check_if_active_vods_exist_for_file(vod_req.filename))
        {
            LOG_I(TAG,"Not Deleting File: %s As Active VODs Exist For It", vod_req.filename.c_str());
            return;
        }
        if(file_delete(filename))
        {
            LOG_I(TAG,"Successfully Deleted File: %s For Cancellation/Completion Of Upload", filename.c_str());
            int64_t udid = udid_from_file(filename);
            int64_t sessionCount = sessionCount_from_file(filename);
            int cam_num = get_cam_num_from_filename(filename);
            string base_file_name, folder_name;
            get_folder_file_names(filename, folder_name, base_file_name);
            bool res = create_dummy_file(filename);
            if(res)
            {
                LOG_I(TAG,"Successfully ceated dummy file for file %s",filename.c_str());
                send_file_size_to_bagheera(base_file_name);
            }
            else
            {
                LOG_I(TAG,"Failed ceated dummy file for file %s",filename.c_str());
            }

            //ext_cam_post_circular_buffer_for_save_ext_camera_files_in_dhub(filename, CIRCULAR_BUFFER_TYPE_NORMAL, cam_num, udid, sessionCount);  
        }
        else
        {
            LOG_E(TAG,"Failed To Delete File: %s", filename.c_str());
        }
    }
    else
    {
        LOG_I(TAG,"Not Deleting File: %s As Save Ext Camera Files In DHUB Is Disabled", filename.c_str());
    }
}

bool is_vod_req(ext_request_details req)
{
    if( (req.vod_id != "") && (req.vod_id.find("bagheera") == string::npos) )
    {
        return true;
    }

    return false;
}
bool check_valid_req(ext_request_details req)
{
   if(is_vod_req(req) && req.filename != "" )
   {
        LOG_I(TAG,"%s Is A Valid Request", (req.vod_id).c_str());
        return true;
   }

   LOG_E(TAG,"%s Is A Invalid Request", (req.vod_id).c_str());
   return false;
}

bool handle_vod_req(ext_request_details vod_req, string val_string)
{
    char *zErrMsg = 0;
    bool rc;
    int ret;
    bool ret_val = false;
    sqlite3_stmt *stmt;

    pthread_mutex_lock(&ext_cam_db_mutex);
    stringstream query_cmd;
    query_cmd << "SELECT * FROM EXTCAM WHERE VOD_ID ='" << vod_req.vod_id << "' AND FILENAME ='" << vod_req.filename << "' AND IS_CANCELLED =" << ACTIVE << ";";
    string query_str = query_cmd.str();
    LOG_I(TAG,"Query Command : %s", query_str.c_str());
    ret = sqlite3_prepare_v2(db_handle, query_str.c_str(), -1, &stmt, NULL);
    pthread_mutex_unlock(&ext_cam_db_mutex);

    do
    {
        if (ret != SQLITE_OK) 
        {
            LOG_E(TAG,"Failed to prepare statement: %s",sqlite3_errmsg(db_handle));
            break;
        }
        pthread_mutex_lock(&ext_cam_db_mutex);
        ret = sqlite3_step(stmt);
        pthread_mutex_unlock(&ext_cam_db_mutex);

        if (ret == SQLITE_ROW)
        {
            pthread_mutex_lock(&ext_cam_db_mutex);
            upload_level_t db_priority = static_cast<upload_level_t>(sqlite3_column_int(stmt, 13));
            uint64_t req_time = static_cast<uint64_t>(sqlite3_column_int64(stmt, 14));
            bool is_cancelled = (bool) sqlite3_column_int(stmt, 17);
            pthread_mutex_unlock(&ext_cam_db_mutex);

            if(db_priority != vod_req.priority)
            {
                LOG_I(TAG,"Priority Change Request Received, VOD Id = %s, Filename = %s, Priority = %d",vod_req.vod_id.c_str(), vod_req.filename.c_str(), vod_req.priority);
                handle_cancellation_completion_req(vod_req, req_time);
            }
            else
            {
                if(vod_req.is_cancelled)
                {
                    LOG_I(TAG,"Cancellation Request Received,VOD Id = %s, Filename = %s, Priority = %d",vod_req.vod_id.c_str(), vod_req.filename.c_str(), vod_req.priority);
                    handle_cancellation_completion_req(vod_req, req_time);
                }
                else
                {
                    LOG_I(TAG,"Not Adding Request To DB As It Is A Duplicate Request, VOD Id = %s, Filename = %s, Priority = %d",vod_req.vod_id.c_str(), vod_req.filename.c_str(), vod_req.priority);
                    send_ack_to_uploader_and_update_db(vod_req);
                }
                ret_val = true;
                break;
            }
        }
        else
        {
            if(vod_req.is_cancelled)
            {
                LOG_I(TAG,"Already Cancelled Or Never Marked For Pulling, VOD Id = %s, Filename = %s, Priority = %d",vod_req.vod_id.c_str(), vod_req.filename.c_str(), vod_req.priority);
                ret_val = true;
                break;
            }
        }

        rc = exec_cmd_db(val_string, NULL, 0, &zErrMsg);
        if(rc == false) {
            LOG_E(TAG, "Failed To Execute exec_cmd_db");
            break;
        } else {
            LOG_I(TAG, "Added File: %s To DB", (vod_req.filename).c_str());
            add_pend_req_pq(vod_req);
            LOG_I(TAG,"Added To PQ %s ", (vod_req.filename).c_str());
            send_ack_to_uploader_and_update_db(vod_req);
            
            // Update latest VOD time when new VOD is successfully added
            if (is_valid_vod_request(vod_req)) {
                write_latest_vod_time_to_file(vod_req.request_time);
            }
            
            ret = true;
        }
    }while(false);
    return ret;
}

void *ext_camera_msg_handler(void *arg) {

    LOG_I(TAG,"Ext Cam Msg Handler Thread - Started");

    char *zErrMsg = 0;
    string val_string;
    stringstream val_stream;
    bool dhub_online = false;
    ext_request_details bagheera_req;
    ext_request_details vod_req;

    //Initialize Message Queue
    if( false == init_msgq() ) {
        LOG_E(TAG, "MSG queue init failed");
        pthread_exit(NULL);
    }

    bool isSpeedServiceAvail = check_for_service(Q_SPEED);
    if(isSpeedServiceAvail)
    {
        if(register_for_speed_with_speed(true) == false)
        {
            LOG_E (TAG,"Registration For Speed Service Failed");
        }
    }

    // Get stuck in msg loop
    while(1) {
        nd_msgq_t::nd_msg_t *msg;
        //Block until a new message is received
        if( (msg = server_q->receive( )) == NULL ) {
            LOG_E(TAG, "Receive message failed" );
            sleep(SLEEP_DURATION_DEFAULT);
            continue;
        }

        msg_type_t type = get_msg_type(msg->get_buffer());
        generic_msg_t *m = (generic_msg_t *)msg->get_buffer();
        if( m == NULL ) {
            LOG_E(TAG, "Received NULL message");
            sleep(SLEEP_DURATION_DEFAULT);
            continue;
        }

        LOG_I(TAG, "Received Message Of Type %d ", m->msg_type);

        switch( type ) {
            case REQ_DHUB_INFO_INSTALLER_APP:
                {
                    LOG_I(TAG, "Received REQ_DHUB_INFO_INSTALLER_APP");

                    nd::device::ext_cam_ns::manageDHUB_ExtCam_ConfigDB(); // reloading DB data with latest details when installer app requests


                    set_dhub_ap_mode = true;
                    DHubInfo dhub_info;
                    req_ext_cam_info_t *msg = (req_ext_cam_info_t *)m;

                    if(msg->toggle_dhub_mode == true)
                    {
                        if(handle_dhub_installer_mode())
                        {
                            dhub_info.ip = "10.10.10.254"; /*Hardcoding IP and Mode values as DHUB just switched to AP mode and we may not 
                                                            establish connection quick enough to get these values*/
                            dhub_info.mode = "AP";
                            set_dhub_ap_mode = false; 
                        }
                        else
                        {
                            LOG_E(TAG, "Failed To Handle DHUB Mode During Installer Mode");
                        }
                    }
                    else
                    {
                        LOG_I(TAG, "Not Toggling DHUB Mode To AP Mode For Installer Activity");  
                    }

                    Ext_Cam_Info ext_cam_info;
                    nd_strncpy(ext_cam_info.fwVersion, dhub_info.fwVersion.c_str(), sizeof(ext_cam_info.fwVersion));
                    nd_strncpy(ext_cam_info.ip, dhub_info.ip.c_str(), sizeof(ext_cam_info.ip));
                    nd_strncpy(ext_cam_info.mode, dhub_info.mode.c_str(), sizeof(ext_cam_info.mode));
                    nd_strncpy(ext_cam_info.serialNumber, dhub_info.serialNumber.c_str(), sizeof(ext_cam_info.serialNumber));

                    if(send_msg( (generic_msg_t *)&ext_cam_info, RES_DHUB_INFO_INSTALLER_APP, sizeof(ext_cam_info), get_msgq_name(), Q_INSTALLER_APP, msg_idx++ )) 
                    {
                        LOG_I(TAG, "RES_DHUB_INFO_INSTALLER_APP Sent To Installer App");
                    } 
                    else
                    {
                        LOG_E(TAG, "Failed To Send DHUB Info To Installer App");
                    }
                    dhub_info.print();

                    // Set block entry to prevent DHUB config updates during installer app usage
                    set_dhub_config_update_block_entry();

                }
                break;
            case REQ_STREAM_RECORDED_FILE:
                {
                    int lagTimeOffset = getHubLagTime();
                    stream_recorded_file_msg_t *msg = (stream_recorded_file_msg_t *)m;
                    bagheera_req.filename = msg->filename;

                    int64_t video_start_time = get_system_time();
                    get_video_start_time(bagheera_req.filename, video_start_time);

                    bagheera_req.rec_start_time = msg->start_time + lagTimeOffset;
                    if((msg->end_time - msg->start_time)/1000 >= 60 )
                    {
                        bagheera_req.rec_end_time = msg->start_time + lagTimeOffset + ext_cam_video_duration_request_ms;
                    }
                    else
                    {
                        bagheera_req.rec_end_time = msg->end_time + lagTimeOffset;
                    }

                    bagheera_req.cam_num = msg->cam_num;
                    bagheera_req.framerate = msg->framerate;
                    bagheera_req.ch_num = msg->mdvr_ch_num;
                    bagheera_req.audio_enable = msg->audio_enable;
                    bagheera_req.udid = msg->udid ;
                    curr_udid = bagheera_req.udid;
                    bagheera_req.session_count = msg->sessionCount;
                    bagheera_req.request_time = get_system_time();

                    stringstream ss;
                    ss << "bagheera_" << bagheera_req.udid << "_" << bagheera_req.session_count << "_" << bagheera_req.cam_num;
                    bagheera_req.vod_id = ss.str();


                    bagheera_req.priority = LOWEST_P;
                    bagheera_req.vod_status = EXT_VOD_NOT_ACK;
                    bagheera_req.ack_time = DEFAULT_ACK_TIME;
                    bagheera_req.is_cancelled = ACTIVE;
                    bagheera_req.failure_reason = "NA";
                    bagheera_req.rgb_status = "NA";

                    LOG_I(TAG, "Received Message To Stream : Start Time   = %lld, End Time     = %lld", bagheera_req.rec_start_time, bagheera_req.rec_end_time);
                    LOG_I(TAG, "                           : Cam Num      = %d  , Channel      = %d", bagheera_req.cam_num, bagheera_req.ch_num);
                    LOG_I(TAG, "                           : Audio Enable = %d  , Frame Rate   = %d", bagheera_req.audio_enable, bagheera_req.framerate);
                    LOG_I(TAG, "                           : Udid         = %lld  , Session Count= %lld", bagheera_req.udid, bagheera_req.session_count);

                    if(!channels_enabled_config[bagheera_req.ch_num - 1])
                    {
                        LOG_I(TAG,"Not Adding Request To DB As Requested Channel is Disabled");
                        break;
                    }
                    //Sync Driveri Hub time first before requesting the videos.
                    if(isMDVRConnected())
                    {
                        dhub_online = true;
                        if(mdvr_time_set == false)
                        {
                            syncTimeToDriveriHub(false);
                        }
                    }
                    else
                    {
                        dhub_online = false;

                    }

                    /* Checking config whether to fetch the session video to device or not
                       Configuration is available in [ext_cam_settings] section in bagheera_config.ini*/
                    int dhub_conn_status;
                    dhub_conn_status = get_dhub_conn_status();
                    if(dhub_conn_status >=2000 && DHUB_HW_VER == MDVR_GEN2_HW_VERSION)
                    {
                        LOG_E(TAG,"Not Considering LPW Session For Gen2 DHUB, Not Adding Request To DB");
                        break;
                    }
                    else
                    {
                        if(!dhub_online)
                        {
                            LOG_I(TAG, "Stream Request For %s Will Be Processed Once DHUB Is Online, DHUB Connection Status = %d", bagheera_req.filename.c_str(), dhub_conn_status);
                        }
                        else
                        {
                            LOG_I(TAG, "Processing Stream Request For %s", bagheera_req.filename.c_str());
                        }
                    }

                    if(!check_and_manage_db_max_limit())
                    {
                        LOG_E(TAG,"Failed To Delete Oldest Non VOD Request Even Though DB Limit Reached");
                        break;
                    }

                    // RGB analysis logic based on firmware type
                    if (isNewFirmwareSupported()) {
                        // New firmware: Always do RGB analysis
                        bagheera_req.periodic_rgb_analysis = true;
                        LOG_I(TAG, "New firmware detected: RGB analysis enabled for all files");
                    } else {
                        // Old firmware: Skip RGB analysis for bagheera requests
                        bagheera_req.periodic_rgb_analysis = false;
                        LOG_I(TAG, "Old firmware detected: RGB analysis disabled for bagheera requests");
                    }

                    LOG_I(TAG, "REQ_STREAM_RECORDED_FILE:: udid: %lld  sessionCount: %lld, periodic_rgb_analysis: %d ", msg->udid, msg->sessionCount,bagheera_req.periodic_rgb_analysis);
                    val_stream  << insert_str
                        << "(" << bagheera_req.cam_num << ", "
                        << "'" <<bagheera_req.filename << "'" << ", "
                        << bagheera_req.rec_start_time << ", "
                        << bagheera_req.rec_end_time << ", "
                        << bagheera_req.framerate << ", "
                        << bagheera_req.ch_num << ", "
                        << "'INIT'," 
                        << bagheera_req.audio_enable << ", "
                        << bagheera_req.udid << ", "
                        << bagheera_req.session_count << ", "
                        << int(bagheera_req.periodic_rgb_analysis) << ", "
                        << "'" << bagheera_req.vod_id << "'" << ", "
                        << bagheera_req.priority << ", "
                        << bagheera_req.request_time << ", "
                        << bagheera_req.vod_status << ", "
                        << bagheera_req.ack_time << ", "
                        << bagheera_req.is_cancelled << ", "
                        << "'" << bagheera_req.failure_reason << "'" << ", " 
                        << "'" << bagheera_req.rgb_status  << "'" << ");" ;

                    val_string = val_stream.str();
                    LOG_I(TAG, "insert command: %s", val_string.c_str());
                    val_stream.str("");

                    bool rc = exec_cmd_db(val_string, NULL, 0, &zErrMsg);
                    if(rc == false) {
                        LOG_E(TAG, "failed to execute exec_cmd_db");
                    } else {
                        LOG_I(TAG, "Added filename to db");
                        add_pend_req_pq(bagheera_req);
                        LOG_I(TAG,"Added To PQ %s ", (bagheera_req.filename).c_str());
                    }

                }
                break;
            case REQ_MDVR_TIME_SET:
                {
                    if(isMDVRConnected())
                    {
                        syncTimeToDriveriHub(true);
                    }
                    else
                    {
                        LOG_I(TAG, "MDVR Is Offline - Time Set Not Done");
                        dhub_time_set = false;
                    }
                }
                break;
            case RES_SPEED_REG:
                {
                    LOG_I(TAG, "Speed Service Registration Successfull");
                }
                break;
            case RES_SPEED_UNREG:
                {
                    LOG_I(TAG, "Speed Service Un-Registration Successfull");
                }
                break;
            case RES_SPEED_UPDATE:
                {
                    res_speed_update_msg_t *msg = (res_speed_update_msg_t *)m;
                    curr_speed = msg->speed;
                    LOG_D(TAG,"speed is %d",curr_speed);
                    if(!mdvrFirmwareVerCheckDone && mdvr_firmware_upgrade && (curr_speed >= mdvr_firmware_upgrade_speed))
                    {
                        LOG_I(TAG, "Speed Service Update Recieved Speed = %d", msg->speed);
                        if(firmware_upgrade_status == NOT_STARTED)
                        {   
                            firmware_upgrade_status = STARTED;
                            std::thread([](){
                                try {
                                    check_and_upgrade_mdvr_firmware();
                                } catch (const std::exception& e) {
                                    LOG_E(TAG, "Exception in check_and_upgrade_mdvr_firmware: %s", e.what());
                                    firmware_upgrade_status = NOT_STARTED;
                                } catch (...) {
                                    LOG_E(TAG, "Unknown exception in check_and_upgrade_mdvr_firmware");
                                    firmware_upgrade_status = NOT_STARTED;
                                }
                            }).detach();
                        }
                        else
                        {
                            LOG_I(TAG,"Firmware Upgrade Check STATE : [%d]", firmware_upgrade_status.load());
                        }
                    }
                }
                break;
            case POWERMON_IGNITION:
                {
                    powermon_ignition_msg_t *msg = (powermon_ignition_msg_t*)m;

                    LOG_I(TAG, "Ign status = %lld, crank_change_time = %lld, lpw_status = %lld", msg->status, msg->crank_change_time, msg->lpw_status);
                    if (msg->status == static_cast<int64_t>(IGNITION_ON))
                    {
                        set_igni_status(1);
                        LOG_I(TAG,"Ignition variable set to 1 ");
                    }
                    else if (msg->status == static_cast<int64_t>(IGNITION_OFF))
                    {
                        LOG_I(TAG,"Ignition variable set to 0 ");
                        if(LOW_POWER_WAKEUP_TRUE == msg->lpw_status)
                        {
                            set_igni_status(-1);
                        }
                        else
                        {
                            set_igni_status(0);
                        }
                    }
                    else
                    {
                        LOG_E(TAG,"Error in reading ignition status");
                    }
                }
                break;
            case REQ_TOGGLE_DHUB_WIFI_MODE:
                {
                    LOG_I(TAG,"RECIEVED REQ_TOGGLE_DHUB_WIFI_MODE");
                    toggle_wifi_mode_t* msg =  (toggle_wifi_mode_t*)m;

                    setCurrDHUBWifiModeFromDB();
                    if(msg->mode == eSTAMode)
                    {
                        set_dhub_to_sta_mode = true;
                    }

           }
            break;
            case REQ_DHUB_WIFI_MODE_FROM_DB:
            {
                LOG_I(TAG,"RECIEVED GET_DHUB_WIFI_MODE_DB");
                setCurrDHUBWifiModeFromDB();
            }
            break;
            case RES_WIFI_UPDATE:
            {
                wifi_updates_t *msg = (wifi_updates_t*)m;
                if(msg->acc_conn_info.isConnected)
                {
                    LOG_I(TAG,"DHUB Is Connected");

                }
                else
                {
                    LOG_E(TAG,"DHUB Is Not Connected");
                }
            }
            break;
            case UPDATE_QUERY_FAIL_COUNT:
            {
                dhub_query_fail_msg_t* msg = (dhub_query_fail_msg_t*) m;
                set_query_fail_count(msg->cam_num);
            }
            break;
            case GET_DHUB_PULL_TIME:
                {
                    dhub_pull_time_msg_t *msg =  (dhub_pull_time_msg_t*)m;
                    if (msg->pull_time != -1)
                    {
                        set_pull_time(msg->pull_time, msg->cam_num);
                    }
                }
                break;
            case PAIR_UNPAIR_STATUS:
                {
                    pair_unpair_info *info = (pair_unpair_info*)m;
                    LOG_I(TAG, "Installer App Pair Status Received: %d", info->pair_status);
                    nd::device::ext_cam_ns::manageDHUB_ExtCam_ConfigDB();
                    if(info->pair_status == false)
                    {
                        setDHUBWifiMode(0);
                        reboot_mdvr();
                    }
               }
               break;
            case REQ_DHUB_INFO:
                {
                    req_dhub_health_msg_t* msg = (req_dhub_health_msg_t*) m;
                    send_dhub_info(msg);
                }
                break;
            case INSTALLER_ACTIVE:
                {
                    if(setDHUBWifiMode(0))
                    {
                        reboot_mdvr();
                    }
                    else
                    {
                        set_dhub_ap_mode = true;
                    }

                }
                break;
            case REQ_FETCH_EXT_VOD:
                {
                    req_ext_vod_msg_t* msg = (req_ext_vod_msg_t*) m;
                    vod_req.periodic_rgb_analysis = false;

                    if( msg->fname != NULL)
                    {
                        LOG_I(TAG, "File Received From Uploader is %s, Done : %d, Cancelled : %d", msg->fname, msg->done, msg->cancelled);

                        string vod_filename = CAMREC_FILE_PREFIX + string(msg->fname);
                        vod_req.filename = vod_filename;
                        vod_req.vod_id = msg->vod_id;
                        vod_req.priority = msg->req_priority;
                        vod_req.request_time = msg->request_time;
                        vod_req.is_cancelled = msg->cancelled;
                        
                        if(msg->done)
                        {
                            handle_cancellation_completion_req(vod_req, msg->request_time);
                            break;
                        }

                        if(!get_video_start_time(vod_req.filename, vod_req.rec_start_time))
                        {
                            break;
                        }

                        int lagTimeOffset = getHubLagTime();
                        vod_req.rec_start_time = vod_req.rec_start_time + lagTimeOffset;
                        vod_req.rec_end_time = vod_req.rec_start_time + ext_cam_video_duration_request_ms;
                        
                        vod_req.cam_num = get_cam_num_from_filename(vod_req.filename);
                        if(vod_req.cam_num >= EXTERNAL_CAMERA_START_POSITION && vod_req.cam_num <= EXTERNAL_CAMERA_END_POSITION && channels_enabled_config[get_ext_cam_num(vod_req.cam_num)]) 
                        {
                            LOG_D(TAG,"Valid Cam Number");
                        }
                        else
                        {
                            LOG_E(TAG,"Invalid Cam Number For EXT CAM VOD Request");
                            break;
                        }

                        vod_req.framerate = ext_cam_frame_rate[get_ext_cam_num(vod_req.cam_num)];
                        vod_req.ch_num = get_ch_num_from_cam_num(vod_req.cam_num);
                        vod_req.audio_enable = ext_cam_audio_enabled[get_ext_cam_num(vod_req.cam_num)];

                        if(vod_req.filename != "")
                        {
                            vod_req.udid = udid_from_file(vod_req.filename);
                            vod_req.session_count = sessionCount_from_file(vod_req.filename);
                            LOG_D(TAG,"Got Valid UDID And Session Count From %s",(vod_req.filename).c_str());
                        }
                        else
                        {
                            LOG_E(TAG,"Failed To Get UDID/Session Count As File Empty, Not Adding VOD To DB/PQ");
                            break;
                        }

                        if(rgb_analysis)
                        {
                            // RGB analysis logic based on firmware type for VOD requests
                            if (isNewFirmwareSupported()) {
                                // New firmware: Always do RGB analysis
                                vod_req.periodic_rgb_analysis = true;
                                LOG_I(TAG, "New firmware detected: RGB analysis enabled for VOD request");
                            } else {
                                // Old firmware: Skip RGB analysis for VOD/alert requests
                                vod_req.periodic_rgb_analysis = false;
                                LOG_I(TAG, "Old firmware detected: RGB analysis disabled for VOD/alert request");
                            }
                        }


                        LOG_I(TAG, "Received Message To Stream VOD : Start Time   = %lld, End Time     = %lld", vod_req.rec_start_time, vod_req.rec_end_time);
                        LOG_I(TAG, "                               : Cam Num      = %d  , Channel      = %d", vod_req.cam_num, vod_req.ch_num);
                        LOG_I(TAG, "                               : Audio Enable = %d  , Frame Rate   = %d", vod_req.audio_enable, vod_req.framerate);
                        LOG_I(TAG, "                               : Udid         = %lld  , Session Count= %lld", vod_req.udid, vod_req.session_count);
                        LOG_I(TAG, "                               : Request_time  = %llu  , Priority %ld", vod_req.request_time, vod_req.priority);
    
                        if(!check_and_manage_db_max_limit())
                        {
                            LOG_E(TAG,"Failed To Delete Oldest Non VOD Request Even Though DB Limit Reached");
                            break;
                        }


                        val_stream  << insert_str
                            << "(" << vod_req.cam_num << ", "
                            << "'" << vod_req.filename << "'" << ", "
                            << vod_req.rec_start_time << ", "
                            << vod_req.rec_end_time << ", "
                            << vod_req.framerate << ", "
                            << vod_req.ch_num << ", "
                            << "'INIT', "
                            << vod_req.audio_enable << ", "
                            << vod_req.udid << ", "
                            << vod_req.session_count << ", "
                            << int(vod_req.periodic_rgb_analysis) << ", "
                            << "'" << vod_req.vod_id << "'" << ", "
                            << vod_req.priority << ", "
                            << vod_req.request_time << ", "
                            << vod_req.vod_status << ", "
                            << vod_req.ack_time << ", "
                            << vod_req.is_cancelled << ", " 
                            << "'" << vod_req.failure_reason << "'" << ", "
                            << "'" << vod_req.rgb_status << "'" << ");";

                        val_string = val_stream.str();
                        LOG_I(TAG, "insert command: %s", val_string.c_str());
                        val_stream.str("");

                        if(!check_valid_req(vod_req))
                        {
                            break;
                        }

                        if(handle_vod_req(vod_req, val_string))
                        {
                            LOG_I(TAG,"Handled VOD Req: %s Sucessfully",(vod_req.vod_id).c_str());
                        }
                        else
                        {
                            LOG_E(TAG,"Failed To Handle VOD Req: %s",(vod_req.vod_id).c_str());
                        }
                    }
                    else
                    {
                        LOG_E(TAG,"Filename Is Empty, Not Adding VOD To DB/PQ");
                    }
                }
                break;
            case REQ_CANCEL_ALL_EXT_VOD:
                {
                    req_ext_vod_msg_t* msg = (req_ext_vod_msg_t*) m;
                    bool ret = false;
                    if(delete_all_ext_vods_from_db())
                    {

                        //empty priority queue as well here
                        res_ext_vod_msg_t res_msg;

                        string failure_reason = "NA";
                        string rgb_status = "NA";
                        string vod_id = "NA";
                        string filename = "NA";

                        ret = stringToCharArray(filename, res_msg.fname,FNAME_LEN);;
                        if(!ret)
                        {
                            LOG_I(TAG,"Failed To Copy ext_vod_fname To Char Array");
                            break;
                        }

                        ret = stringToCharArray(vod_id, res_msg.vod_id, VOD_ID_LEN);
                        if(!ret)
                        {
                            LOG_I(TAG,"Failed To Copy vod_id To Char Array");
                            break;
                        }
                        ret = stringToCharArray(failure_reason, res_msg.failure_reason, EXT_CAM_FAILURE_REASON_LEN);
                        if(!ret)
                        {
                            LOG_I(TAG,"Failed To Copy failure_reason To Char Array");
                            break;
                        }
                        ret = stringToCharArray(rgb_status, res_msg.rgb_status, EXT_CAM_RGB_STATUS_LEN);
                        if(!ret)
                        {
                            LOG_I(TAG,"Failed To Copy rgb_status To Char Array");
                            break;
                        }


                        res_msg.vod_status = EXT_VOD_ACK;

                        if( false == send_msg( (generic_msg_t *)&res_msg, RES_CANCEL_ALL_EXT_VOD, sizeof(res_msg), get_msgq_name(), Q_UPL, msg_idx++ ) ) 
                        {
                            LOG_E(TAG, "Not Able To Send EXT VOD Response ACK Message");
                        }
                        else
                        {
                            LOG_I(TAG, "Successfully Sent EXT VOD Response ACK Message");
                        }

                    }
                }
                break;
            case DHUB_RECOVERY:
                {
                    LOG_I(TAG,"DHUB_recovery_flag Called From Automation");
                    dhub_recovery_msg_t *msg = (dhub_recovery_msg_t *)m;
                    DHUB_recovery_flag = msg->config_flag;
                    if(is_automation_enabled && !DHUB_recovery_flag)
                    {
                        if(get_mdvr_rec_config())
                        {
                            LOG_I(TAG,"Sucessfully Got Recovery Config");
                        }
                        else
                        {
                            LOG_E(TAG,"Failed To Get Recovery Config");
                        }
                    }
                    
                }
                break;
            default:
                {
                    LOG_E(TAG, "Unknown message received");
                }
                break;
        }
        delete msg;
    }

    pthread_exit(NULL);
}

void delete_from_db(int idx_id) {
    char *zErrMsg = 0;
    stringstream del_stmt;
    del_stmt << "DELETE FROM EXTCAM WHERE INDEXID=";
    del_stmt << idx_id;
    string delete_stmt = del_stmt.str();
    if(exec_cmd_db(delete_stmt, NULL, 0, &zErrMsg) == true) {
        LOG_D(TAG, "Deleted row from db");
    }
}

void set_thread_active_flag(int th_idx, bool flag) {
    if(flag == true) {
        LOG_I(TAG, "Marking thread %d as active", th_idx);
    }
    else {
        LOG_I(TAG, "Marking thread %d as inactive", th_idx);
    }

    // Mark thread as free before exit   
    pthread_mutex_lock(&file_dld_mutex);
    file_dld_thread_active[th_idx] = flag;
    pthread_mutex_unlock(&file_dld_mutex);
    // Small sleep better to have so that thread exists will not impact the flag update
    sleep(1);
}

bool check_req_is_cancelled(ext_request_details req) {
    auto it = ext_req_cancel_map.find(req.filename);
    if (it != ext_req_cancel_map.end() && it->second == req.request_time) {
        return true;
    }
    return false;
}

bool get_index_id_from_filename_req_time(ext_request_details req, int &idx_id)
{
    int rc = 0;
    bool ret = false;
    sqlite3_stmt *stmt;
    stringstream get_index_id_cmd;
    LOG_I(TAG,"Get Index Filename : %s, Request_time : %llu", req.filename.c_str(), req.request_time);
    get_index_id_cmd << "SELECT INDEXID FROM EXTCAM WHERE FILENAME='" << req.filename << "' AND REQUEST_TIME=" << req.request_time;
    string get_index_id_cmd_str = get_index_id_cmd.str();
    rc = sqlite3_prepare_v2(db_handle, get_index_id_cmd_str.c_str(), -1, &stmt, NULL);

    do
    {
        if (rc != SQLITE_OK)
        {
            LOG_E(TAG,"Failed to prepare statement: %s",sqlite3_errmsg(db_handle));
            break;
        }

        rc = sqlite3_step(stmt);
        if (rc != SQLITE_ROW) 
        {
            LOG_I(TAG, "Found no rows in db");
            break;
        }
        else if(rc == SQLITE_ROW)
        {
            idx_id = sqlite3_column_int(stmt, 0);
            ret = true;
        }
    }while(false);
    if(stmt)
    {
        sqlite3_finalize(stmt);
    }
    return ret;
}

bool get_req_and_index_id(ext_request_details &req, int &index_id)
{
    lock_guard<recursive_mutex> lock(pq_ext_req_mutex);
    while(!pq_ext_req.empty())
    {
        req = pq_ext_req.top();
        remove_req_pq(); 
        if(get_index_id_from_filename_req_time(req, index_id))
        {
            return true;
        }
        else
        {
            LOG_E(TAG,"Present in Priority Queue But Not In DB, Removing Priority Queue entry");
        }
    }
    return false;
}

void get_rgb_status_string(rgb_err rgb_status, string &ext_vod_rgb_status)
{
    switch(rgb_status)
    {
        case BLACK_VIDEO:
            ext_vod_rgb_status = "black";
            break;
        case GREY_VIDEO:
            ext_vod_rgb_status = "grey";
            break;
        case WHITE_VIDEO:
            ext_vod_rgb_status = "white";
            break;
        case DISTORTED_VIDEO:
            ext_vod_rgb_status = "distorted";
            break;
        case RGB_LOSS_VIDEO:
            ext_vod_rgb_status = "rgb_loss";
            break;
        case NORMAL_VIDEO:
            ext_vod_rgb_status = "normal";
            break;
        case CAM_ERROR:
            ext_vod_rgb_status = "black";
            break;
        case UNKNOWN_ERROR_VIDEO:
            ext_vod_rgb_status = "unknown_error";
            break;
        case FRAME_COUNT_ERROR:
            ext_vod_rgb_status = "frame_count_error";
            break;
        case IMG_READ_ERROR:
            ext_vod_rgb_status = "img_read_error";
            break;
        default:
            ext_vod_rgb_status = "not-applicable";
            break;
    }
    return;
}

bool is_dummy_file(string fname) {
    ifstream file(fname);
    if (file) {
        int file_size = get_file_size(fname);
        if (file_size < (DHUB_BIN_FILE_MIN_LIMIT * 0.02)) {
            return true;
        }
    } else {
        LOG_E(TAG,"Failed to open file: %s",fname.c_str());
    }
    return false;
}

bool is_file_already_pulled(string filename, string& dest_filename)
{
    if(filename.find(CAMREC_FILE_PREFIX) == 0)
    {
        dest_filename = CIRCULAR_BUFFER_PATH + filename.substr(CAMREC_FILE_PREFIX.length());
    }
    else
    {
        LOG_I(TAG,"File Didnt Contain Camera Record Folder Prefix, Going To Pull File: %s", filename.c_str());
        return false;
    }

    if(file_is_present(dest_filename) && (!is_dummy_file(dest_filename)))
    {
        LOG_I(TAG,"File: %s Already Pulled", dest_filename.c_str());
        return true;
    }
    LOG_I(TAG,"File: %s Yet To Be Pulled", dest_filename.c_str());
    return false;
}

void update_db_state_to_init(int idx_id)
{
    // Update state of this row to init so that it will be picked by other thread again  
    char *zErrMsg = 0;
    stringstream updt_stmt;
    updt_stmt << "UPDATE EXTCAM SET STATE='INIT' WHERE INDEXID=";
    updt_stmt << idx_id;
    string update_stmt = updt_stmt.str();
    // Update all the states to init before we start
    if(exec_cmd_db(update_stmt, NULL, 0, &zErrMsg) == false) {
        LOG_E(TAG, "Failed to update state to init");
        sleep(SLEEP_DURATION_DEFAULT);
    } else {
        LOG_I(TAG, "Updated state of row with index id %d to init in db", idx_id);
    }

}

void update_failure_reason_and_rgb_status(stream_file_resp_t res, rgb_err rgb_status, ext_request_details& req)
{
    if(res == FILE_DURATION_CHECK_FAIL || 
       res == FILE_DOWNLOAD_SUCCESS || 
       res == FILE_TOO_BIG ||
       res == RGB_ANALYSIS_DONE)
    {
        if(rgb_status != DEFAULT)
        {
            get_rgb_status_string(rgb_status, req.rgb_status);
        }
        else
        {
            req.rgb_status = "not-applicable";
        }
        req.vod_status = EXT_VOD_SUCCESS;
    }
    else
    {
        if(res == FILE_QUERY_FAIL)
        {
            req.vod_status = EXT_VOD_NOT_AVAILABLE; 
        }
        else if(res == FILE_EMPTY)
        {
            req.vod_status = EXT_VOD_FAILURE;
            req.failure_reason  = "Empty File Pulled";
        }
        else
        {
            req.vod_status = EXT_VOD_NOT_AVAILABLE; 
        }
    }
}


void *pull_mdvr_file(void *args) {
    if(args == NULL) {
        LOG_E(TAG, "args for pull files thread NULL");
        pthread_exit(NULL);
    }

    thread_param_t *th_param = (thread_param_t *)args;
    int idx_id = th_param->db_index_id;
    int th_idx = th_param->thread_index_id;
    ext_request_details req = th_param->req;
    req.failure_reason = "NA";
    req.rgb_status = "NA";

    char *zErrMsg = 0;
    string filename = "";
    int64_t rec_start_time, rec_end_time, udid = 0, sessionCount = 0;
    int ch_num, cam_num, framerate, audio_enable;
    bool periodic_rgb_analysis = isNewFirmwareSupported(); // Initialize based on firmware capability
    long pull_time;
    rgb_err rgb_status = DEFAULT;
    string dest_filename;

    stringstream log_tag_ss;
    log_tag_ss << "PULL_MDVR_TH";
    log_tag_ss << th_idx;
    string log_tag = log_tag_ss.str();
    const char* pull_tag = log_tag.c_str();

    LOG_I(pull_tag, "pulling file from thread %d for row with idx:%d", th_idx, idx_id);

    if(!check_req_is_cancelled(req))
    {
        filename = req.filename;
        rec_start_time = req.rec_start_time;
        rec_end_time = req.rec_end_time;
        int64_t present_time = get_system_time();
        if(present_time < rec_end_time)
        {
            LOG_E(pull_tag,"Sleeping For 60 Seconds As Session End Time Is In Future, Session End Time is %lld",rec_end_time);
            sleep(60);
        }
        ch_num = req.ch_num;
        cam_num = req.cam_num;
        framerate = req.framerate;
        audio_enable = req.audio_enable;
        udid = req.udid;
        sessionCount = req.session_count;
        periodic_rgb_analysis = req.periodic_rgb_analysis;
        LOG_I(pull_tag, "after reading from queue:: priority: %d  sessionCount: %lld filename: %s PeriodicRgbAnalysis : %d ", req.priority, sessionCount ,filename.c_str(),periodic_rgb_analysis);
        
        // OLD FIRMWARE: Determine if we should update cam_status_array
        // Only update when: save_ext_camera_files_in_dhub=false AND NOT a VOD
        bool update_rgb_health = (!save_ext_camera_files_in_dhub && !is_vod_req(req));

        float ext_cam_file_size = 0.0;
        stream_file_resp_t res = TCP_COMM_ERROR;
        do {

            if(!isMDVRConnected())
            {
                break;
            }

            if(filename == "") {
                break;
            }

            if(is_file_already_pulled(filename, dest_filename))
            {
                res = FILE_DOWNLOAD_SUCCESS;
                get_rgb_status(dest_filename, ch_num, rgb_status, update_rgb_health);
                break;
            }
            string file_prefix = filename.substr(0, filename.length() - 3);
            string mp4_filename = file_prefix + "mp4";

            LOG_I(pull_tag, "fname:%s, start_time: %lld, end_time: %lld, ch_num: %d, cam_num: %d, frate: %d, audio: %d",
                    mp4_filename.c_str(), rec_start_time, rec_end_time, ch_num, cam_num, framerate, audio_enable);

            int retry_count = 0, file_empty_retry_count = 0, tcp_comm_retry_count = 0;
            int64_t start_time_resp, end_time_resp;
            while(retry_count < MAX_RETRY_COUNT && file_empty_retry_count < max_retry_file_empty && tcp_comm_retry_count < MAX_TCP_COMM_RETRY_COUNT) {
                // It would be good to delete any stale files before proceeding with streaming
                if(file_is_present(mp4_filename) == true) {
                    file_delete(mp4_filename);
                }

                if((!is_vod_req(req)) && save_ext_camera_files_in_dhub)
                {
                    string fname = "";
                    string folder_name = "";

                    if(!get_folder_file_names(filename, folder_name, fname)) {
                        LOG_E(pull_tag, "Failed to get folder and file names from given path");
                    }
                    res = query_for_session_file(rec_start_time, rec_end_time, ch_num, cam_num,filename);
#if 0
                    if(FILE_QUERY_SUCCESS == res)
                    {
                        ext_cam_post_circular_buffer_for_save_ext_camera_files_in_dhub(fname, CIRCULAR_BUFFER_TYPE_NORMAL, cam_num, udid, sessionCount);
                    }
                    else
                    {
                        LOG_I(pull_tag,"File Query Failed, Not Adding To Circular Buffer");
                    }
#endif
                }
                else
                {
                    res = stream_saved_file(mp4_filename, rec_start_time, rec_end_time, ch_num,
                            framerate, (bool)audio_enable,  update_rgb_health, pull_time, rgb_status);
                    LOG_D(pull_tag, "stream_saved_file resp: %d", res);

                    set_pull_time(pull_time, cam_num);  
                }
                if(res == FILE_QUERY_SUCCESS || res == FILE_DOWNLOAD_SUCCESS || res == FILE_TOO_BIG || res == RGB_ANALYSIS_DONE) {
                    break;
                }

                if(res == FILE_EMPTY) {
                    sleep(SLEEP_DURATION_FILE_EMPTY);
                    file_empty_retry_count++;
                    continue;
                }

                // On TCP comm error, no point in breaking loop            
                if(res == TCP_COMM_ERROR) {
                    LOG_E(pull_tag, "no TCP connection to MDVR. check if wifi is connected or mdvr is off");
                    sleep(SLEEP_DURATION_DEFAULT);
                    tcp_comm_retry_count++;
                    continue;
                }

                retry_count++;
                sleep(5);
            }

            // proceed to copy only if file query is success or duration check fails
            if(res == FILE_DOWNLOAD_SUCCESS || res == FILE_DURATION_CHECK_FAIL || res == FILE_TOO_BIG) {
                int file_size = get_file_size(mp4_filename);
                if(file_size < DHUB_BIN_FILE_MIN_LIMIT) // file size less than 1MB then considering it as a empty file
                {
                    LOG_I(pull_tag,"file : %s size is less than 1MB considering it as empty file",mp4_filename.c_str());
                    set_empty_file_count(cam_num);

                    stringstream message;
                    string timestamp;
                    if(! get_timestamp_from_filename(mp4_filename, timestamp)) {
                        LOG_E(pull_tag, "Failed to get timestamp from filename: %s", mp4_filename.c_str());
                        timestamp = "-1";
                    }

                    if(!report_black_videos[ch_num-1])
                    {
                        report_black_videos[ch_num-1] = true;
                        message << " Cam " << cam_num << " - Black Video @" << timestamp;
                        LOG_E(pull_tag, "Black Video: %s", message.str().c_str());
                        switch (cam_num)
                        {
                            case cam_4:
                                nd_service_obj->send_err_msg(SM_E_EXT_CAM_VIDEO_BLACK, cam_num, message.str());
                                break;
                            case cam_5:
                                nd_service_obj->send_err_msg(SM_E_EXT_CAM_VIDEO_BLACK, cam_num, message.str());
                                break;
                            case cam_6:
                                nd_service_obj->send_err_msg(SM_E_EXT_CAM_VIDEO_BLACK, cam_num, message.str());
                                break;
                            case cam_7:
                                nd_service_obj->send_err_msg(SM_E_EXT_CAM_VIDEO_BLACK, cam_num, message.str());
                                break;
                            default:
                                LOG_E(pull_tag, "Unexpected cam_num: %d", cam_num);
                                break;
                        }
                    }
                }
                ext_cam_file_size = file_size/1000000.0;
                LOG_I(pull_tag,"Ext Cam File Size is %f", ext_cam_file_size);
                copy_and_post_circular_buffer(mp4_filename, cam_num, udid, sessionCount);
            }
            if(res == RGB_ANALYSIS_DONE)
            {
                if(!(is_vod_req(req)) && save_ext_camera_files_in_dhub)
                {

                    if(file_delete(mp4_filename) == false)
                    {
                        LOG_I(pull_tag, "Failed to delete db file");
                    }
                    else
                    {
                        LOG_I(pull_tag,"Deleted File after RGB analysis :  %s ",mp4_filename.c_str());
                    }
                }
                else
                {
                    copy_and_post_circular_buffer(mp4_filename, cam_num, udid, sessionCount);
                }
            }
        } while (false);

        if(res > TCP_COMM_ERROR) {
            if(res == FILE_QUERY_FAIL) {
                set_query_fail_count(cam_num);
                LOG_E(pull_tag, "not able to find video file in MDVR. deleting entry from db, fname: %s", filename.c_str());
            } else if(res == FILE_EMPTY) {
                set_empty_file_count(cam_num);
                LOG_I(pull_tag, "file empty even after retrials. deleting entry from db. fname: %s", filename.c_str());
            } else if(res == FILE_DURATION_CHECK_FAIL) {
                set_partial_file_count(cam_num);
                total_partial_files++;
                total_partial_files_size +=ext_cam_file_size;
                LOG_I(pull_tag, "file duration check failed even after retrials. keeping the file and deleting entry from db. fname: %s", filename.c_str());
            } else if(res == FILE_TOO_BIG) {
                LOG_I(pull_tag, "Very Large Video File Pulled From DHUB, Keeping File And Deleting Entry From DB, File Name: %s", filename.c_str());
            } else if(res == FILE_DOWNLOAD_SUCCESS) {
                LOG_I(pull_tag, "video file in MDVR successfully downloaded. deleting entry from db, fname: %s", filename.c_str());
            } else if(res == FILE_QUERY_SUCCESS) {
                LOG_I(pull_tag, "video file in MDVR successfully queried. deleting entry from db, fname: %s", filename.c_str());
            } else if(res == RGB_ANALYSIS_DONE){
                LOG_I(pull_tag,"Rgb done on video : %s", filename.c_str()); 
            }else {
                LOG_I(pull_tag, "deleting entry from db due to other errors, fname: %s", filename.c_str());
            }

            bool report_error = false;
            if(save_ext_camera_files_in_dhub)
            {
                if(!(is_vod_req(req)) && (res != FILE_QUERY_SUCCESS) && (res != FILE_DOWNLOAD_SUCCESS)) // File Can Be Downloaded Even In save_ext_camera_files_in_dhub = true For VODs
                {
                    report_error = true;
                }
            }
            else
            {
                if( (res != FILE_QUERY_SUCCESS) && (res != FILE_DOWNLOAD_SUCCESS) && (res != FILE_DURATION_CHECK_FAIL) && (res != RGB_ANALYSIS_DONE) && (res != FILE_TOO_BIG))
                {
                    report_error = true;
                }
            }

            if(report_error)
            {
                stringstream message;
                message << " Cam " << cam_num << " - Video Not Available @" << rec_start_time;
                nd_service_obj->send_err_msg(SM_E_EXTCAM_VIDEO_FILE_NOT_PRESENT, curr_speed, message.str());
            }

            if(is_vod_req(req))
            {
                LOG_I(TAG,"Video Pull Process Done For VOD Request: %s", filename.c_str());

                update_failure_reason_and_rgb_status(res, rgb_status, req);
                if(send_fetch_response_uploader(req))
                {
                    //delete_from_db(idx_id);
                    update_vod_status_to_success(idx_id, req);
                }
                else
                {
                    if(!handle_send_response_failure(req))
                    {
                        update_db_state_to_init(idx_id); 
                        add_pend_req_pq(req);
                        LOG_I(TAG,"Added To PQ %s ", (req.filename).c_str());
                    }
                }
            }
            else
            {
                LOG_I(TAG,"Video Pull Process Done For Bagheera Request: %s", filename.c_str());
                // proceed to delete irrespective of file query success or fail
                delete_from_db(idx_id);
            }
        } 
        else 
        {
            update_db_state_to_init(idx_id);
            add_pend_req_pq(req); //adding back to queue for it to retry later
            LOG_I(TAG,"Added To PQ %s ", (req.filename).c_str());
        }
    }
    else
    {
        LOG_I(TAG,"Not Pulling Fot Cancelled Req: %s",(req.vod_id).c_str());
        delete_from_db(idx_id);
    }

    set_thread_active_flag(th_idx, false);
    pthread_exit(NULL);
}

static int integrity_callback(void *NotUsed, int argc, char **argv, char **azColName) {

    if (strstr(argv[0],"ok") != NULL)
    {
        db_integrity_check = true;
    }

    return 0;
}

static int callback_col_names(void* str_vector, int argc, char** argv, char **azColName){
    vector<string>* t_str_vector = (vector < string > *) str_vector;
    string tstring;
    
    if (argc < 2){
        LOG_E(TAG, "Something went wrong in callback_col_names");
        return -1;
    }

    tstring = argv[1];
    t_str_vector->push_back(tstring);
    
    return 0;
}

bool check_integrity_db(){
    bool ret = false, rc = false;
    char *zErrMsg = 0;
    string sql;
    string result;
    sql = "PRAGMA integrity_check";

    rc = exec_cmd_db(sql, integrity_callback, NULL, &zErrMsg);
    if(rc == false){
        LOG_E(TAG, "failed to execute %s", sql.c_str());
        return false;
    }

    return db_integrity_check;
}

bool check_integrity_extcam_config_db(db_handle_t* db_handle){
    bool rc = false;
    char *zErrMsg = 0;
    string sql;
    string result;
    sql = "PRAGMA integrity_check";

    rc = exec_cmd_extcam_config_db(sql, integrity_callback, NULL, &zErrMsg, db_handle);
    if(rc == false){
        LOG_E(TAG, "failed to execute %s", sql.c_str());
        return false;
    }

    return db_integrity_check;
}

void check_and_rectify_extcam_config_db(NDService* nd_service_obj, int curr_speed) {
    db_handle_t* ext_cam_config_db_handle = nullptr;
    bool ret = open_db(extcam_config_db_file_path.c_str(), &ext_cam_config_db_handle);
    if (!ret) {
        LOG_E(TAG, "Failed to open Ext_cam_config.db");
        return;
    }
    LOG_I(TAG, "success in open DB");
    bool healthy = check_integrity_extcam_config_db(ext_cam_config_db_handle);
    if (!healthy) { 
        LOG_E(TAG, "Possible Corruption Of DB");
        if (nd_service_obj) {
            nd_service_obj->send_err_msg(SM_E_EXTCAM_CONFIG_DB_CORRUPTED, curr_speed, "Ext Cam Config DB Corrupted");
        }
        if (file_delete(extcam_config_db_file_path) == false) {
            LOG_E(TAG, "Failed to delete Ext_cam_config.db file");
        } else {
            LOG_E(TAG, "Deleted corrupted Ext_cam_config.db file, will recreate");
        }
    }
    if (ext_cam_config_db_handle) {
        close_db(ext_cam_config_db_handle);
    }
}

bool check_col_exists(string column_name) {
    bool ret = false, rc = false;
    char *zErrMsg = 0;
    string sql;
    vector<string> col_names;
    sql = "PRAGMA table_info(EXTCAM)";

    rc = exec_cmd_db(sql, callback_col_names, (void*) &col_names, &zErrMsg);
    if(rc == false){
        LOG_E(TAG, "failed to execute %s", sql.c_str());
        return false;
    }

    for ( vector<string>::iterator it = col_names.begin(); it != col_names.end(); ++it){
        if (column_name.compare(*(it)) == 0){
            ret = true;
            break;
        }
    }

    return ret;
}

bool add_col(string column_name) {
    bool rc;
    char* zErrMsg = 0;
    string val_string;
    std::stringstream val_stream;
    val_stream << "ALTER TABLE EXTCAM ADD COLUMN " << column_name << " INTEGER DEFAULT 0;";
    val_string = val_stream.str();
 
    rc = exec_cmd_db(val_string, NULL, (void*) NULL, &zErrMsg);
    if(rc == false){
        LOG_E(TAG, "failed to execute %s", val_string.c_str());
        return rc;
    }

    LOG_I(TAG, "%s column created using %s", column_name.c_str(), val_string.c_str());
    
    return rc;
}

bool check_and_add_column(db_handle_t* db_handle, string column_name) {
    bool coloumn_exists = false, add_col_attempt = false;
    coloumn_exists = check_col_exists(column_name);
    if (!coloumn_exists) {
        LOG_I(TAG, "%s column does not exist", column_name.c_str());
        add_col_attempt = add_col(column_name);
        if (!add_col_attempt){
            LOG_E(TAG, "Unable to add %s column to DB", column_name.c_str());
            return false;
        }
    } else {
        LOG_I(TAG, "column %s already exist in the table", column_name.c_str());
    }

    return true;
}

bool add_col_string(string column_name) {
    bool rc;
    char* zErrMsg = 0;
    string val_string;
    std::stringstream val_stream;
    val_stream << "ALTER TABLE EXTCAM ADD COLUMN " << column_name << " TEXT DEFAULT '';";
    val_string = val_stream.str();

    rc = exec_cmd_db(val_string, NULL, (void*) NULL, &zErrMsg);
    if(rc == false){
        LOG_E(TAG, "failed to execute %s", val_string.c_str());
        return rc;
    }

    LOG_I(TAG, "%s column created using %s", column_name.c_str(), val_string.c_str());

    return rc;
}

bool check_and_add_string_column(db_handle_t* db_handle, string column_name) {
    bool coloumn_exists = false, add_col_attempt = false;
    coloumn_exists = check_col_exists(column_name);
    if (!coloumn_exists) {
        LOG_I(TAG, "%s column does not exist", column_name.c_str());
        add_col_attempt = add_col_string(column_name);
        if (!add_col_attempt){
            LOG_E(TAG, "Unable to add %s column to DB", column_name.c_str());
            return false;
        }
    } else {
        LOG_I(TAG, "column %s already exist in the table", column_name.c_str());
    }

    return true;
}

bool add_col_priority(string column_name) {
    bool rc;
    char* zErrMsg = 0;
    string val_string;
    std::stringstream val_stream;
    val_stream << "ALTER TABLE EXTCAM ADD COLUMN " << column_name << " INTEGER DEFAULT " << (int)LOWEST_P << ";";
    val_string = val_stream.str();

    rc = exec_cmd_db(val_string, NULL, (void*) NULL, &zErrMsg);
    if(rc == false){
        LOG_E(TAG, "failed to execute %s", val_string.c_str());
        return rc;
    }

    LOG_I(TAG, "%s column created using %s", column_name.c_str(), val_string.c_str());

    return rc;
}

bool check_and_add_priority_column(db_handle_t* db_handle, string column_name) {
    bool coloumn_exists = false, add_col_attempt = false;
    coloumn_exists = check_col_exists(column_name);
    if (!coloumn_exists) {
        LOG_I(TAG, "%s column does not exist", column_name.c_str());
        add_col_attempt = add_col_priority(column_name);
        if (!add_col_attempt){
            LOG_E(TAG, "Unable to add %s column to DB", column_name.c_str());
            return false;
        }
    } else {
        LOG_I(TAG, "column %s already exist in the table", column_name.c_str());
    }

    return true;
}

int db_version_cb(void *data, int argc, char **argv, char **azColName) {
    int *currentVersion = (int *)data;
    if (argc > 0 && argv[0]) {
        int curr_version = 0;
        string_to_integer(argv[0], curr_version);
        *currentVersion = curr_version;
    }
    return 0;
}

bool alter_db_add_column(db_handle_t* handle, const string& column, const string& defaultValue, bool notNull = true)  
{     
    string alter_query = "ALTER TABLE EXTCAM ADD COLUMN " + column + " " + defaultValue;  
    if (notNull)  
    {  
        alter_query += " NOT NULL";  
    }  
    alter_query += ";";  
  
    char* zErrMsg = nullptr;
    int rc = sqlite3_exec(handle, alter_query.c_str(), nullptr, 0, &zErrMsg);
    if (rc != SQLITE_OK)
    {
        LOG_E(TAG, "Can't Alter db Table EXT_CAM: %s",  zErrMsg);
        sqlite3_free(zErrMsg);
        return false;
    }
    return true;
}

bool handle_db_versioning_and_column_addition(db_handle_t* handle)
{
    char *zErrMsg = NULL;
    int rc = 0;
    bool status = false;

    int currentVersion = -1;
    rc = sqlite3_exec(handle, "PRAGMA user_version;", db_version_cb, &currentVersion, &zErrMsg);
    if (rc != SQLITE_OK)
    {
        LOG_E(TAG, "Failed To Get user_version: %s", sqlite3_errmsg(handle));
        sqlite3_free(zErrMsg);
        return status;
    }
    LOG_I(TAG, "Current DB version: %d", currentVersion);

    if (currentVersion == 0)
    {
        bool add_col_status = check_and_add_column(handle, "PULL");
        if (!add_col_status)
        {
            LOG_E(TAG, "Failed To Add Column PULL");
            return status;
        }

        // Begin a transaction
        rc = sqlite3_exec(handle, "BEGIN TRANSACTION;", NULL, 0, &zErrMsg);
        if (rc != SQLITE_OK)
        {
            LOG_E(TAG, "Failed To Begin Transaction: %s", zErrMsg);
            sqlite3_free(zErrMsg);
            return status;
        }

        // The below operations are atomic. If any of the operation fails, the transaction will be rolled back.
        do
        {
            status = alter_db_add_column(handle, "VOD_ID", "TEXT DEFAULT 'bagheera_0_0_0'");
            if (!status) break;

            status = alter_db_add_column(handle, "PRIORITY", "INT DEFAULT 23 ");
            if (!status) break;

            status = alter_db_add_column(handle, "REQUEST_TIME", "INT DEFAULT 0");
            if (!status) break;

            status = alter_db_add_column(handle, "VOD_STATUS", "INT DEFAULT 0");
            if (!status) break;

            status = alter_db_add_column(handle, "ACK_TIME", "INT DEFAULT 0");
            if (!status) break;

            status = alter_db_add_column(handle, "IS_CANCELLED", "INT DEFAULT 0");
            if (!status) break;

            status = alter_db_add_column(handle, "FAILURE_REASON", "TEXT DEFAULT 'NA'");
            if (!status) break;

            status = alter_db_add_column(handle, "RGB_STATUS", "TEXT DEFAULT 'NA'");
            if (!status) break;

            rc = sqlite3_exec(handle, "PRAGMA user_version = 1;", nullptr, 0, nullptr);

            if (rc != SQLITE_OK) {
                LOG_E(TAG, "Failed To Set user_version: %s", sqlite3_errmsg(handle));
                status = false;
                break;
            }
            LOG_I(TAG, "db Table Altered: EXTCAM");

            status = true;
        } while (false);

        if (status)
        {
            // Commit the transaction
            rc = sqlite3_exec(handle, "COMMIT TRANSACTION;", NULL, 0, &zErrMsg);
            if (rc != SQLITE_OK)
            {
                LOG_E(TAG, "Failed To Commit Transaction: %s", zErrMsg);
                status = false;
                sqlite3_free(zErrMsg);
            }
        }
        if (!status)
        {
            // Rollback the transaction
            LOG_E(TAG, "Rollback:: Failed to alter EXTCAM DB V1");
            rc = sqlite3_exec(handle, "ROLLBACK TRANSACTION;", NULL, 0, &zErrMsg);
            if (rc != SQLITE_OK)
            {
                LOG_E(TAG, "Failed to rollback transaction: %s", zErrMsg);
                sqlite3_free(zErrMsg);
            }
            string str_msg = "Failed To Alter EXTCAM DB V1";
            nd_service_obj->send_err_msg(SM_E_EXTCAM_DB_V1_ALTER_FAIL, curr_speed, str_msg);
        }
    }
    else if(currentVersion == 1)
    {
        LOG_I(TAG,"Already In Latest Db Version");
        status = true;
    }

    return status;
}

void print_req_queue()
{
    priority_queue<ext_request_details, vector<ext_request_details>, compare> q2= pq_ext_req;  
    while (!q2.empty()) {  
        LOG_I(TAG,"element file name is %s, priority is %d, request_time is %llu", q2.top().filename.c_str(),q2.top().priority,q2.top().request_time);  
        q2.pop();
    } 
}

bool delete_cancelled_requests() 
{
    pthread_mutex_lock(&ext_cam_db_mutex);
    string sql_stmt = "DELETE FROM EXTCAM WHERE IS_CANCELLED = 1;";
    char* zErrMsg = nullptr;
    int rc = sqlite3_exec(db_handle, sql_stmt.c_str(), nullptr, 0, &zErrMsg);
    if (rc != SQLITE_OK) {
        LOG_E(TAG, "Failed to delete cancelled requests: %s", zErrMsg);
        sqlite3_free(zErrMsg);
        pthread_mutex_unlock(&ext_cam_db_mutex);
        return false;
    }
    LOG_I(TAG, "Cancelled requests deleted successfully");
    pthread_mutex_unlock(&ext_cam_db_mutex);
    return true;
}

bool get_pending_requests()
{
    vector<ext_request_details> pending_list;
    sqlite3_stmt *stmt;
    int rc = 0;
    int index_id = -1;
    int ext_cam_db_counter = 0;
    int no_of_rows;
    get_row_count_from_db(no_of_rows);
    pthread_mutex_lock(&ext_cam_db_mutex);
    stringstream sql_stmt;
    sql_stmt << "SELECT * FROM EXTCAM";
    string sql_stmt_str = sql_stmt.str();
    rc = sqlite3_prepare_v2(db_handle, sql_stmt_str.c_str(), -1, &stmt, NULL);

    bool ret = false;
    do {
        if (rc != SQLITE_OK)
        {
            LOG_E(TAG,"Failed to prepare statement: %s",sqlite3_errmsg(db_handle));
            break;
        }
        ret = true;
        LOG_I(TAG,"No of Rows in EXTCAM DB is %d, Loading EXTCAM DB To List",no_of_rows);
        while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
        {

            if( ext_cam_db_counter > no_of_rows)
            {
                LOG_E(TAG,"Iteration Are Exceeding DB Count, Stopping DB Iteration");
                break;
            }
            ext_request_details pend_req;
            index_id = sqlite3_column_int(stmt, 0);
            pend_req.cam_num = sqlite3_column_int(stmt, 1);
            pend_req.filename = string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));
            pend_req.rec_start_time = sqlite3_column_int64(stmt, 3);
            pend_req.rec_end_time = sqlite3_column_int64(stmt, 4);
            pend_req.framerate = sqlite3_column_int(stmt, 5);
            pend_req.ch_num = sqlite3_column_int(stmt, 6);
            pend_req.audio_enable = sqlite3_column_int(stmt, 8);
            pend_req.udid = sqlite3_column_int(stmt, 9);
            pend_req.session_count = sqlite3_column_int(stmt, 10);
            pend_req.periodic_rgb_analysis = (bool)sqlite3_column_int(stmt,11);
            pend_req.vod_id = string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 12)));;
            pend_req.priority = static_cast<upload_level_t>(sqlite3_column_int(stmt,13));
            pend_req.request_time = static_cast<uint64_t>(sqlite3_column_int64(stmt,14));
            pend_req.vod_status = static_cast<ext_vod_status_t>(sqlite3_column_int(stmt,15));
            pend_req.ack_time = sqlite3_column_int(stmt,16);
            pend_req.is_cancelled = sqlite3_column_int(stmt,17);
            pend_req.failure_reason = string(reinterpret_cast<const char*>(sqlite3_column_text(stmt,18)));
            pend_req.rgb_status = string(reinterpret_cast<const char*>(sqlite3_column_text(stmt,19)));
            ext_cam_db_counter++;

            if(pend_req.is_cancelled)
            {
               continue;
            }

            if(pend_req.vod_status == EXT_VOD_SUCCESS)
            {
                // Already completely processed, skip entirely
                continue;
            }
            
            if(pend_req.vod_status == EXT_VOD_FETCHED_UPLOADER_NOTIFY_FAILED)
            {
                // Video was fetched but uploader wasn't notified, try to send response
                if(send_fetch_response_uploader(pend_req))
                {
                    // Successfully sent response, mark as complete using existing function
                    update_vod_status_to_success(index_id, pend_req);
                }
                // Whether successful or not, skip adding to pending list
                continue;
            }
            
            LOG_D(TAG,"get_pending_req: filename is :%s request_time is :%llu", pend_req.filename.c_str(), pend_req.request_time);
            pending_list.push_back(pend_req);
        }
        if (rc != SQLITE_DONE)
        {
            LOG_E(TAG,"Error get_pending_req :%s",sqlite3_errmsg(db_handle));
            ret = false;
        }
    }while(false);
    
    LOG_I(TAG,"Done Loading EXTCAM DB To List");
    pthread_mutex_unlock(&ext_cam_db_mutex);

    if(ret)
    {
        for(unsigned i = 0; i < pending_list.size(); i++) {
            LOG_D(TAG,"Adding Pending Request To Queue: %s, request_time %llu",pending_list[i].filename.c_str(), pending_list[i].request_time);
            //pq_ext_req.push(pending_list[i]);
            add_pend_req_pq(pending_list[i]);
        }
    }
    if(stmt)
    {
        sqlite3_finalize(stmt);
    }
    return ret;

}

bool check_channel_enabled()
{
    bool cam1_enabled, cam2_enabled, cam3_enabled, cam4_enabled, audio_enabled;
    int framerate;
    read_ext_cam_ch1_config(cam1_enabled, framerate, audio_enabled);
    channels_enabled_config[0] = cam1_enabled;
    ext_cam_frame_rate[0] = framerate;
    ext_cam_audio_enabled[0] = audio_enabled;

    read_ext_cam_ch2_config(cam2_enabled, framerate, audio_enabled);
    channels_enabled_config[1] = cam2_enabled;
    ext_cam_frame_rate[1] = framerate;
    ext_cam_audio_enabled[1] = audio_enabled;

    read_ext_cam_ch3_config(cam3_enabled, framerate, audio_enabled);
    channels_enabled_config[2] = cam3_enabled;
    ext_cam_frame_rate[2] = framerate;
    ext_cam_audio_enabled[2] = audio_enabled;

    read_ext_cam_ch4_config(cam4_enabled, framerate, audio_enabled);
    channels_enabled_config[3] = cam4_enabled;
    ext_cam_frame_rate[3] = framerate;
    ext_cam_audio_enabled[3] = audio_enabled;

    if(cam1_enabled || cam2_enabled || cam3_enabled || cam4_enabled)
    {
        return true;
    }
    return false;
}

bool send_msg_toggle_wifi_mode(wifi_mode_t wifi_mode, bool persist_mode = false)
{
    toggle_wifi_mode_t req_wifi_toggle;
    req_wifi_toggle.mode = wifi_mode;
    req_wifi_toggle.persist_mode = persist_mode;

    if( false == send_msg( (generic_msg_t *)&req_wifi_toggle, REQ_TOGGLE_DRIVERI_WIFI_MODE, sizeof(req_wifi_toggle), get_msgq_name(), Q_WIFI_MGR, msg_idx++ ) ) 
    {
        LOG_E(TAG, "Not Able To Send Toggle Wifi Message");
    }
    else
    {   
        LOG_I(TAG, "Successfully Sent Toggle Wifi Message");
    }
}

bool init_config(void) {

    CIRCULAR_BUFFER_PATH = nd_device_obj->get_external_eMMC_mount_path();
    return true;

}

void check_and_request_ignition_status()
{
    bool isPowerServiceAvail = check_for_service(Q_POWER);
    if(isPowerServiceAvail)
    {
        if( req_ignition_status())
        {
            LOG_E (TAG,"Request For Ignition Status Sent");
        }
        else
        {
            LOG_I (TAG,"Request For Ignition Status Falied");
        }
    }
    else
    {
        LOG_E(TAG,"Power Monitor Service Is Not Available");
    }
}

bool send_msg_to_set_dhub_time()
{
    generic_msg_t req_to_set_dhub_time;
    if (!send_msg (&req_to_set_dhub_time, REQ_MDVR_TIME_SET, sizeof (req_to_set_dhub_time), get_msgq_name(), get_msgq_name(), msg_idx++)) {
        LOG_E (TAG,"Failed to Send Request Message To Set DHUB Time");
        return false;
    }
    return true;
}

void* rgb_thread(void *ptr)
{
    LOG_I(TAG,"RGB Analysis Thread - Started");
    
    int minute_counter = 0;
    bool fifth_minute_rgb_done = false;
    bool firmware_supports_api = false;
    
    int64_t rec_start_time, rec_end_time;
    int ch_num, cam_num, framerate, audio_enable;
    long pull_time;
    rgb_err rgb_status;
    
    while(1)
    {
        sleep(60);  // Sleep 1 minute
        
        if(!isMDVRConnected())
        {
            LOG_D(TAG,"DHUB Not Connected - Resetting counter");
            minute_counter = 0;
            fifth_minute_rgb_done = false;
            continue;
        }
        
        minute_counter++;
        
        // Check firmware capability
        firmware_supports_api = isNewFirmwareSupported();
        
        // Handle scheduled RGB analysis
        if(minute_counter == 5 && !fifth_minute_rgb_done)
        {
            LOG_I(TAG,"Performing RGB Analysis For All Cameras");
            
            bool rgb_analysis_status[4] = {false, false, false, false};
            for(int i=0; i < 4; i++)
            {
                if(channels_enabled_config[i])
                {
                    stream_file_resp_t res = TCP_COMM_ERROR;
                    cam_num = i+4;
                    ch_num = i+1;
                    LOG_I(TAG,"Going To Analyze RGB For Cam %d",cam_num);
                    rec_start_time = get_system_time() - (ONE_MINUTE_DURATION_MS * 2); 
                    rec_end_time = get_system_time() - (ONE_MINUTE_DURATION_MS);
                    string mp4_filename = "/home/ubuntu/RGB_Check_Cam_" + to_string(cam_num) + "_" + to_string(rec_start_time) + "_" + to_string(rec_end_time) +".mp4";
                    framerate = ext_cam_frame_rate[i];
                    audio_enable = ext_cam_audio_enabled[i];
                    res = stream_saved_file(mp4_filename, rec_start_time, rec_end_time, ch_num,
                            framerate, (bool)audio_enable, false, pull_time, rgb_status);
                    if(res == FILE_DOWNLOAD_SUCCESS || res == FILE_DURATION_CHECK_FAIL || res == FILE_TOO_BIG)
                    {
                        rgb_err analysis_result = video_rgb_analyser(mp4_filename, ch_num);
                        rgb_analysis_status[i] = true;
                    
                    }
                    file_delete(mp4_filename);
                }
            }
            bool rgb_analysis_done = true;
            for(int i=0; i < 4; i++)
            {
                if(channels_enabled_config[i] && !rgb_analysis_status[i])
                {
                    LOG_I(TAG,"RGB Analysis Is Not Yet Done For Cam %d", i+4);
                    rgb_analysis_done = false;
                    break;
                }
            }
            if(rgb_analysis_done)
            {
                if(ext_cam_channels_enabled)
                {
                    LOG_I(TAG,"RGB Analysis Is Done");
                    set_rgb_analysis_status(1);
                }
                else
                {
                    LOG_E(TAG,"Not Doing RGB Analysis As No Channels Are Enabled");
                }
                fifth_minute_rgb_done = true;
            }
            else
            {
                LOG_E(TAG,"RGB Analysis NOT completed for all cameras. Will retry next cycle.");
                // Do not mark fifth_minute_rgb_done, so it will retry
            }
            continue;
        }
        
        // Handle new firmware (API support)
        if(firmware_supports_api)
        {
            if(minute_counter < 5)
            {
                // Minutes 1-4: Full API update (overwrite everything)
                LOG_D(TAG,"Minute %d - Updating Camera Status via API (New Firmware)", minute_counter);
                update_cam_status_with_api(false);
            }
            else if(minute_counter > 5)
            {
                // Minutes 6+: Selective API update (preserve scheduled RGB analysis status unless disconnected)
                LOG_D(TAG,"Minute %d - Selective Camera Status Update via API (New Firmware)", minute_counter);
                update_cam_status_with_api(true);
            }
        }
        // Handle old firmware (no API support)
        else
        {
            // For old firmware, only scheduled RGB analysis update happens, then status stays frozen
            if(minute_counter < 5)
            {
                LOG_D(TAG,"Minute %d - Waiting for RGB analysis (Old Firmware)", minute_counter);
            }
            else if(minute_counter > 5)
            {
                LOG_D(TAG,"Minute %d - Camera status frozen (Old Firmware)", minute_counter);
            }
        }
    }
    
    return NULL;
}

bool send_msg_reg_wifi_updates()
{
    generic_msg_t wifi_reg;

    if( false == send_msg( &wifi_reg, REQ_WIFI_REG, sizeof(wifi_reg), get_msgq_name(), Q_WIFI_MGR, msg_idx++ )) 
    {
        LOG_E(TAG, "Not Able To Send Message To Register For Wifi Updates");
    }
    else
    {   
        LOG_I(TAG, "Successfully Sent Message To Register For Wifi Update");
    }
}

bool send_msg_get_dhub_wifi_mode_db()
{
    generic_msg_t dhub_wifi_mode_msg;

    for(int i=0; i < NUM_DHUB_WIFI_MODE_CLIENTS; i++ )
    {
        if( false == send_msg(&dhub_wifi_mode_msg, REQ_DHUB_WIFI_MODE_FROM_DB, sizeof(dhub_wifi_mode_msg), get_msgq_name(), DHUB_WIFI_MODE_CLIENTS[i], 0) )
        {
            LOG_E(TAG, "Not Able To Send Get DHUB WIFI Mode From DB Message");
        }
        else
        {
            LOG_I(TAG, "Successfully Sent Get DHUB WIFI Mode From DB Message");
        }
    }
    return true;
}

void init_file_dld_thread_active() {
    file_dld_thread_active = new bool[MAX_NUM_THREADS];
    for (int i = 0; i < MAX_NUM_THREADS; i++) {
        file_dld_thread_active[i] = false;
    }
}

void* monitor_installer_status (void *args)
{
    int64_t pair_time;
    while(1)
    {
        if(get_installer_pair_time(pair_time))
        {
            if(check_installer_deactivated(pair_time))
            {
                remove_installer_accessory();
            }
        }

        // Check and remove DHUB config update block if speed >= 5 mph
        check_and_remove_dhub_config_block_if_needed();

        sleep(60);
    }
}

int main(int argc, char *argv[]) {

    nd_service_obj = NDService::get_service_obj(TAG); 
    nd_log_init (log_dir.c_str());
    Timer AppTimer;
#ifdef ROUTE_LOGS
    route_logs( log_dir.c_str() );
 #endif

    LOG_I(TAG,"**********Starting EXT CAM Service**********");

    int uptime = get_system_monotonic_time()/1000;
    LOG_I(TAG,"System uptime is %d",uptime);
    if( uptime < 60 )
    {
        LOG_I(TAG,"Deleting old bin and MP4 files");
        string cmd_bin = "rm -rf /home/iriscli/files/[4-7]*.bin";
        system_execute("FILE_CLEAN",cmd_bin);
        string cmd_mp4 = "rm -rf /home/iriscli/files/[4-7]*.mp4";
        system_execute("FILE_CLEAN",cmd_mp4);
    }

    nd_device_obj_init();
    init_config();
    set_report_config_mismatch_enabled(true);
    set_nd_service_ext_object(nd_service_obj);
    // Sets the ND device external object to enable communication with the DHUB firmware API.
    // This is a critical step in initializing the device for external camera operations.
    set_nd_device_ext_object(nd_device_obj);
    is_automation_enabled = isAutomationEnabled();
    int ext_cam_rgb_period;
    read_ext_camera_common_config(ext_cam_feature_enabled);
    read_ext_cam_rgb_analysis_timer_config(ext_cam_rgb_period);
    ext_cam_channels_enabled = check_channel_enabled();
    read_rgb_analysis_config(rgb_analysis);
    read_dhub_auto_configuration(dhub_auto_config);

    check_and_rectify_extcam_config_db(nd_service_obj, curr_speed);
    nd::device::ext_cam_ns::manageDHUB_ExtCam_ConfigDB();
    read_installer_mode_max_time_config();

    if(ext_cam_channels_enabled || ext_cam_feature_enabled)
    {
        if(!ext_cam_feature_enabled)
        {
            LOG_I(TAG, "Ext Cam Feature Disabled");
            if(ext_cam_channels_enabled)
            {
                nd_service_obj->send_err_msg(SM_E_EXT_CAM_FEATURE_DISABLED_CH_ENABLED, curr_speed, "Ext Cam Feature Disabled - Channels Enabled");
            }
        }
        else
        {
            if(!ext_cam_channels_enabled)
            {
                nd_service_obj->send_err_msg(SM_E_EXT_CAM_FEATURE_ENABLED_CH_DISABLED, curr_speed, "Ext Cam Feature Enabled - Channels Disabled");
            }
        }
   }
   else
    {
        if(stop_service("ext_cam.service"))
        {  /* I Dont Come Here As the Service is already Stopped*/
            LOG_I(TAG, "Ext Cam Service Is Stopped Successfully");
        }
        return 0;
    }

    int64_t fspace = file_getfreespace("/");
    LOG_I(TAG, "Free Space : %lld", fspace);

    if( fspace < MIN_FREE_SPACE ) {
        LOG_E(TAG, "Free Space %lld is less than %ld. Exiting From main", fspace, MIN_FREE_SPACE);
        return 1;
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

    read_ext_cam_time_zone_hours(time_zone);
    set_time_zone(time_zone);
    LOG_I(TAG, "time_zone for mdvr video osd: %d", time_zone);

    // read the time offset from config that will add to start and end times during request
    read_ext_camera_video_request_offset_config(time_offset_msecs);
    read_ext_camera_file_empty_retry_count_config(max_retry_file_empty);

    read_ext_camera_video_encryption_config(video_encryption);

    //read_save_ext_camera_session_files_in_dhub_config(save_ext_camera_session_files_in_dhub);
    read_mdvr_firmware_upgrade_config(mdvr_firmware_upgrade, mdvr_firmware_upgrade_speed);
    read_save_ext_camera_files_in_dhub_config(save_ext_camera_files_in_dhub);
    read_mdvr_low_power_wakeup_config(mdvr_low_power_wakeup);
    read_ext_camera_video_request_duration(ext_cam_video_duration_request_ms);
    read_max_db_limit_config(MAX_DB_LIMIT);
    read_max_num_thread_config(MAX_NUM_THREADS);
    read_dhub_server_ip_config(dhub_server_ip);
    init_file_dld_thread_active();   

    read_iosix_config(iosix_enabled);
    readDHUBSTATimeout(dhub_sta_timeout);
    DHUB_HW_VER = getDHUBHWVer();    

    read_ext_camera_upload_video_config(upload_ext_cam_video);

    read_message_socket_config();
    init_mdvr_file_response_count();
    init_mdvr_status();
    get_driveri_hotspot_credentials(hotspot_ssid, hotspot_password);
    pthread_t ext_cam_th, maintainance_th, file_dld_th[MAX_NUM_THREADS], health_stats_th, auto_config_th, rgb_anlysis_th, ext_cam_vod_th, monitor_installer_th;
    int thread_idx, i;
    thread_param_t th_param;
    char *zErrMsg = 0;

    bool ret;
    string db_path_file = nd_device_obj->get_db_base_path() + "/" + DBFILE_NAME;
    int set_dhub_sta_fail_count = 0;

    while (1) {
        ret = open_db(db_path_file.c_str(), &db_handle);
        if(ret == true)
        {
            LOG_I(TAG, "success in open DB");
            if(!check_integrity_db())
            {
                LOG_C(TAG, "Possible Corruption Of DB");
                nd_service_obj->send_err_msg(SM_E_EXTCAM_DB_CORRUPTED, curr_speed,
                        "Ext Cam DB Corrupted");
                if(file_delete(db_path_file) == false)
                {
                    LOG_C(TAG, "Failed to delete db file");
                }
                continue;
            }
            ret = create_table_db(db_handle);
            if(ret == false)
            {
                LOG_C(TAG, "Failed to create_table_db");
                nd_service_obj->send_err_msg(SM_E_EXTCAM_DB_CREATE_FAIL, curr_speed,
                        "Ext Cam DB Table Creation Fail");
                if(file_delete(db_path_file) == false)
                {
                    LOG_C(TAG, "Failed to delete db file");
                }
            }

            else
            {
                LOG_I(TAG, "success in create_table_db");
                if(handle_db_versioning_and_column_addition(db_handle))
                {
                    break;
                }
                continue;
            }
        }

        else
        {
            nd_service_obj->send_err_msg(SM_E_EXTCAM_DB_OPEN_FAIL, curr_speed,
                                        "Ext Cam DB Open Fail");
            LOG_C(TAG, "Failed to open DB");
            if(file_delete(db_path_file) == false)
            {
                LOG_C(TAG, "Failed to delete db file");
            }
        }

        sleep(SLEEP_DURATION_DEFAULT); 
    }

    check_and_request_ignition_status();

    delete_cancelled_requests();
    
    // Add VOD cleanup at boot
    if (!cleanup_old_vods_at_boot()) {
        LOG_E(TAG, "VOD cleanup at boot failed, but continuing with service startup");
        nd_service_obj->send_err_msg(SM_E_EXTCAM_CLEANUP_FAIL, curr_speed, "VOD cleanup at boot failed");
    }
    
    get_pending_requests();

#ifdef DHUB_VOD_TEST
    print_req_queue();
#endif

    // Thread for Driveri Hub Time Sync With  Driver-i
    if ((pthread_create (&maintainance_th, NULL, extcam_maintainance_thread, NULL)) != 0 ) {
        LOG_E(TAG, "Can't create  D-HUB Maintainance thread");
    }
    // Thread for external camera msg handler  
    if ( (pthread_create (&ext_cam_th, NULL, ext_camera_msg_handler, NULL)) != 0 ) {
        LOG_E(TAG, "Can't create extrenal camera message handler thread");
    }
    if ((pthread_create (&health_stats_th, NULL, get_mdvr_health_stats ,NULL)) != 0 ) {
        LOG_E(TAG, "Can't create  mdvr health stats thread");
    }

    if ((pthread_create (&auto_config_th, NULL, extcam_auto_configure_and_speed_mon_thread, NULL)) != 0 ) {
        LOG_E(TAG, "Can't create  D-HUB extcam_auto_configure_thread thread");
    }
 
    if ((pthread_create (&rgb_anlysis_th, NULL, rgb_thread, NULL)) != 0 ) {
        LOG_E(TAG, "Can't create  D-HUB extcam_rgb thread");
    }

    if ( (pthread_create (&monitor_installer_th, NULL, monitor_installer_status, NULL)) != 0 ) 
    {
       LOG_E(TAG, "Can't create monitor installer status thread");
    }


    // Update all the states of db rows to init before we start
    string update_stmt = "UPDATE EXTCAM SET STATE='INIT' WHERE STATE='PROCESS'";
    if(exec_cmd_db(update_stmt, NULL, 0, &zErrMsg) == true) {
        LOG_I(TAG, "Updated state of all rows to init in db");
    }

    //Check And Raise Alert If DB Count Crossed 2000
    int num_rows = 0;
    get_row_count_from_db(num_rows);
    if(num_rows >= 2000)
    {
        LOG_I(TAG,"DB Limit 2000 Reached");
        string message;
        message = " DB Limit 2000 Reached";
        nd_service_obj->send_err_msg(SM_E_EXT_CAM_DB_LIMIT, curr_speed, message);
    }

    setCurrDHUBWifiModeFromDB();
    wifi_mode_t curr_dhub_wifi_mode =  getCurrDHUBWifiModeFromDB();
    bool isWifiServiceAvail = check_for_service(Q_WIFI_MGR);
    if(isWifiServiceAvail)
    {
        send_msg_reg_wifi_updates();
    }
   // Keep monitoring db for files to pull
    while (1) {

        if(isMDVRConnected())
        {  
#ifdef AUTOMATION
                if(is_automation_enabled && !file_is_present(dhub_connection_file))
                {
                    file_touch(dhub_connection_file);
                }
#endif
            if(!report_mdvr_info_to_cloud)
            {
                string message;
                if(get_mdvr_status_info(message))
                {
                   // nd_service_obj->send_err_msg(SM_E_EXT_CAM_SERVICE_INFO, 1, message);
                    report_mdvr_info_to_cloud = true;

                    string disk_16GB = "SD1:14GB";

                    int hwVersion;
                    identifyHardwareVersion(hwVersion);
                    updateDHUBHWVerInDB(hwVersion);
                    if(hwVersion == MDVR_GEN3_HW_VERSION)
                    {
                        if(!save_ext_camera_files_in_dhub)
                        {
                            nd_service_obj->send_err_msg(SM_E_EXT_CAM_DHUBX_PULLING_VIDEOS, curr_speed, "DHUB-X Is Pulling Videos");
                        }
                    }

                    if(save_ext_camera_files_in_dhub && (message.find(disk_16GB) != string::npos))
                    {
                        LOG_I(TAG, "16GB Disk Detected - Forcing save_ext_cam_files_in_dhub = false");
                        save_ext_camera_files_in_dhub = false;

                        //nd_service_obj->send_err_msg(SM_E_EXT_CAM_16GB_SD_CARD, curr_speed, "Keeping Session Files In Driveri - 16GB Disk Detected");
                    }

                    string disk_0GB = "SD1:0GB";
                    if((message.find(disk_0GB) != string::npos))
                    {
                        //nd_service_obj->send_err_msg(SM_E_EXT_CAM_SD_CARD_ERR, curr_speed, "0GB SD Card");
                    }
                }
                else
                {
                    LOG_I(TAG,"Failed To Get DHUB Status Info, Retrying..");
                }
            }
            if(!log_mdvr_rec_setup_config)
            {
                mdvr_record_setup_t mdvr_rec_set;
                get_mdvr_rec_setup(mdvr_rec_set);
                LOG_I(TAG, "=================== MDVR RECORD SETUP =====================");
                if(mdvr_rec_set.tv_sys != "")
                {
                    LOG_I(TAG, "MDVR TV System       = %s", mdvr_rec_set.tv_sys.c_str());
                }
                else
                {
                    LOG_I(TAG, "Unable To Get MDVR TV System");
                    log_mdvr_rec_setup_config = false;
                }
                if(mdvr_rec_set.cam_type != "")
                {
                    LOG_I(TAG, "MDVR Camera Type     = %s", mdvr_rec_set.cam_type.c_str());
                }
                else
                {
                    LOG_I(TAG, "Unable To Get MDVR Camera Type");
                    log_mdvr_rec_setup_config = false;
                }
                if(mdvr_rec_set.fps != "")
                {
                    LOG_I(TAG, "MDVR Cameras FPS     = %s", mdvr_rec_set.fps.c_str());
                }
                else
                {
                    LOG_I(TAG, "Unable To Get Cameras FPS");
                    log_mdvr_rec_setup_config = false;
                }
                if(mdvr_rec_set.audio != "")
                {
                    LOG_I(TAG, "MDVR Cameras Audio   = %s", mdvr_rec_set.audio.c_str());
                }
                else
                {
                    LOG_I(TAG, "Unable To Get MDVR Cameras Audio");
                    log_mdvr_rec_setup_config = false;
                }
                if(mdvr_rec_set.quality != "")
                {
                    LOG_I(TAG, "MDVR Cameras Quality = %s", mdvr_rec_set.quality.c_str());
                }
                else
                {
                    LOG_I(TAG, "Unable To Get MDVR Cameras Quality");
                    log_mdvr_rec_setup_config = false;

                }
                if(mdvr_rec_set.mirror != "")
                {
                    LOG_I(TAG, "MDVR Cameras Mirror  = %s", mdvr_rec_set.mirror.c_str());
                }
                else
                {
                    LOG_I(TAG, "Unable To Get MDVR Cameras Mirror");
                    log_mdvr_rec_setup_config =false;
                }
                if( mdvr_rec_set.gop != "")
                {
                    LOG_I(TAG, "MDVR Cameras GOP     = %s", mdvr_rec_set.gop.c_str());
                }
                else
                {
                    LOG_I(TAG, "Unable To Get MDVR Cameras GOP");
                    log_mdvr_rec_setup_config = false;
                }
                LOG_I(TAG, "============================================================");
                log_mdvr_rec_setup_config = true;
            }
            if(!dhub_time_set)
            {
                if(send_msg_to_set_dhub_time())
                {   
                    LOG_I(TAG,"Sent Message To Set DHUB Time");
                    dhub_time_set = true;
                }
                else
                {
                    LOG_I(TAG,"Failed To Send Message To Set DHUB Time, Retrying ..");
                }
            }
            if(set_dhub_to_sta_mode)
            {
                curr_dhub_wifi_mode =  getCurrDHUBWifiModeFromDB();
                if(curr_dhub_wifi_mode == eAPMode)
                {
                    if(isDHUBCoexistanceSupported())
                    {   
                        if(setDHUBWifiMode(dhub_sta_timeout))
                        {
                            reboot_mdvr();
                            // Adding a sleep of 5 seconds before sending the "DHUB is in STA mode" message to wifi_mgr.
                            // This prevents issues where the DHUB hotspot is still detected in a Wi-Fi scan during passive scanning
                            // immediately after switching DHUB to STA mode.
                            sleep(SLEEP_DURATION_MDVR_SYNC);
                            set_dhub_sta_fail_count = 0;
                            LOG_I(TAG,"Set DHUB Mode To STA Mode");
                            updateDHUBWifiModeInDB(eSTAMode);
                            setCurrDHUBWifiModeFromDB();
                            send_msg_get_dhub_wifi_mode_db();
                            set_dhub_sta_fail_count = 0;
                            if(send_msg_toggle_wifi_mode(eAPMode))
                            {
                               set_dhub_to_sta_mode = false;
                            }
                        }
                        else
                        {
                            set_dhub_sta_fail_count++; 
                            LOG_E(TAG,"Failed To Configure DHUB To STA Mode, Retry Count : %d", set_dhub_sta_fail_count);
                            if( (set_dhub_sta_fail_count >= max_retries_to_set_dhub_sta_mode))
                            {
                                LOG_E(TAG,"Reached Max Retry Count To Configure DHUB To STA Mode");
                                if(send_msg_toggle_wifi_mode(eAPMode))
                                {
                                    set_dhub_to_sta_mode = false;
                                }
                            }
                        }
                    }
                    else
                    {
                        LOG_I(TAG, "Not Sending Toggle Wifi Message As DHUB Station Mode Is Not Yet Supported");
                        if(iosix_enabled)
                        {
                            int hwVersion;
                            identifyHardwareVersion(hwVersion);
                            if(hwVersion == MDVR_GEN2_HW_VERSION)
                            {
                                if(send_msg_toggle_wifi_mode(eAPMode, true))
                                {
                                    nd_service_obj->send_err_msg(SM_E_EXT_CAM_IOSIX_DHUB_GEN2, curr_speed, "VBUS Enabled, DHUB Not Supported");
                                    set_dhub_to_sta_mode = false;
                                }
                            }
                        }
                    }
                }
                else if(curr_dhub_wifi_mode == eSTAMode)
                {                        
                    LOG_I(TAG, "Not Sending Toggle Wifi Message As DHUB Is Already In Station Mode");
                    set_dhub_to_sta_mode = false;
                }
            }
            if(set_max_lpw_count_dhub)
            {
                if(setDHUBMaxLPW())
                {
                    set_max_lpw_count_dhub = false;
                }
                else
                {
                   LOG_I(TAG,"Failed To Set DHUB LPW, Reterying"); 
                }
            }
            if(set_dhub_login_details)
            {
                if(setDHUBLoginCredentials())
                {
                    set_dhub_login_details = false;
                }
                else
                {
                   LOG_I(TAG,"Failed To Set DHUB Login Details, Retrying"); 
                }
            }
            if (!mdvr_read_config) {
                if (get_mdvr_cam_enable_and_res_config()) {
                    LOG_I(TAG, "Got MDVR Camera Enable and Resolution Configuration");
                    mdvr_read_config = true;
                } else {
                    LOG_I(TAG, "Failed to Get MDVR Camera Enable and Resolution Configuration, Retrying ..");
                }
            }
#ifdef AUTOMATION
             if(is_automation_enabled)
            {
               if(file_is_present(set_record_config_file_path) )
                {
                   if(set_mstream_config_for_automation())
                    {   
                        get_mstream_details();
                        LOG_I(TAG, "Fetched mstream details.");
                        reboot_mdvr();
                        if(file_is_present(dhub_connection_file))
                        {
                            file_delete(dhub_connection_file);
                        }
                        LOG_I(TAG, "Rebooting MDVR after setting record config.");
                        file_delete(set_record_config_file_path);
                        LOG_I(TAG, "Deleted set_record config file: %s", set_record_config_file_path.c_str());
                    }
                    else
                    {   
                        LOG_E(TAG,"Failed to enter set_record_config.");
                    }
                }
                else
                {
                    LOG_I(TAG,"Set record config file is not present: %s", set_record_config_file_path.c_str());
                }
            }
#endif
            if(set_dhub_ap_mode)
            {
                if(get_installer_pair_time(pair_time))
                {
                    if(setDHUBWifiMode(0))
                    {
                        reboot_mdvr();
                        set_dhub_ap_mode = false;
                        LOG_I(TAG, "Successfully Set DHUB To AP Mode");
                    }
                    else
                    {
                        LOG_I(TAG, "Failed Set DHUB To AP Mode, Reteying..");
                    }

                }
                else
                {
                    LOG_I(TAG, "Not Setting DHUB To AP Mode as Installer Mode Is No Longer Active");
                    set_dhub_ap_mode = false;
                }
            }
            if(DHUB_recovery_flag && (check_firmware_upgrade_required() == MDVR_STABLE_SW_VERSION_DETECTED) && file_is_present(record_config_file_path))
            {
                if(!verify_rec_config())
                {    
                    if(set_mdvr_rec_config())
                    {
                        sleep(SLEEP_DURATION_MDVR_SYNC);
                        reboot_mdvr();
                        remove(record_config_file_path.c_str());
                    }
                }
                else
                {
                    LOG_I(TAG,"Configs In DHUB Are Correct, Removing Backup File %s",record_config_file_path.c_str());
                    remove(record_config_file_path.c_str());
                }
            }
            if(check_dhub_disk_status)
            {
                int record = -1, disk_status = -1, disk_space = -1;
                bool disk_working = false;
                if(getDHUBDiskStatus(record, disk_status, disk_space))
                {
                    check_dhub_disk_status = false;
                    if(record == 0)
                    {
                        LOG_E(TAG,"DHUB Record Is False,Sending Message To DHUB To Enable Recording");
                        if(setDHUBDiskRecording(1))
                        {
                            sleep (SLEEP_DURATION_MDVR_SYNC);
                            reboot_mdvr();
                            LOG_I(TAG,"DHUB Recording Enabled Successfully");
                        }
                        else
                        {
                            LOG_E(TAG,"Failed To Enable DHUB Recording");
                        }
                         check_dhub_disk_status = true;
                    }
                    else if(disk_status == 0)
                    {
                        LOG_E(TAG,"DHUB Disk Status Is False");
                    }
                    else if(disk_space == 0)
                    {
                        LOG_E(TAG,"DHUB Disk Space Is Zero");
                    }
                    else
                    {
                        disk_working = true;
                        LOG_I(TAG,"DHUB Disk Is In Good State");
                    }
                    if(!disk_working && !report_dhub_disk_status)
                    {
                        LOG_E(TAG,"DHUB Disk Status Is Not Good");
                        string msg = "Disk Status Is Bad-Record:" + to_string(record) + ",Disk Status:" + to_string(disk_status) + ",Disk Space: " + to_string(disk_space);
                        nd_service_obj->send_err_msg(SM_E_EXT_CAM_DHUB_DISK_ERR, curr_speed, msg);
                        report_dhub_disk_status = true;
                    }
                }
            }
            if (DHUBFormatRequired()) 
            {
                if(format_mdvr())
                {
                    LOG_I(TAG,"DHUB Formatted Successfully");
                    string msg = "DHUB Disk Is Formatted";
                    nd_service_obj->send_err_msg(SM_E_EXT_CAM_DHUB_FORMAT, curr_speed, msg);
                    file_delete(format_dhub_file_path);
                }
                else
                {
                    LOG_E(TAG,"Failed To Format DHUB");
                }
            }
        }
        else
        {
            LOG_D(TAG, "MDVR Is Not Connected");
#ifdef AUTOMATION
            if(is_automation_enabled && file_is_present(dhub_connection_file))
            {
                file_delete(dhub_connection_file);
            }
#endif
            sleep(SLEEP_DURATION_DEFAULT);
            continue;
        }

         // Check if threads are active to take work
        for(i = 0; i < MAX_NUM_THREADS; i++) {
            if(file_dld_thread_active[i] == false) {
                break;
            }
        }

        // sleep and continue if no threads are active
        if(i == MAX_NUM_THREADS) {
            LOG_D(TAG, "All the threads are active");
            sleep(SLEEP_DURATION_DEFAULT);
            continue;
        }

        thread_idx = i;

        // sleep and continue if no rows found in db
        int index_id;
        ext_request_details req;
        if(get_req_and_index_id(req,index_id) == false) {
            sleep(SLEEP_DURATION_NO_ROWS);
            continue;
        }

        th_param.db_index_id = index_id;
        th_param.thread_index_id = thread_idx;
        th_param.req = req;
        // Initiate thread for file download
        if ( (pthread_create (&file_dld_th[thread_idx], NULL, pull_mdvr_file, (void *)&th_param)) != 0 ) {
            LOG_E(TAG, "Can't create pull mdvr file thread for idx: %d", thread_idx);
            sleep(SLEEP_DURATION_DEFAULT);
            continue;
        }

        // detach the thread after create
        int err = pthread_detach(file_dld_th[thread_idx]);
        if (err) {
            LOG_E(TAG, "Failed to detach thread with id: %d, error: %s", thread_idx, strerror(err));
            break;
        }

        set_thread_active_flag(thread_idx, true);
        // Update state of this row to process so that it will not be picked by other thread again  
        stringstream sql_stmt;
        sql_stmt << "UPDATE EXTCAM SET STATE='PROCESS' WHERE INDEXID=";
        sql_stmt << index_id;
        update_stmt = sql_stmt.str();
        // Update all the states to init before we start
        if(exec_cmd_db(update_stmt, NULL, 0, &zErrMsg) == false) {
            LOG_E(TAG, "Failed to update state to process");
            sleep(SLEEP_DURATION_DEFAULT);
            continue;
        }
        LOG_I(TAG, "Updated state of row with index id %d to process in db", index_id);
    }

    nd_service_obj->release_service_obj();
    delete[] file_dld_thread_active;
    return 0;
}

