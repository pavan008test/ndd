
/* Copyright (C) 2017 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Suresh Kumar Y <suresh.kumar@netradyne.com>, February 2017
 */

#include "circular_buffer.h"
#include <sqlite3.h> 
#include <stdio.h>
//#include <log.h>
#include <cstdlib>
#include <unistd.h>
#include "system_utils.h"
#include "service_utils.h"
#include "nd_prop_utils.h"
#include "nd_msg_types.h"
#include <nd_file_utils.h>
extern ND_DeviceFactory *nd_device_obj;
extern bool video_list_sent;
static const char *TAG="CB_SQL";
static const int NUM_CLOUD_NOTIFY_ENTRIES=500; //number of entries in the json string sent to the cloud
static string insert_str =
        "INSERT INTO VIDFILES (TIME,NAME,FILE_SIZE,TYPE,STATUS,CAM_TYPE,DURATION,UDID,SESSIONCOUNT,TRANSCODE_STATUS,UPL_VID_ENABLED,REC_VID_ENABLED) VALUES ";
static string insert_str_with_file_compression =
    "INSERT INTO VIDFILES (TIME,NAME,FILE_SIZE,TYPE,STATUS,CAM_TYPE,DURATION,UDID,SESSIONCOUNT,TRANSCODE_STATUS,FILE_COMPRESSION,UPL_VID_ENABLED,REC_VID_ENABLED) VALUES ";

extern circular_buffer_vid *CIRC_BUFF_ctx;
extern int64_t FILLING_LIMIT;
extern NDService *nd_service_obj;
static const string ld_extn  = ".ld.mp4";

int oldest_file_rotated_index = 0;
int max_alert_hq_file = 80;

#define BYTES_IN_MB 1024*1024
extern int64_t current_sessionCount;
extern int64_t current_udid;
extern bool deleteUnkownFileType ;
extern int64_t RESERVED_SPACE;
extern bool store_lq_dms_file;
extern int64_t num_hq_videos_max;

#define day_to_hours 24
#define MAX_FILE_DELETE_DRP 10
#define MAX_SIGN_CROP_FILE_DELETE_DRP 20
#define MAX_SIGN_CROP_PER_SESSION 10
#define MAX_SIGN_CROP_FILES_IN_CACHE 12*60*10 // (12hrs)*(60 sessions per hour)*(max 10 files per session)
#define hours_to_milliseconds 60*60*1000
#define MAX_NO_OF_DUPLICATE_ENTRIES_DB 2
#define MAX_NO_OF_CRITICAL_INFO 0
#define DMS_CAM_NO 8
static int no_of_critical_info_send = 0;
int64_t default_drp_clock_hours = 3*day_to_hours;  // default is 3 days
int64_t drp_clock_hours = default_drp_clock_hours ;
static const string INT_COMPLETE_OBS_PATH = "/home/ubuntu/.nddevice/observations/";
static char const outward_file_prefix = '0' ;
static const int max_file_deletion_drp = 100; //number of entries in the json string sent to the cloud
vector<circular_buffer_drp_t> signCropFiles;
vector<circular_buffer_drp_t> backlogSignCropFiles;
int sign_crop_file_idx = 0;
int callback_count_entry_available(void* data, int argc, char** argv, char** azColName) ;
// Called after cloud notify succeeds
bool update_status_xattr(int i, pair < vector< json_add_del >, vector< json_add_del > >& add_del_files);
// Called from update_file_DB for Privacy and file size update
bool write_updatefile_metadata_to_xattrs(const circular_buffer_fileinfo_t& fileinfo);

static const int64_t MIN_EMMC_FREE_SPACE = 0;
int64_t MAX_EMMC_FREE_SPACE = 512LL * 1024LL * 1024LL * 1024LL; // 512 GB assigned by default, this will updated according to card/emmc capacity
static const int64_t MIN_SAFE_OUTWARD_LD_FILES_D4XX = 100LL * 60LL; // 100 hrs of LD files, as each session is 1 min long
static const int64_t MIN_SAFE_OUTWARD_LD_FILES_D2XX = 50LL * 60LL; // 50 hrs of LD files, as each session is 1 min long
bool matching_file_list_info_from_xattrs(string filename_matching, vector<file_info_str>& file_data_list);

constexpr int MAX_MISC_INSERT_FAILED_FILES = 100; // Maximum number of files for which insert failure can be tracked to avoid memory issues. This is a safeguard and can be adjusted based on expected failure rates and memory constraints.
static vector<circular_buffer_fileinfo_t> misc_insert_failed_files; // This vector will store the fileinfo of misc files for which insert in DB failed.

/// call backs for sqlite start
static int callback_add_del(void *add_del, int argc, char **argv, char **azColName)
{
    json_add_del node;
    vector < json_add_del >* temp_add_del_vct = (vector< json_add_del >*) add_del;

    if( argc != 7 )
    {
        LOG_E(TAG, "Something wrong here in callback_add_del");
        return -1;
    }
    if(argv[0] != NULL ){
        if (!string_to_integer(argv[0], node.index_id)) {
            LOG_E(TAG, "failed in string_to_integer for index_id, continuing with index_id=0");
            node.index_id = 0;
        }
    }else{
        LOG_E(TAG, "index_id is NULL in callback_add_del");
    } 
    
    if(argv[1] != NULL)
        node.file_name = argv[1];
    else{
        node.file_name = "";
        LOG_E(TAG, "file_name is NULL in callback_add_del");
    }        
    
    if(argv[2] != NULL){
        if(!string_to_integer(argv[2], node.duration)){
            LOG_E(TAG, "failed in string_to_integer for duration, continuing with duration=0");
            node.duration = 0;
        }
    }else{
        LOG_E(TAG, "duration is NULL in callback_add_del");
    }

    int file_type = 0;
    if(argv[3] != NULL){
        if(!string_to_integer(argv[3], file_type)){
            LOG_E(TAG, "failed in string_to_integer for file_type, continuing with file_type=CIRCULAR_BUFFER_TYPE_NORMAL");
            node.file_type = CIRCULAR_BUFFER_TYPE_NORMAL;
        }    
        else {
            node.file_type = (circular_buffer_filetype_t)file_type;
        }
    } else {
        LOG_E(TAG, "file_type is NULL in callback_add_del");
    }

    if(argv[4] != NULL){
        if(!string_to_int64(argv[4], node.udid)){
            LOG_E(TAG, "failed in string_to_int64 for udid, continuing with udid=0");
            node.udid = 0;
        }
    } else {
        LOG_E(TAG, "udid is NULL in callback_add_del");
    }
    
    if(argv[5] != NULL){
        if(!string_to_int64(argv[5], node.sessionCount)){
            LOG_E(TAG, "failed in string_to_int64 for sessionCount, continuing with sessionCount=0");
            node.sessionCount = 0;
        }
    } else {
        LOG_E(TAG, "sessionCount is NULL in callback_add_del");
    }

    int alert_type = 0;
    if(argv[6] != NULL){
        if(!string_to_integer(argv[6], alert_type)){
            LOG_E(TAG, "failed in string_to_integer for alert_type, continuing with alert_type=CIRCULAR_BUFFER_MEDIUM_COMPRESSION");
            node.alert_type =CIRCULAR_BUFFER_MEDIUM_COMPRESSION;
        } else {
            node.alert_type = (circular_buffer_file_compression_t)alert_type;
        }
    } else {
        LOG_E(TAG, "alert_type is NULL in callback_add_del");
    }

    if(node.file_name != "")
        temp_add_del_vct->push_back(node);
    
    return 0;
}

static int callback_allfiles_db(void *allfiles, int argc, char **argv, char **azColName)
{
    file_data_str_db_t node = {0};
    vector < file_data_str_db_t >* temp_allfiles = (vector< file_data_str_db_t >*) allfiles;

    if( argc != 13 )
    {
        LOG_E(TAG, "Something wrong here in callback_allfiles_db");
        return -1;
    }
    // INDEXID, NAME, FILE_SIZE, TYPE, SESSIONCOUNT, UDID, TIME, REC_VID_ENABLED, UPL_VID_ENABLED, CAM_TYPE, TRANSCODE_STATUS
    if(argv[0] != NULL) {
        if (!string_to_integer(argv[0], node.file_data.index_id)) {
            LOG_E(TAG, "failed in string_to_integer for index_id, continuing with index_id=0");
            node.file_data.index_id = 0;
        }
    } else {
        LOG_E(TAG, "index_id is NULL in callback_allfiles_db");
        node.file_data.index_id = 0;
    }
    
    if(argv[1] != NULL) {
        node.file_data.file_name = argv[1];
    } else {
        node.file_data.file_name = "";
        LOG_E(TAG, "file_name is NULL in callback_allfiles_db");
    }
    
    if(argv[2] != NULL) {
        if(!string_to_int64(argv[2], node.file_data.file_size)) {
            LOG_E(TAG, "failed in string_to_int64 for file_size, continuing with file_size=0");
            node.file_data.file_size = 0;
        }
    } else {
        LOG_E(TAG, "file_size is NULL in callback_allfiles_db");
        node.file_data.file_size = 0;
    }
    
    int file_type = 0;
    if(argv[3] != NULL) {
        if(!string_to_integer(argv[3], file_type)) {
            LOG_E(TAG, "failed in string_to_integer for file_type, continuing with file_type=CIRCULAR_BUFFER_TYPE_NORMAL");
            node.file_data.file_type = CIRCULAR_BUFFER_TYPE_NORMAL;
        } else {
            node.file_data.file_type = (circular_buffer_filetype_t)file_type;
        }
    } else {
        LOG_E(TAG, "file_type is NULL in callback_allfiles_db");
        node.file_data.file_type = CIRCULAR_BUFFER_TYPE_NORMAL;
    }
    
    if(argv[4] != NULL) {
        if(!string_to_int64(argv[4], node.file_data.sessionCount)) {
            LOG_E(TAG, "failed in string_to_int64 for sessionCount, continuing with sessionCount=0");
            node.file_data.sessionCount = 0;
        }
    } else {
        LOG_E(TAG, "sessionCount is NULL in callback_allfiles_db");
        node.file_data.sessionCount = 0;
    }

    if(argv[5] != NULL) {
        if(!string_to_int64(argv[5], node.file_data.udid)) {
            LOG_E(TAG, "failed in string_to_int64 for udid, continuing with udid=0");
            node.file_data.udid = 0;
        }
    } else {
        LOG_E(TAG, "udid is NULL in callback_allfiles_db");
        node.file_data.udid = 0;
    }

    if(argv[6] != NULL) {
        if(!string_to_int64(argv[6], node.time)) {
            LOG_E(TAG, "failed in string_to_int64 for time, continuing with time=0");
            node.time = 0;
        }
    } else {
        LOG_E(TAG, "time is NULL in callback_allfiles_db");
        node.time = 0;
    }

    if(argv[7] != NULL) {
        if(!string_to_integer(argv[7], node.rec_vid_enabled)) {
            LOG_E(TAG, "failed in string_to_integer for rec_vid_enabled, continuing with rec_vid_enabled=1");
            node.rec_vid_enabled = DEFAULT_VALUE_REC_VID_ENABLED_COLUMN;
        }
    } else {
        LOG_E(TAG, "rec_vid_enabled is NULL in callback_allfiles_db");
        node.rec_vid_enabled = DEFAULT_VALUE_REC_VID_ENABLED_COLUMN;
    }

    if(argv[8] != NULL) {
        if(!string_to_integer(argv[8], node.upl_vid_enabled)) {
            LOG_E(TAG, "failed in string_to_integer for upl_vid_enabled, continuing with upl_vid_enabled=1");
            node.upl_vid_enabled = DEFAULT_VALUE_UPL_VID_ENABLED_COLUMN;
        }
    } else {
        LOG_E(TAG, "upl_vid_enabled is NULL in callback_allfiles_db");
        node.upl_vid_enabled = DEFAULT_VALUE_UPL_VID_ENABLED_COLUMN;
    }

    int cam_type = CIRCULAR_BUFFER_CAM_FRONT;
    if(argv[9] != NULL) {
        if(!string_to_integer(argv[9], cam_type)) {
            LOG_E(TAG, "failed in string_to_integer for cam_type, continuing with cam_type=CIRCULAR_BUFFER_CAM_FRONT");
            cam_type = CIRCULAR_BUFFER_CAM_FRONT;
        }
    } else {
        LOG_E(TAG, "cam_type is NULL in callback_allfiles_db");
    }
    node.camtype = (circular_buffer_camtype_t)cam_type;

    int tc_status = CIRCULAR_BUFFER_TC_STATUS_WAITING;
    if(argv[10] != NULL) {
        if(!string_to_integer(argv[10], tc_status)) {
            LOG_E(TAG, "failed in string_to_integer for transcode_status, continuing with transcode_status=CIRCULAR_BUFFER_TC_STATUS_WAITING");
            tc_status = CIRCULAR_BUFFER_TC_STATUS_WAITING;
        }
    } else {
        LOG_E(TAG, "transcode_status is NULL in callback_allfiles_db");
    }
    node.tc_status = (circular_buffer_transcode_status_t)tc_status;

    int cb_status = CIRCULAR_BUFFER_STATUS_NEW;
    if(argv[11] != NULL) {
        if(!string_to_integer(argv[11], cb_status)) {
            LOG_E(TAG, "failed in string_to_integer for cb_status, continuing with cb_status=CIRCULAR_BUFFER_STATUS_NEW");
            cb_status = CIRCULAR_BUFFER_STATUS_NEW;
        }
    } else {
        LOG_E(TAG, "cb_status is NULL in callback_allfiles_db");
    }
    node.status = (circular_buffer_filestatus_t)cb_status;

    int file_compression = CIRCULAR_BUFFER_MEDIUM_COMPRESSION;
    if(argv[12] != NULL) {
        if(!string_to_integer(argv[12], file_compression)) {
            LOG_E(TAG, "failed in string_to_integer for file_compression, continuing with file_compression=CIRCULAR_BUFFER_MEDIUM_COMPRESSION");
            file_compression = CIRCULAR_BUFFER_MEDIUM_COMPRESSION;
        }
    } else {
        LOG_E(TAG, "file_compression is NULL in callback_allfiles_db");
    }
    node.file_compression = (circular_buffer_file_compression_t)file_compression;

    if(node.file_data.file_name != "")
        temp_allfiles->push_back(node);
    
    return 0;
}


static int callback(void *NotUsed, int argc, char **argv, char **azColName){
   int i;
   for(i=0; i<argc; i++){
      LOG_D(TAG, "%s = %s\n", azColName[i], argv[i] ? argv[i] : "NULL");
   }
   LOG_D(TAG, " &&&&& ARGC count %d &&&&& ",argc);
   return 0;
}

static int callback_count_size(void *count, int argc, char **argv, char **azColName){
    int i;
    int64_t *temp_count = (int64_t* )count;

    temp_count[0] = temp_count[0] + argc;
    //if( temp_count[0] == 1 )
    {
        temp_count[1] += argc >= 1 ? atoi(argv[0]) : 0;
        temp_count[2] += argc >= 2 ? atoi(argv[1]) : 0;
    }
    return 0;
}

static int callback_get_count_and_size(void *count, int argc, char **argv, char **azColName){
    
    count_size_data_t *temp_count = (count_size_data_t* )count;

    if( argc > 1 )
    {
        temp_count->count = (int32_t)atoi(argv[1]);
        if(temp_count->count > 0) {
            temp_count->size = (float)((float)atoll(argv[0])/((float)BYTES_IN_MB));
        }
    } else {
        temp_count->count = 0;
        temp_count->size = 0.0;
    }

    return 0;
}

static int callback_count_size_status(void *count, int argc, char** argv, char **azColName){
    int64_t *temp_count = (int64_t*) count;
    if(temp_count[RES_ENTRY_COUNT_IND] > 0){
        LOG_E(TAG,"More than one entry found with same filename in DB");
    }
    temp_count[RES_ENTRY_COUNT_IND] += 1;
    // upl_vid_enabled enabled means upload privacy is disabled. by defaul upload video should be enabled.
    // Details: https://netradyne.atlassian.net/browse/DT-954?focusedCommentId=359586
    temp_count[RES_FILE_SIZE_IND] = DEFAULT_VALUE_STRING_TO_INTEGER_FAILED;
    temp_count[RES_STATUS_IND] = DEFAULT_VALUE_STRING_TO_INTEGER_FAILED;
    temp_count[RES_UPL_VID_ENABLED_IND] = DEFAULT_VALUE_STRING_TO_INTEGER_FAILED;
    temp_count[RES_REC_VID_ENABLED_IND] = DEFAULT_VALUE_STRING_TO_INTEGER_FAILED;
    for(int column_ind = RES_FILE_SIZE_IND;column_ind <= RES_REC_VID_ENABLED_IND;column_ind++){
        int temp_column_val = DEFAULT_VALUE_STRING_TO_INTEGER_FAILED;
        if (!string_to_integer(argv[column_ind-1], temp_column_val)){
            LOG_E(TAG, "string_to_integer failed for column %s : unable to convert %s to integer", azColName[column_ind-1], argv[column_ind - 1]);
            return 1; //SQLITE_ABORT
        }
        temp_count[column_ind] = temp_column_val;
    }
    return 0;
}

static int callback_count_indx(void *count, int argc, char **argv, char **azColName){
    int i;
    int64_t *temp_count = (int64_t* )count;
    LOG_D(TAG, "inside callback_count_indx");
    temp_count[0] = temp_count[0] + (int64_t)argc;
    if( argc >= 1 )
    {
        int64_t numb_temp = argv[0] ? strtoll( argv[0], NULL, 10 ) : 0;
        temp_count[1] += (int64_t)( numb_temp );
    }
    return 0;
}

int callback_indx(void *index, int argc, char **argv, char **azColName){
    LOG_D(TAG, "inside callback_indx");
    if(argv[0] == NULL){
        LOG_I(TAG, "inside callback_indx(). argv[0] is NULL "); 
    }
    else {
        int tmp = -1;
        *((int *)index) = tmp;
        if(false == string_to_integer(std::string(argv[0]), tmp)) {
            LOG_E(TAG, "string_to_integer failed for index : unable to convert %s to integer", argv[0]);
            return 0;
        }
        *((int *)index) = tmp;
    }

    return 0;
}

static int callback_indx_filename(void *index, int argc, char **argv, char **azColName){
    LOG_D(TAG, "inside callback_indx");
    if(argc != 2) {
        return -1;
    }

    if(NULL != index){
        alldata_str_t *ptr = (alldata_str_t *)index;
        ptr->index_id = -1;
        // Check if argv[0] is NULL before using it
        if(argv[0] != NULL && argv[0][0] != '\0') {
            if(false == string_to_integer(std::string(argv[0]), ptr->index_id)) {
                LOG_E(TAG, "string_to_integer failed for index_id : unable to convert %s to integer", argv[0]);
            }
        } else {
            LOG_C(TAG, "argv[0] (INDEXID) is NULL or empty - no rows found in query");
            // Don't return -1 here as it aborts the query. Leave default value -1 for index_id
            // The calling code will handle this case by checking if index_id == -1
        }

        // Check if argv[1] is NULL before using it
        if(argv[1] != NULL && argv[1][0] != '\0') {
            ptr->file_name = std::string(argv[1]);
        } else {
            LOG_I(TAG, "argv[1] (NAME) is NULL or empty");
            ptr->file_name = "";
        }
    }
    else{
        LOG_E(TAG,"data is null in callback_indx_filename");
        return -1;
    }
    return 0;
}

static int callback_db_details(void *db_details, int argc, char **argv, char **azColName){
    pair<string, int64_t> *db_details_temp = (pair<string, int64_t> *)db_details;
    LOG_D(TAG, "inside callback_db_identifier");
    db_details_temp->first = argv[0];
    db_details_temp->second = atoll(argv[1]);

    return 0;
}

int64_t  drp_calcualte_timestamp_uploader(const char* fileName){
    LOG_I( TAG, "Get timestamp of file:  %s", fileName );
    string val_string;
    std::stringstream val_stream;
    int64_t timestamp ;
    int64_t curr_sessionCount = current_sessionCount, sessionCount = sessionCount_from_file(fileName) ;
    int64_t curr_udid = current_udid, file_udid = udid_from_file(fileName);
    prop_data_t entry;
    string udid_string = "", sessionCount_string = "";
    if( get_property_DB("udid", &entry) ) {
        udid_string = entry.value;
        string_to_int64(udid_string , curr_udid);
        LOG_I(TAG, "curr_udid: %lld, file_udid: %lld", curr_udid , file_udid);
    }else{
        LOG_C(TAG,"Unable to get udid from gen prop Db");
    }
    if( get_property_DB("sessionCount", &entry) ) {
        sessionCount_string = entry.value;
        string_to_int64(sessionCount_string , curr_sessionCount);
        LOG_I(TAG, "curr_sessionCount: %lld, file_sessionCount: %lld", curr_sessionCount , sessionCount);
    }else{
        LOG_C(TAG,"Unable to get sessionCount from gen prop Db");
    }
// check if file is present in DB
    val_stream << "SELECT COUNT(*) from VIDFILES WHERE NAME LIKE  \"" << fileName << "%\"";
    val_string = val_stream.str();
    LOG_I(TAG, "sqlquery: %s", val_string.c_str() );
    int64_t count_size[3] = {0, 0, 0}; 
    int rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string,
            callback_count_size, (void*)&count_size[0]);
    if(rc == false){
        LOG_E(TAG, "failed to execute");
        return 0;
    }
    if(count_size[1] <= 0){
        string str_msg = "DRP: VOD File is not present in DB ";
        LOG_E(TAG, str_msg.c_str() );
        nd_service_obj->send_err_msg(SM_E_CB_VOD_DRP_FILE_NOT_IN_DB, sessionCount, str_msg );

    }
    // File udid and current boot udid is same
    if(file_udid == curr_udid){
        int64_t totalSession_diff = curr_sessionCount-sessionCount ;
        LOG_I("DRP ","file from same udid %s", fileName);
        return (get_system_time() - (totalSession_diff*60*1000)) ;  

    }
    else {
        // find a file in db with correct timestamp and udid same as file udid.
        val_stream.str("");
        val_stream.clear();
        val_stream << "SELECT COUNT(*) from VIDFILES WHERE UDID == "<< file_udid 
            << "  AND STATUS != 2 AND TIME > 0 " ;
        val_string = val_stream.str();
        LOG_I(TAG, "sqlquery: %s", val_string.c_str() );

        int64_t count_size[3] = {0, 0, 0}; 
        int rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string,
                callback_count_size, (void*)&count_size[0]);
        if(rc == false){
            LOG_E(TAG, "failed to execute");
            return 0;
        } 
        val_stream.str("");
        val_stream.clear();
        if(count_size[1] > 0){ 
            // file found with correct timestap and udid is same as file udid.
            val_stream << "SELECT * from VIDFILES WHERE UDID == " << file_udid 
                << "  AND STATUS != 2 AND TIME > 0 AND NAME LIKE \"%_y.mp4%\" ORDER BY SESSIONCOUNT DESC LIMIT 1";
            val_string = val_stream.str();
            LOG_I(TAG, "sqlquery: %s", val_string.c_str() );

            circular_buffer_file_t fileinfo;
            rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string, 
                    callback_complete_fileinfo, (void*)&fileinfo);
            if(rc == false){
                LOG_E(TAG, "failed to execute");
                return 0;
            }
            int64_t timeStamp_tmp = fileinfo.time ;
            int64_t sessionCount_tmp = fileinfo.sessionCount ;
            int64_t diff_sessionCount = sessionCount_tmp - sessionCount ;
            timestamp = (timeStamp_tmp - (diff_sessionCount*60*1000));             
            LOG_I(TAG, "timestamp %lld, same udid file with good timestamp: %s", timestamp, fileinfo.base_file_name );
        }
        else {
            LOG_I(TAG, "No file exist with correct timestamp in udid: %lld", file_udid );
            val_stream << "SELECT COUNT(*) from VIDFILES WHERE SESSIONCOUNT < "<< sessionCount << " AND TIME > 0 AND STATUS != 2 AND NAME LIKE \"%_y.mp4%\" " ;

            val_string = val_stream.str();
            LOG_I(TAG, "sqlquery: %s", val_string.c_str() );

            int64_t count_size[3] = {0, 0, 0}; 
            int rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string,
                    callback_count_size, (void*)&count_size[0]);
            if(rc == false){
                LOG_E(TAG, "failed to execute");
                return 0;
            }
            LOG_I(TAG, "number of older video file with good timestamp : %lld", count_size[1] );
            if(count_size[1] > 0 ){   
                val_stream.str("");
                val_stream.clear();
                val_stream << "SELECT * from VIDFILES WHERE SESSIONCOUNT < "<< sessionCount 
                    << " AND STATUS != 2 AND TIME > 0 AND NAME LIKE \"%_y.mp4%\" ORDER BY SESSIONCOUNT DESC LIMIT 1";
                val_string = val_stream.str();
                LOG_I(TAG, "sqlquery: %s", val_string.c_str() );

                circular_buffer_file_t fileinfo;
                rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string, 
                        callback_complete_fileinfo, (void*)&fileinfo);
                if(rc == false){
                    LOG_E(TAG, "failed to execute");
                    return 0;
                }
                timestamp = fileinfo.time ;
                LOG_I(TAG, "Latest previous file: %s with correct timestamp: %lld", fileinfo.base_file_name, timestamp );

            }
            else {
                LOG_I(TAG, "Couldn't determine the file creation time"  );
                return -1;
            }
        }
    }
    return timestamp ;

}

/**
 * @brief Checks if the number of DMS alert LQ files exceeds the limit and deletes the oldest.
 *
 * @param db SQLite database handle
 * @return true if successful or no deletion needed, false if an error occurred
 */

bool check_and_delete_alert_dms_file(sqlite3 *db)
{
	int rc, cam_type = DMS_CAM_NO;
	bool ret = true;

	string val_string;
	std::stringstream val_stream;

	val_stream << "SELECT COUNT(*) from VIDFILES WHERE STATUS != "<< CIRCULAR_BUFFER_STATUS_DELETE << " AND FILE_COMPRESSION == "
		<< CIRCULAR_BUFFER_NO_COMPRESSION  << " AND CAM_TYPE == " << cam_type  << " AND TRANSCODE_STATUS = " << CIRCULAR_BUFFER_TC_STATUS_TRANSCODED  ;

	val_string = val_stream.str();
	LOG_I(TAG, "alert mds lq video count query: %s", val_string.c_str() );

	int64_t count_size[3] = {0, 0, 0}; 
	rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string,
			callback_count_size, (void*)&count_size[0]);
	if(rc == false){
		LOG_E(TAG, "failed to execute");
		return false;
	}
	LOG_I(TAG, "Total alert dms lq video: %lld", count_size[1] );

	if(count_size[1] > max_alert_hq_file ) { 
		// logic to delete for oldest alert file. 
		LOG_I(TAG, "Deleting one DMS alert file to add new alert file" );
		val_stream.str("");
		val_stream.clear();
		val_stream << "SELECT min(INDEXID) from VIDFILES WHERE STATUS != " << CIRCULAR_BUFFER_STATUS_DELETE << " AND  FILE_COMPRESSION == "
			<< CIRCULAR_BUFFER_NO_COMPRESSION  << " AND CAM_TYPE == " << cam_type << " AND TRANSCODE_STATUS = " << CIRCULAR_BUFFER_TC_STATUS_TRANSCODED;
		val_string = val_stream.str();
		LOG_I(TAG, "oldest DMS alert query: %s", val_string.c_str() );
		int64_t count_indx[3] = {0, 0, 0};
		rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string, 
				callback_count_indx, (void*)&count_indx[0]);
		if(rc == false){
			LOG_E(TAG, "Failed to execute query: %s", val_string.c_str());
			return false;
		}

		val_stream.str("");
		val_stream.clear();
		val_stream << "SELECT * from VIDFILES WHERE INDEXID == " << count_indx[1];
		val_string = val_stream.str();
		circular_buffer_file_t fileinfo;
		rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string, 
				callback_complete_fileinfo, (void*)&fileinfo);
		if(rc == false){
			LOG_E(TAG, "failed to execute");
			return false;
		}
        
        if(store_lq_dms_file==false){
            string file_name(fileinfo.base_file_name);
            LOG_I(TAG, "oldest_file_rotated_index: %d count_indx[1]: %lld", oldest_file_rotated_index , count_indx[1] );
            delete_entry_from_db(fileinfo.index_id, fileinfo.file_type);
            bool del_status = delete_file_from_sdcard(file_name);
            if(del_status == false){
                ret = false;
            }
        }
        else{
            if(!update_file_compression_DB(fileinfo.base_file_name, CIRCULAR_BUFFER_NO_COMPRESSION_ADJ))
               LOG_E(TAG, "Failed to update %s file compression to %d in DB", fileinfo.base_file_name, (int)CIRCULAR_BUFFER_NO_COMPRESSION_ADJ);
        }
	}
	return ret;

} 

int get_min_max_count_db(query_type_t query_type, circular_buffer_filetype_t file_type, circular_buffer_file_compression_t file_compression)
{
    LOG_I(TAG, "get_min_max_count_db for query_type: %d, file_type: %d, file_compression: %d", query_type, file_type, file_compression);
    string val_string;
    std::stringstream val_stream;
    val_stream << "SELECT " ;
    if(query_type == QUERY_TYPE_MIN_INDEX){
        val_stream << "min(INDEXID) " ;
    } else if (query_type == QUERY_TYPE_MAX_INDEX){
        val_stream << "max(INDEXID) " ;
    } else if ((query_type == QUERY_TYPE_COUNT) || (query_type == QUERY_TYPE_CAM0_COUNT)){
        val_stream << "COUNT(*) " ;
    } else {
        LOG_E(TAG, "Invalid query_type: %d", query_type);
        return -1;
    }
    val_stream << "from VIDFILES WHERE STATUS != "<< CIRCULAR_BUFFER_STATUS_DELETE ;
    val_stream << " AND TYPE == " << (int)file_type ; // For video / observation files
    if(file_compression != CIRCULAR_BUFFER_NO_COMPRESSION){  // For alert / non-alert files
        val_stream << " AND FILE_COMPRESSION != " << (int)CIRCULAR_BUFFER_NO_COMPRESSION;
    }else{
        val_stream << " AND FILE_COMPRESSION == " << (int)CIRCULAR_BUFFER_NO_COMPRESSION;
    }
    if(query_type == QUERY_TYPE_CAM0_COUNT){
        val_stream << " AND CAM_TYPE == " <<  CIRCULAR_BUFFER_CAM_FRONT;
        val_stream << " AND NAME LIKE " << "\"%mp4.ld.mp4\"";
    }
    val_string = val_stream.str();
    LOG_I(TAG, "sqlquery: %s", val_string.c_str() );

    int result = -1;
    int rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string,
            callback_indx, (void*)&result);
    if(rc == false){
        LOG_E(TAG, "failed to execute %s", val_string.c_str());
        return -1;
    }
    LOG_I(TAG, "Result: %d", result);
    return result;
}

string get_filename_by_index(int64_t index_id)
{
    LOG_D(TAG, "get_filename_by_index for index_id: %d", index_id);
    string val_string;
    std::stringstream val_stream;
    val_stream << "SELECT INDEXID, NAME from VIDFILES WHERE INDEXID == " << index_id ;
    val_string = val_stream.str();
    LOG_D(TAG, "sqlquery: %s", val_string.c_str() );

    alldata_str_t file_data = {-1, ""};
    int rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string,
            callback_indx_filename, (void*)&file_data);
    if(rc == false){
        LOG_E(TAG, "failed to execute %s", val_string.c_str());
        return "";
    }
    LOG_D(TAG, "Filename: %s", file_data.file_name.c_str() );
    return file_data.file_name;
}

void get_sessiondata_and_send_critical_event(int64_t oldest_index, int64_t newest_index, int64_t count, circular_buffer_filetype_t file_type, circular_buffer_file_compression_t file_compression, int64_t cam0_count = -1)
{
    LOG_I(TAG, "Oldest index: %lld, Newest index: %lld, Count: %lld", oldest_index, newest_index, count);
    string oldest_filename = "", newest_filename = "";
    if(oldest_index != -1){
        oldest_filename = get_filename_by_index(oldest_index);
    }
    if(newest_index != -1){
        newest_filename = get_filename_by_index(newest_index);
    }
    if(oldest_filename == "" || newest_filename == ""){
        LOG_E(TAG, "Failed to get oldest or newest filename from DB for non alert files");
    }else{
        int64_t oldest_udid = -1, oldest_sessionCount = -1, newest_udid = -1, newest_sessionCount = -1;
        string file_path = nd_device_obj->get_external_eMMC_mount_path();
        
        if(CIRC_BUFF_ctx->extended_attr_enabled){
            string oldest_full_path = file_path + oldest_filename;
            string newest_full_path = file_path + newest_filename;

            bool oldest_success = ((get_file_xattr(oldest_full_path, FileMetadataKey::udid, &oldest_udid, sizeof(oldest_udid)) == XATTR_OK) &&
                                (get_file_xattr(oldest_full_path, FileMetadataKey::sid, &oldest_sessionCount, sizeof(oldest_sessionCount)) == XATTR_OK));
           
            bool newest_success = ((get_file_xattr(newest_full_path, FileMetadataKey::udid, &newest_udid, sizeof(newest_udid)) == XATTR_OK) &&
                                (get_file_xattr(newest_full_path, FileMetadataKey::sid, &newest_sessionCount, sizeof(newest_sessionCount)) == XATTR_OK));
            
            // If UDID or sessionCount is -1, treat as invalid and fallback to filename parsing
            if(oldest_udid == -1 || oldest_sessionCount == -1){
               oldest_success = false;
            }
            if(newest_udid == -1 || newest_sessionCount == -1){
                newest_success = false;           
            }
            
            // Fallback to filename parsing for failed xattr reads or in case of zip files
            if(!oldest_success) {
                oldest_udid = udid_from_file(oldest_filename);
                oldest_sessionCount = sessionCount_from_file(oldest_filename); 
            }
            if(!newest_success) {
                newest_udid = udid_from_file(newest_filename);
                newest_sessionCount = sessionCount_from_file(newest_filename);
            }

        } else {
            // Extended attributes disabled, parse from filenames
            oldest_udid = udid_from_file(oldest_filename);
            oldest_sessionCount = sessionCount_from_file(oldest_filename);
            newest_udid = udid_from_file(newest_filename);
            newest_sessionCount = sessionCount_from_file(newest_filename);
        }
        LOG_I(TAG, "Oldest file: %s, udid: %lld, sessionCount: %lld :: Newest file: %s, udid: %lld, sessionCount: %lld :: Count %lld", oldest_filename.c_str(), oldest_udid, oldest_sessionCount, newest_filename.c_str(), newest_udid, newest_sessionCount, count);
        string session_msg = "";
        if(file_type == CIRCULAR_BUFFER_TYPE_NORMAL){
            if(file_compression == CIRCULAR_BUFFER_NO_COMPRESSION){
                session_msg += "Alert ";
            } else {
                session_msg += "Non-Alert ";
            }
        } else if (file_type == CIRCULAR_BUFFER_TYPE_TEXT_ALERTS){
            session_msg += "Observation ";
        }
        session_msg += "Oldest UDID: " + std::to_string(oldest_udid) + " SC: " + std::to_string(oldest_sessionCount) + " Latest UDID: " + std::to_string(newest_udid) + " SC: " + std::to_string(newest_sessionCount) + " Cnt: " + std::to_string(count);
        if(-1 != cam0_count){
            session_msg += " Cnt_0: " + std::to_string(cam0_count);
        }
        nd_service_obj->send_err_msg(SM_I_CB_DB_TOTAL_SESSION_COUNT, count, session_msg);
    }
}

bool oldestNewest_Session()
{
    int rc ;

    // Non Alert
    // Oldest and Newest Session Data
    int oldest_index = -1, newest_index = -1, count = -1, out_cam_count = -1;
    oldest_index = get_min_max_count_db(QUERY_TYPE_MIN_INDEX, CIRCULAR_BUFFER_TYPE_NORMAL, CIRCULAR_BUFFER_MEDIUM_COMPRESSION);
    newest_index = get_min_max_count_db(QUERY_TYPE_MAX_INDEX, CIRCULAR_BUFFER_TYPE_NORMAL, CIRCULAR_BUFFER_MEDIUM_COMPRESSION);
    count = get_min_max_count_db(QUERY_TYPE_COUNT, CIRCULAR_BUFFER_TYPE_NORMAL, CIRCULAR_BUFFER_MEDIUM_COMPRESSION);
    out_cam_count = get_min_max_count_db(QUERY_TYPE_CAM0_COUNT, CIRCULAR_BUFFER_TYPE_NORMAL, CIRCULAR_BUFFER_MEDIUM_COMPRESSION);
    get_sessiondata_and_send_critical_event(oldest_index, newest_index, count, CIRCULAR_BUFFER_TYPE_NORMAL, CIRCULAR_BUFFER_MEDIUM_COMPRESSION, out_cam_count);

    // Alert
    // Oldest and Newest Session Data
    oldest_index = -1; newest_index = -1; count = -1;
    oldest_index = get_min_max_count_db(QUERY_TYPE_MIN_INDEX, CIRCULAR_BUFFER_TYPE_NORMAL, CIRCULAR_BUFFER_NO_COMPRESSION);
    newest_index = get_min_max_count_db(QUERY_TYPE_MAX_INDEX, CIRCULAR_BUFFER_TYPE_NORMAL, CIRCULAR_BUFFER_NO_COMPRESSION);
    count = get_min_max_count_db(QUERY_TYPE_COUNT, CIRCULAR_BUFFER_TYPE_NORMAL, CIRCULAR_BUFFER_NO_COMPRESSION);
    out_cam_count = get_min_max_count_db(QUERY_TYPE_CAM0_COUNT, CIRCULAR_BUFFER_TYPE_NORMAL, CIRCULAR_BUFFER_NO_COMPRESSION);
    get_sessiondata_and_send_critical_event(oldest_index, newest_index, count, CIRCULAR_BUFFER_TYPE_NORMAL, CIRCULAR_BUFFER_NO_COMPRESSION, out_cam_count);

    // Observation / Zip files
    oldest_index = -1; newest_index = -1; count = -1;
    oldest_index = get_min_max_count_db(QUERY_TYPE_MIN_INDEX, CIRCULAR_BUFFER_TYPE_TEXT_ALERTS, CIRCULAR_BUFFER_MEDIUM_COMPRESSION);
    newest_index = get_min_max_count_db(QUERY_TYPE_MAX_INDEX, CIRCULAR_BUFFER_TYPE_TEXT_ALERTS, CIRCULAR_BUFFER_MEDIUM_COMPRESSION);
    count = get_min_max_count_db(QUERY_TYPE_COUNT, CIRCULAR_BUFFER_TYPE_TEXT_ALERTS, CIRCULAR_BUFFER_MEDIUM_COMPRESSION);
    get_sessiondata_and_send_critical_event(oldest_index, newest_index, count, CIRCULAR_BUFFER_TYPE_TEXT_ALERTS, CIRCULAR_BUFFER_MEDIUM_COMPRESSION);
    return true;

}
static int callback_count_min_index(void* data, int argc, char** argv, char** azColName)
{
    if ((data == nullptr) || (argc < 2)){
        LOG_E(TAG, "Invalid data pointer or insufficient columns in callback_count_min_index");
        return 0;
    }

    int* result = static_cast<int*>(data);
    int tmp = -1;
    // argv[i] can be NULL if the result is NULL
    if(argv[0] != nullptr){ // COUNT(*)
        if(false == string_to_integer(argv[0], tmp)){
            LOG_E(TAG, "string_to_integer failed for count : unable to convert %s to integer", argv[0]);
            result[0] = 0;
        } else {
            result[0] = tmp;
        }
    }
    if(argv[1] != nullptr){ // min(INDEXID)
        if(false == string_to_integer(argv[1], tmp)){
            LOG_E(TAG, "string_to_integer failed for min_index : unable to convert %s to integer", argv[1]);
            result[1] = -1;
        } else {
            result[1] = tmp;
        }
    }

    return 0; // returning non-zero aborts sqlite3_exec
}

static bool get_num_hq_min_index_videos_db(int64_t &num_hq_videos_db, int &min_index, circular_buffer_camtype_t cam_type)
{
    int rc;
    bool ret = true;
    int tc_status = CIRCULAR_BUFFER_TC_STATUS_WAITING ;
    string val_string;
    std::stringstream val_stream;
    int data[2] = {-1, -1};
    val_stream.str("");
    val_stream.clear();
    val_stream <<
        "SELECT COUNT(*), MIN(INDEXID) from VIDFILES WHERE (TRANSCODE_STATUS == "
        << tc_status << " ) AND ( TYPE IN ("
        << CIRCULAR_BUFFER_TYPE_NORMAL << ",  "
        << CIRCULAR_BUFFER_TYPE_ALERT << ") ) AND (STATUS != "
        << CIRCULAR_BUFFER_STATUS_DELETE << ")" << " AND (CAM_TYPE == "
        << cam_type << ")"
        << "AND NAME LIKE \"%_y.mp4\" " ;
    val_string = val_stream.str();
    int64_t count_rows[2] = {0, 0};
    rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string,
            callback_count_min_index, (void*)&data);
    if(rc == false){
        LOG_E(TAG, "failed to execute %s", val_string.c_str());
        return false;
    }
    num_hq_videos_db = data[0];
    min_index = data[1];
    LOG_I(TAG, "get_num_hq_min_index_videos_db : count %lld min_index %lld", num_hq_videos_db, min_index);

    return ret;
}

bool check_and_delete_alert_hq_file(sqlite3 *db)
{
    int rc, cam_type = 0;
    bool ret = true;

    string val_string;
    std::stringstream val_stream;

    val_stream << "SELECT COUNT(*) from VIDFILES WHERE STATUS != "<< CIRCULAR_BUFFER_STATUS_DELETE << " AND (FILE_COMPRESSION == "
                                << CIRCULAR_BUFFER_NO_COMPRESSION  << ") AND (CAM_TYPE == " << cam_type << ")";
 
    val_string = val_stream.str();
    LOG_D(TAG, "alert hq video count query: %s", val_string.c_str() );
    
    int64_t count_size[3] = {0, 0, 0}; // callback function callback_count_size() will fill argc in count_size[0], argv[0] in count_size[1] and argv[2] is reserved.
                                       // argc is number of coloumn and argv[i] is ith coloumn.
    rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string,
                     callback_count_size, (void*)&count_size[0]);
    if(rc == false){
        LOG_E(TAG, "failed to execute");
        return false;
    }
    LOG_I(TAG, "Total alert hq video: %lld", count_size[1] );

    if(count_size[1] > max_alert_hq_file) { 
    // logic to delete for oldest alert file. 
        bool delete_needed = false;
        LOG_I(TAG, "Deleting one alert file to add new alert file" );
        val_stream.str("");
        val_stream.clear();
        val_stream << "SELECT min(INDEXID) from VIDFILES WHERE STATUS != " << CIRCULAR_BUFFER_STATUS_DELETE << " AND ( FILE_COMPRESSION == "
                                << CIRCULAR_BUFFER_NO_COMPRESSION  << ") AND (CAM_TYPE == " << cam_type << ")";
        val_string = val_stream.str();
        LOG_D(TAG, "oldest alert: %s", val_string.c_str() );
        int64_t count_indx[3] = {0, 0, 0};
        rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string, 
                                callback_count_indx, (void*)&count_indx[0]);
        if(rc == false){
            LOG_E(TAG, "failed to execute");
            return false;
        }

        val_stream.str("");
        val_stream.clear();
        val_stream << "SELECT * from VIDFILES WHERE INDEXID == " << count_indx[1];
        val_string = val_stream.str();
        circular_buffer_file_t fileinfo;
        rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string, 
                            callback_complete_fileinfo, (void*)&fileinfo);
        if(rc == false){
            LOG_E(TAG, "failed to execute");
            return false;
        }
        
        string file_name(fileinfo.base_file_name);
        LOG_I(TAG, "oldest_file_rotated_index: %d count_indx[1]: %lld", oldest_file_rotated_index , count_indx[1] );
        if(oldest_file_rotated_index > count_indx[1] || get_file_size(nd_device_obj->get_external_eMMC_mount_path() + file_name + ld_extn) > 0) {
            /////   TBD
            int64_t num_hq_videos = 0;
            int hq_min_index = -1;
            if(!get_num_hq_min_index_videos_db( (int64_t &)num_hq_videos, hq_min_index, (circular_buffer_camtype_t)cam_type)){
                LOG_E(TAG, "get_num_hq_min_index_videos_db failed");
                return false;
            }
            LOG_I(TAG, "HQ file count: %d, min_index: %d, alert index id: %lld", num_hq_videos, hq_min_index, fileinfo.index_id);
            if((num_hq_videos > num_hq_videos_max) && (hq_min_index > fileinfo.index_id)){ // hq count is more than max limit and alert file is oldest among hq files, then delete it.
                LOG_I(TAG, "Deleting HQ file to maintain max hq video files limit, hq file count: %d, min_index: %d, alert index id: %lld", num_hq_videos, hq_min_index, fileinfo.index_id);
                delete_entry_from_db(fileinfo.index_id, fileinfo.file_type);
                bool del_status = delete_file_from_sdcard(file_name);
                if(del_status == false){
                    ret = false;
                }
                delete_needed = true;
            }else{ // otherwise just update the alert file compression to medium compression.
                LOG_I(TAG, "Alert HQ file in range, mark it CIRCULAR_BUFFER_NO_COMPRESSION_ADJ and update transcode status to WAITING : %s", fileinfo.base_file_name);
                if(!update_file_compression_DB(fileinfo.base_file_name, CIRCULAR_BUFFER_NO_COMPRESSION_ADJ )){
                    LOG_E(TAG, "Failed to update %s file compression to %d in DB", fileinfo.base_file_name, (int)CIRCULAR_BUFFER_NO_COMPRESSION_ADJ);
                }
                if(false == update_tc_status_DB(fileinfo.base_file_name, CIRCULAR_BUFFER_TC_STATUS_WAITING)){
                    LOG_E(TAG, "Failed to update %s transcode status to WAITING in DB", fileinfo.base_file_name);
                }
            }   
        }
        else {
            LOG_I(TAG, "LD file not present, mark it CIRCULAR_BUFFER_NO_COMPRESSION_NO_LQ : %s", (nd_device_obj->get_external_eMMC_mount_path() + file_name + ld_extn).c_str() );
                if (!update_file_compression_DB(fileinfo.base_file_name, CIRCULAR_BUFFER_NO_COMPRESSION_NO_LQ )){
                    LOG_E(TAG, "Failed to update %s file compression to %d in DB",
                                fileinfo.base_file_name, (int)CIRCULAR_BUFFER_NO_COMPRESSION_NO_LQ);
                }
            
        }
        // delete Inward 
        val_stream.str("");
        val_stream.clear();
        file_name[0] = '1';
        val_stream << "SELECT INDEXID from VIDFILES WHERE NAME == " << "'" << file_name << "'" ;
        val_string = val_stream.str();
        rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string, 
                                 callback_count_entry_available, (void*)&count_indx[0]);
        if(rc == false){
            LOG_E(TAG, "failed to execute. %s", val_string.c_str() );
            return false;
        }
        if(get_file_size(nd_device_obj->get_external_eMMC_mount_path() + file_name + ld_extn) > 0) {
            /////   TBD
            if(true == delete_needed){
                LOG_I(TAG, "Deleting Inward HQ file to maintain max hq video files limit, hq file count: alert index id: %lld", fileinfo.index_id);
                delete_entry_from_db(count_indx[0], fileinfo.file_type);
                bool del_status = delete_file_from_sdcard(file_name);
                if(del_status == false){
                    ret = false;
                }
            }else{
                LOG_I(TAG, "Inward HQ file in range, mark it CIRCULAR_BUFFER_NO_COMPRESSION_ADJ and update transcode status to WAITING: %s", file_name.c_str());
                if(!update_file_compression_DB(file_name.c_str(), CIRCULAR_BUFFER_NO_COMPRESSION_ADJ )){
                    LOG_E(TAG, "Failed to update %s file compression to %d in DB", file_name.c_str(), (int)CIRCULAR_BUFFER_NO_COMPRESSION_ADJ);
                }
                if(false == update_tc_status_DB(file_name.c_str(), CIRCULAR_BUFFER_TC_STATUS_WAITING)){
                    LOG_E(TAG, "Failed to update %s transcode status to WAITING in DB", file_name.c_str());
                }
            }
        }
        else {
            LOG_I(TAG, "Inward LD file not present, mark it CIRCULAR_BUFFER_NO_COMPRESSION_NO_LQ : %s", (nd_device_obj->get_external_eMMC_mount_path() + file_name + ld_extn).c_str() );
            if (!update_file_compression_DB(file_name.c_str() , CIRCULAR_BUFFER_NO_COMPRESSION_NO_LQ )){
                LOG_E(TAG, "Failed to update %s file compression to %d in DB",
                            fileinfo.base_file_name, (int)CIRCULAR_BUFFER_NO_COMPRESSION_NO_LQ);
            }
            
        }
        // changing the corresponding side cams(2, 3) from CIRCULAR_BUFFER_NO_COMPRESSION to CIRCULAR_BUFFER_MEDIUM_COMPRESSION
        // Because Alert file(CIRCULAR_BUFFER_NO_COMPRESSION) is not deleting at time of adding new file through filling limit check.

        // Get index of oldest alert file, and delete according to the side camera index, if index < oldest non alert file index, then delete side cam alert file, else change compression to medium.

        file_name[0] = '0' + CIRCULAR_BUFFER_CAM_LEFT; //rename file name to left side camera video file
        if(file_is_present(nd_device_obj->get_external_eMMC_mount_path() + file_name)){
            if (!update_file_compression_DB(file_name.c_str(), CIRCULAR_BUFFER_NO_COMPRESSION_ADJ)){
                LOG_E(TAG, "Failed to update %s file compression to %d in DB", file_name.c_str(), (int)CIRCULAR_BUFFER_NO_COMPRESSION_ADJ);
            }
        }
        file_name[0] = '0' + CIRCULAR_BUFFER_CAM_RIGHT; //rename file name to right side camera video file
        if(file_is_present(nd_device_obj->get_external_eMMC_mount_path() + file_name)){
            if (!update_file_compression_DB(file_name.c_str(), CIRCULAR_BUFFER_NO_COMPRESSION_ADJ)){
                LOG_E(TAG, "Failed to update %s file compression to %d in DB", file_name.c_str(), (int)CIRCULAR_BUFFER_NO_COMPRESSION_ADJ);
            }
        }
    }
    else {
        LOG_I(TAG, "Total alert files are : %lld", count_size[1]);

    }
    return ret;
}

static int callback_get_num_vid_files(void *num_files, int argc, char **argv, char **azColName) {

    int *val = (int *)num_files;
    if(argc > 0) {
        *val = atoi(argv[0]);
    } else {
        *val = -1;
        return -1;
    }

    return 0;
}

int execute_query_for_num_vid_files(string str, int32_t *count, float *size)
{
    bool rc = false;
    count_size_data_t count_size;

    memset(&count_size, 0, sizeof(count_size_data_t));
    rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, str,
                     callback_get_count_and_size, (void*)&count_size);
    if(rc == false){
        LOG_E(TAG, "failed to execute %s", str.c_str());
        return -1;
    }
    *count = count_size.count;
    *size = count_size.size;
    if(*count == 0) {
        *size = 0.0;
    }
    LOG_D(TAG, "count = %ld", *count);
    LOG_D(TAG, "size = %f", *size);
    return 0;
}

int get_num_lq_vid_files( int32_t &front, int32_t &back, int32_t &ext, float& front_size, float& back_size, float& ext_size) {

    int rc = -1;
    bool ret = false;
    string val_string;
    std::stringstream val_stream;
    int32_t ext_1 = 0, ext_2 = 0, ext_3 = 0, ext_4 = 0;
    float ext_1_size = 0.0, ext_2_size = 0.0, ext_3_size = 0.0, ext_4_size = 0.0;

    val_stream << "SELECT SUM(FILE_SIZE),COUNT(*) FROM VIDFILES WHERE NAME LIKE '%.ld.mp4' AND STATUS != "
        << CIRCULAR_BUFFER_STATUS_DELETE
        << " AND CAM_TYPE == "
        << CIRCULAR_BUFFER_CAM_FRONT
        << " AND FILE_SIZE > 0"
        << ";";

    val_string = val_stream.str();

    rc = execute_query_for_num_vid_files(val_string, &front, &front_size);
    if(rc < 0) {
        LOG_E(TAG, "fail to execute execute_query_for_num_vid_files for outward LQ");
        return -1;
    }
    val_string.clear();
    val_stream.clear();

    val_stream << "SELECT SUM(FILE_SIZE),COUNT(*) FROM VIDFILES WHERE NAME LIKE '%.ld.mp4' AND STATUS != "
        << CIRCULAR_BUFFER_STATUS_DELETE
        << " AND CAM_TYPE == "
        << CIRCULAR_BUFFER_CAM_DRIVER
        << " AND FILE_SIZE > 0"
        << ";";

    val_string = val_stream.str();

    rc = execute_query_for_num_vid_files(val_string, &back, &back_size);
    if(rc < 0) {
        LOG_E(TAG, "fail to execute execute_query_for_num_vid_files for inward LQ");
        return -1;
    }

    val_string.clear();
    val_stream.clear();

    val_stream << "SELECT SUM(FILE_SIZE),COUNT(*) FROM VIDFILES WHERE NAME LIKE '%.ld.mp4' AND STATUS != "
        << CIRCULAR_BUFFER_STATUS_DELETE
        << " AND CAM_TYPE == "
        << CIRCULAR_BUFFER_CAM_EXT1
        << " AND FILE_SIZE > 0"
        << ";";

    val_string = val_stream.str();

    rc = execute_query_for_num_vid_files(val_string, &ext_1, &ext_1_size);
    if(rc < 0) {
        LOG_E(TAG, "fail to execute execute_query_for_num_vid_files for external cam 1 LQ");
        return -1;
    }
 
    val_string.clear();
    val_stream.clear();

    val_stream << "SELECT SUM(FILE_SIZE),COUNT(*) FROM VIDFILES WHERE NAME LIKE '%.ld.mp4' AND STATUS != "
        << CIRCULAR_BUFFER_STATUS_DELETE
        << " AND CAM_TYPE == "
        << CIRCULAR_BUFFER_CAM_EXT2
        << " AND FILE_SIZE > 0"
        << ";";

    val_string = val_stream.str();

    rc = execute_query_for_num_vid_files(val_string, &ext_2, &ext_2_size);
    if(rc < 0) {
        LOG_E(TAG, "fail to execute execute_query_for_num_vid_files for external cam 2 LQ");
        return -1;
    }

    val_string.clear();
    val_stream.clear();

    val_stream << "SELECT SUM(FILE_SIZE),COUNT(*) FROM VIDFILES WHERE NAME LIKE '%.ld.mp4' AND STATUS != "
        << CIRCULAR_BUFFER_STATUS_DELETE
        << " AND CAM_TYPE == "
        << CIRCULAR_BUFFER_CAM_EXT3
        << " AND FILE_SIZE > 0"
        << ";";

    val_string = val_stream.str();

    rc = execute_query_for_num_vid_files(val_string, &ext_3, &ext_3_size);
    if(rc < 0) {
        LOG_E(TAG, "fail to execute execute_query_for_num_vid_files for external cam 4 LQ");
        return -1;
    }
 
    val_string.clear();
    val_stream.clear();

    val_stream << "SELECT SUM(FILE_SIZE),COUNT(*) FROM VIDFILES WHERE NAME LIKE '%.ld.mp4' AND STATUS != "
        << CIRCULAR_BUFFER_STATUS_DELETE
        << " AND CAM_TYPE == "
        << CIRCULAR_BUFFER_CAM_EXT4
        << " AND FILE_SIZE > 0"
        << ";";

    val_string = val_stream.str();

    rc = execute_query_for_num_vid_files(val_string, &ext_4, &ext_4_size);
    if(rc < 0) {
        LOG_E(TAG, "fail to execute execute_query_for_num_vid_files for external cam 4 LQ");
        return -1;
    }
 
    LOG_D(TAG, "Num of LQ videos present for outward cam : %d ", front);
    LOG_D(TAG, "Num of LQ videos present for inward cam : %d ", back);
    LOG_D(TAG, "Num of LQ videos present for left cam : %d ", left);
    LOG_D(TAG, "Num of LQ videos present for right cam : %d ", right);
    LOG_D(TAG, "Num of LQ videos present for ext1 cam : %d ", ext_1);
    LOG_D(TAG, "Num of LQ videos present for ext2 cam : %d ", ext_2);
    LOG_D(TAG, "Num of LQ videos present for ext3 cam : %d ", ext_3);
    LOG_D(TAG, "Num of LQ videos present for ext4 cam : %d ", ext_4);

    ext = ext_1 + ext_2 + ext_3 + ext_4;
    ext_size = ext_1_size + ext_2_size + ext_3_size + ext_4_size;
    return 0;
}



int get_num_hq_vid_files(int32_t &front, int32_t &back, int32_t &left, int32_t &right, int32_t &ext, float& front_size, float& back_size, float& left_size, float& right_size, float& ext_size) {

    int rc = -1;
    bool ret = false;
    string val_string;
    std::stringstream val_stream;
    int32_t ext_1 = 0, ext_2 = 0, ext_3 = 0, ext_4 = 0;
    float ext_1_size = 0, ext_2_size = 0, ext_3_size = 0, ext_4_size = 0;

    val_stream << "SELECT SUM(FILE_SIZE),COUNT(*) FROM VIDFILES WHERE NAME LIKE '%_y.mp4' AND STATUS != "
        << CIRCULAR_BUFFER_STATUS_DELETE
        << " AND CAM_TYPE == "
        << CIRCULAR_BUFFER_CAM_FRONT
        << " AND FILE_SIZE > 0"
        << ";";

    val_string = val_stream.str();

    rc = execute_query_for_num_vid_files(val_string, &front, &front_size);
    if(rc < 0) {
        LOG_E(TAG, "fail to execute execute_query_for_num_vid_files for outward HQ");
        return -1;
    }

    val_string.clear();
    val_stream.clear();

    val_stream << "SELECT SUM(FILE_SIZE),COUNT(*) FROM VIDFILES WHERE NAME LIKE '%_y.mp4' AND STATUS != "
        << CIRCULAR_BUFFER_STATUS_DELETE
        << " AND CAM_TYPE == "
        << CIRCULAR_BUFFER_CAM_DRIVER
        << " AND FILE_SIZE > 0"
        << ";";

    val_string = val_stream.str();

    rc = execute_query_for_num_vid_files(val_string, &back, &back_size);
    if(rc < 0) {
        LOG_E(TAG, "fail to execute execute_query_for_num_vid_files for inward HQ");
        return -1;
    }

    val_string.clear();
    val_stream.clear();

    val_stream << "SELECT SUM(FILE_SIZE),COUNT(*) FROM VIDFILES WHERE NAME LIKE '%_y.mp4' AND STATUS != "
        << CIRCULAR_BUFFER_STATUS_DELETE
        << " AND CAM_TYPE == "
        << CIRCULAR_BUFFER_CAM_LEFT
        << " AND FILE_SIZE > 0"
        << ";";

    val_string = val_stream.str();

    rc = execute_query_for_num_vid_files(val_string, &left, &left_size);
    if(rc < 0) {
        LOG_E(TAG, "fail to execute execute_query_for_num_vid_files for left HQ");
        return -1;
    }

    val_string.clear();
    val_stream.clear();

    val_stream << "SELECT SUM(FILE_SIZE),COUNT(*) FROM VIDFILES WHERE NAME LIKE '%_y.mp4' AND STATUS != "
        << CIRCULAR_BUFFER_STATUS_DELETE
        << " AND CAM_TYPE == "
        << CIRCULAR_BUFFER_CAM_RIGHT
        << " AND FILE_SIZE > 0"
        << ";";

    val_string = val_stream.str();

    rc = execute_query_for_num_vid_files(val_string, &right, &right_size);
    if(rc < 0) {
        LOG_E(TAG, "fail to execute execute_query_for_num_vid_files for right HQ");
        return -1;
    }

    val_string.clear();
    val_stream.clear();

    val_stream << "SELECT SUM(FILE_SIZE),COUNT(*) FROM VIDFILES WHERE NAME LIKE '%_y.mp4' AND STATUS != "
        << CIRCULAR_BUFFER_STATUS_DELETE
        << " AND CAM_TYPE == "
        << CIRCULAR_BUFFER_CAM_EXT1
        << " AND FILE_SIZE > 0"
        << ";";

    val_string = val_stream.str();

    rc = execute_query_for_num_vid_files(val_string, &ext_1, &ext_1_size);
    if(rc < 0) {
        LOG_E(TAG, "fail to execute execute_query_for_num_vid_files for ext_1 HQ");
        return -1;
    }

    val_string.clear();
    val_stream.clear();

    val_stream << "SELECT SUM(FILE_SIZE),COUNT(*) FROM VIDFILES WHERE NAME LIKE '%_y.mp4' AND STATUS != "
        << CIRCULAR_BUFFER_STATUS_DELETE
        << " AND CAM_TYPE == "
        << CIRCULAR_BUFFER_CAM_EXT2
        << " AND FILE_SIZE > 0"
        << ";";

    val_string = val_stream.str();

    rc = execute_query_for_num_vid_files(val_string, &ext_2, &ext_2_size);
    if(rc < 0) {
        LOG_E(TAG, "fail to execute execute_query_for_num_vid_files for ext_2 HQ");
        return -1;
    }

    val_string.clear();
    val_stream.clear();

    val_stream << "SELECT SUM(FILE_SIZE),COUNT(*) FROM VIDFILES WHERE NAME LIKE '%_y.mp4' AND STATUS != "
        << CIRCULAR_BUFFER_STATUS_DELETE
        << " AND CAM_TYPE == "
        << CIRCULAR_BUFFER_CAM_EXT3
        << " AND FILE_SIZE > 0"
        << ";";

    val_string = val_stream.str();

    rc = execute_query_for_num_vid_files(val_string, &ext_3, &ext_3_size);
    if(rc < 0) {
        LOG_E(TAG, "fail to execute execute_query_for_num_vid_files for ext_3 HQ");
        return -1;
    }

    val_string.clear();
    val_stream.clear();

    val_stream << "SELECT SUM(FILE_SIZE),COUNT(*) FROM VIDFILES WHERE NAME LIKE '%_y.mp4' AND STATUS != "
        << CIRCULAR_BUFFER_STATUS_DELETE
        << " AND CAM_TYPE == "
        << CIRCULAR_BUFFER_CAM_EXT4
        << " AND FILE_SIZE > 0"
        << ";";

    val_string = val_stream.str();

    rc = execute_query_for_num_vid_files(val_string, &ext_4, &ext_4_size);
    if(rc < 0) {
        LOG_E(TAG, "fail to execute execute_query_for_num_vid_files for ext_4 HQ");
        return -1;
    }

    val_string.clear();
    val_stream.clear();

    LOG_D(TAG, "Num of HQ videos present for outward cam : %d ", front);
    LOG_D(TAG, "Num of HQ videos present for inward cam : %d ", back);
    LOG_D(TAG, "Num of HQ videos present for left cam : %d ", left);
    LOG_D(TAG, "Num of HQ videos present for right cam : %d ", right);
    LOG_D(TAG, "Num of HQ videos present for ext1 cam : %d ", ext_1);
    LOG_D(TAG, "Num of HQ videos present for ext2 cam : %d ", ext_2);
    LOG_D(TAG, "Num of HQ videos present for ext3 cam : %d ", ext_3);
    LOG_D(TAG, "Num of HQ videos present for ext4 cam : %d ", ext_4);
    
    ext = ext_1 + ext_2 + ext_3 + ext_4;
    ext_size = ext_1_size + ext_2_size + ext_3_size + ext_4_size;
    return 0;
}

int get_num_audio_files( int32_t &num_aud_files , float &size) {
    int rc;
    bool ret = false;
    string val_string;
    std::stringstream val_stream;
    
    val_stream << "SELECT SUM(FILE_SIZE),COUNT(*) FROM VIDFILES WHERE NAME LIKE '%.aac' AND STATUS != "
                << CIRCULAR_BUFFER_STATUS_DELETE
                << ";";

    val_string = val_stream.str();

    rc = execute_query_for_num_vid_files(val_string, &num_aud_files, &size);
    if(rc < 0) {
        LOG_E(TAG, "fail to get audio files count");
        num_aud_files = -1;
        size = -1.0;
        return -1;
    }

    LOG_D(TAG, "Audio files count : %d", num_aud_files);
    return 0;
}

int get_Observation_files_details(int32_t& count, float& size) {
    int rc;
    bool ret = false;
    string val_string;
    std::stringstream val_stream;

    val_stream << "SELECT SUM(FILE_SIZE),COUNT(*) FROM VIDFILES WHERE NAME LIKE '%.zip' AND STATUS != "
                << CIRCULAR_BUFFER_STATUS_DELETE
                <<";";
    
    val_string = val_stream.str();
    
    count_size_data_t count_size;
    memset(&count_size, 0, sizeof(count_size_data_t));
    rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string,
                     callback_get_count_and_size, (void*)&count_size);
    if(rc == false){
        LOG_E(TAG, "failed to execute");
        count = -1;
        size = -1.0;
        return -1;
    }

    LOG_D(TAG, "count_size Return values %ld %f", count_size.count, count_size.size);
    size = count_size.size;
    count = count_size.count;
    return 0;
}

int get_partial_files_info(int32_t &front, int32_t &back, int32_t &left, int32_t &right, float& front_size, float& back_size, float& left_size, float& right_size) {

    string val_string1 = "", val_string2 = "", val_string3 = "", val_string4 = "";
    std::stringstream val_stream1, val_stream2, val_stream3, val_stream4;
    int rc = -1;
    bool ret = false;
    const char *sql;
    count_size_data_t count_size;
    memset(&count_size, 0, sizeof(count_size_data_t));

    val_stream1 << "SELECT SUM(file_size),COUNT(*) FROM VIDFILES WHERE NAME LIKE '%_y.mp4' AND STATUS != "
                << CIRCULAR_BUFFER_STATUS_DELETE
                << " AND CAM_TYPE == "
                <<  CIRCULAR_BUFFER_CAM_FRONT
                << " AND FILE_SIZE > 0"
                << " AND FILE_SIZE < 43600000;";
    
    val_string1 = val_stream1.str();
    sql = val_string1.c_str();

    LOG_D(TAG, "CAM 0 query : %s", val_string1.c_str());
    rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, sql, callback_get_count_and_size,
            (void*)&count_size);
    
    LOG_D(TAG, "CAM_0 count_size Return values %ld %f", count_size.count, count_size.size);

    front = count_size.count;
    front_size = count_size.size;
    if(front == 0) {
        front_size = 0.0;
    }

    memset(&count_size, 0, sizeof(count_size_data_t));

    val_stream2 << "SELECT SUM(file_size),COUNT(*) FROM VIDFILES WHERE NAME LIKE '%_y.mp4' AND STATUS != "
                << CIRCULAR_BUFFER_STATUS_DELETE
                << " AND CAM_TYPE == "
                <<  CIRCULAR_BUFFER_CAM_DRIVER 
                << " AND FILE_SIZE > 0"
                << " AND FILE_SIZE < 14155776;";
    
    val_string2 = val_stream2.str();
    sql = val_string2.c_str();

    LOG_D(TAG, "CAM 1 query : %s", val_string2.c_str());
    rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, sql, callback_get_count_and_size,
            (void*)&count_size);
    
    LOG_D(TAG, "CAM 1 count_size Return values %ld %f", count_size.count, count_size.size);
    back = count_size.count;
    back_size = count_size.size;
    if(back == 0) {
        back_size = 0.0;
    }
    memset(&count_size, 0, sizeof(count_size_data_t));

    
    val_stream3 << "SELECT SUM(file_size),COUNT(*) FROM VIDFILES WHERE NAME LIKE '%_y.mp4' AND STATUS != "
                << CIRCULAR_BUFFER_STATUS_DELETE
                << " AND CAM_TYPE == "
                <<  CIRCULAR_BUFFER_CAM_LEFT
                << " AND FILE_SIZE > 0"
                << " AND FILE_SIZE < 3355443;";
    val_string3 = val_stream3.str();
    sql = val_string3.c_str();

    LOG_D(TAG, "CAM 2 query : %s", val_string3.c_str());
    rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, sql, callback_get_count_and_size,
            (void*)&count_size);
    
    LOG_D(TAG, "CAM 2 count_size Return values %ld %f", count_size.count, count_size.size);
    left = count_size.count;
    left_size = count_size.size;
    if(left == 0) {
        left_size = 0.0;
    }
    
    memset(&count_size, 0, sizeof(count_size_data_t));

    val_stream4 << "SELECT SUM(file_size),COUNT(*) FROM VIDFILES WHERE NAME LIKE '%_y.mp4' AND STATUS != "
                << CIRCULAR_BUFFER_STATUS_DELETE
                << " AND CAM_TYPE == "
                <<  CIRCULAR_BUFFER_CAM_RIGHT 
                << " AND FILE_SIZE > 0"
                << " AND FILE_SIZE < 3355443;";
    val_string4 = val_stream4.str();
    sql = val_string4.c_str();

    LOG_D(TAG, "CAM 3 query : %s", val_string4.c_str());
    rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, sql, callback_get_count_and_size,
            (void*)&count_size);
    
    LOG_D(TAG, "CAM 3 count_size Return values %ld %f", count_size.count, count_size.size);
    right = count_size.count;
    right_size = count_size.size;
    if(right == 0) {
        right_size = 0.0;
    }

    LOG_D(TAG, "num partial file 0 = %ld", front);
    LOG_D(TAG, "num partial file 1 = %ld", back);
    LOG_D(TAG, "num partial file 2 = %ld", left);
    LOG_D(TAG, "num partial file 3 = %ld", right);
    LOG_D(TAG, "partial file size 0 = %f", front_size);
    LOG_D(TAG, "partial file size 1 = %f", back_size);
    LOG_D(TAG, "partial file size 2 = %f", left_size);
    LOG_D(TAG, "partial file size 3 = %f", right_size);
    return 0;
}

std::vector<std::string> nd_split(const std::string& s, char del) {
    std::stringstream ss(s);
    std::string word;
    std::vector<std::string> words;

    while (getline(ss, word, del)) {
        words.push_back(word);
    }

    return words;
}
// Validate filename and if folder is also present in it, remove folder name
void validate_filename(string &org_filename) {
    string filename = "", folder_name = "";
    LOG_I(TAG, "Original filename %s", org_filename.c_str());
    get_folder_file_names(org_filename, folder_name, filename);
    if(filename.empty()) {
        LOG_E(TAG,"Invalid filename %s", org_filename.c_str());
        string err_msg = "Invalid filename " + string(org_filename);
    }
    if(folder_name.empty()) {
        return ;
    }
    org_filename = filename;
    LOG_I(TAG,"Validated filename %s", org_filename.c_str());
}

int get_oldest_video_details(int64_t &ts, float &lat, float &lon, string &udid, string &fileName) {
    int rc;
    bool ret = false;
    const char *sql;
    alldata_str_t data = {-1, ""};
    string val_string;
    std::stringstream val_stream;

    // Initialize the variables with invalid values
    ts = -1;
    lat = 91.0;
    lon = 181.0;
    udid = "";

    val_stream << "SELECT min(INDEXID), NAME from VIDFILES where STATUS != "
                << CIRCULAR_BUFFER_STATUS_DELETE
                << ";";
    val_string = val_stream.str();
    sql = val_string.c_str();

    rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, sql, callback_indx_filename,
            (void*)&(data));
    if(rc == false){
        LOG_E(TAG, "failed to execute");
        return -1;
    }

    LOG_D(TAG,"Index id : %ld file : %s", data.index_id, data.file_name.c_str());
    fileName = data.file_name;
    validate_filename(fileName); // This is to ensure the filename is in the expected format, and to remove folder path if present.
    data.file_name = fileName; // Update the filename after validation.
    std::vector<std::string> fields = nd_split(data.file_name, '_');
    for (const std::string& field : fields) {
        LOG_I(TAG," %s",field.c_str());
    }
    if(fields.size() < FIELDS_PRESENT_IN_FILENAME) { // Incase the filename does not have enough fields, invalid value will be returned.
        LOG_E(TAG, "Invalid file name format: %s", data.file_name.c_str());
        return -1;
    }
    int camera_id = -1;
    if(false == string_to_integer(fields[POS_CAMERA_ID_IN_FILENAME],camera_id)) {
        LOG_C(TAG, "Unable to convert string to integer: %s", data.file_name.c_str());
        camera_id = -1;
    }
    if((camera_id < CIRCULAR_BUFFER_CAM_FRONT) || (camera_id > CIRCULAR_BUFFER_CAM_DMS)){
        LOG_C(TAG, "Invalid camera id in file name: %s", data.file_name.c_str());
    }
    if(string_to_int64(fields[POS_TIMESTAMP_IN_FILENAME], ts) == false) {
        LOG_E(TAG, "Invalid timestamp in file name: %s", data.file_name.c_str());
        ts = -1;
    }
    if(string_to_float(fields[POS_LATITUDE_IN_FILENAME], lat) == false) {
        LOG_E(TAG, "Invalid latitude in file name: %s", data.file_name.c_str());
        lat = 91.0;
    }
    if(string_to_float(fields[POS_LONGITUDE_IN_FILENAME], lon) == false) {
        LOG_E(TAG, "Invalid longitude in file name: %s", data.file_name.c_str());
        lon = 181.0;
    }
    if(fields[POS_UDID_IN_FILENAME].length() < 5) {
        LOG_E(TAG, "Invalid UDID in file name: %s", data.file_name.c_str());
        udid = "";
    }else{
        udid = fields[POS_UDID_IN_FILENAME].substr(4); // Remove the 'trip' prefix from the UDID field.
    }
    LOG_I(TAG,"udid is %s",udid.c_str());
    return 0;
}

int get_available_video_storage_min(void)
{
    int rc;
    bool ret = true;

    string val_string;
    std::stringstream val_stream;

    val_stream <<
                "SELECT FILE_SIZE from VIDFILES WHERE STATUS != "
                << CIRCULAR_BUFFER_STATUS_DELETE
                << ";"; 
    val_string = val_stream.str();
    
    int64_t count_size[3] = {0, 0, 0};
    rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string,
                     callback_count_size, (void*)&count_size[0]);
    if(rc == false){
        LOG_E(TAG, "failed to execute");
        return false;
    }

    LOG_D(TAG, "count_size Return values %lld %lld %lld", count_size[0], count_size[1], count_size[2]);
    int64_t size_vid_kb = count_size[1] / 1024;
    int64_t size_meta = 0;

    val_stream.clear();
    
    int64_t num_hq_videos_db = 0;
    int64_t filling_limit_kb = FILLING_LIMIT/1024;
    int64_t full_session_size_kb = 79872;
    int64_t lq_session_size_kb = 19456;
    int64_t max_hq_vid_limit = 120;
    circular_buffer_camtype_t camtype = CIRCULAR_BUFFER_CAM_FRONT;
    rc = get_num_hq_videos_db(&num_hq_videos_db, camtype);

    if (rc == false) {
        LOG_E(TAG, "Failed to get number of hq videos in db in transcode thread");
    }

    LOG_D(TAG, "size_vid : %lld : %lld", count_size[1], size_vid_kb);
    LOG_D(TAG, "FILLING_LIMIT : %lld : %lld", FILLING_LIMIT, filling_limit_kb);
    LOG_D(TAG, "num_hq_videos_db : %lld", num_hq_videos_db);
    if(size_vid_kb >= filling_limit_kb) {
        LOG_I(TAG, "Available video storage(min) : 0");
        return 0;
    } else {
        if(num_hq_videos_db >= max_hq_vid_limit) {
            LOG_I(TAG, "Available video storage(min) : %d", ((filling_limit_kb - size_vid_kb)/lq_session_size_kb));
            return ((filling_limit_kb - size_vid_kb)/lq_session_size_kb);
        } else if (num_hq_videos_db < max_hq_vid_limit) {
            LOG_I(TAG, "Available video storage(min) : %d", ((max_hq_vid_limit - num_hq_videos_db) + ((filling_limit_kb - (full_session_size_kb * max_hq_vid_limit))/lq_session_size_kb)));
            return ((max_hq_vid_limit - num_hq_videos_db) + ((filling_limit_kb - (full_session_size_kb * max_hq_vid_limit))/lq_session_size_kb));
        }
    }
    return 0;
}

int callback_get_file_data_list(void* data, int argc, char** argv, char** azColName) {
    if (NULL == data) return 0;

    auto* file_data_list = static_cast<std::vector<file_info_str>*>(data);
    if (NULL == file_data_list) return 0; // Null check after casting

    file_info_str file_data = {-1, -1, -1, -1, ""};

    // Order: INDEXID, NAME, FILE_TYPE, FILE_SIZE
    if(argv == NULL) {
        LOG_E(TAG, "No data found in the database.");
        return 0; // No data to process
    }
    int temp = 0;
    // INDEXID
    if (NULL != argv[0]) {
        if(false == string_to_integer(argv[0], temp)) {
            LOG_E(TAG, "Unable to convert string to integer: %s", argv[0]);
            temp = -1;
        }
        file_data.index_id = temp;
    }
    else{
        file_data.index_id = -1;
    }
    // NAME
    if (NULL != argv[1]) {
        file_data.file_name = argv[1];
    }
    else {
        file_data.file_name = "";
    }
    // FILE_TYPE
    if (NULL != argv[2]) {
        if (false == string_to_integer(argv[2], temp)) {
            LOG_E(TAG, "Unable to convert string to integer: %s", argv[2]);
            temp = -1;
        }
        file_data.file_type = temp;
    } else {
        file_data.file_type = -1;
    }
    // FILE_SIZE
    int64_t temp_size = -1;
    if (NULL != argv[3]) {
        if (false == string_to_int64(argv[3], temp_size)) {
            LOG_E(TAG, "Unable to convert string to int64: %s", argv[3]);
            temp_size = -1;
        }
        file_data.file_size = temp_size;
    } else {
        file_data.file_size = -1;
    }
    // FILE_COMPRESSION
    if (NULL != argv[4]) {
        if (false == string_to_integer(argv[4], temp)) {
            LOG_E(TAG, "Unable to convert string to integer: %s", argv[4]);
            temp = CIRCULAR_BUFFER_MEDIUM_COMPRESSION;
        }
        file_data.file_compression = temp;
    } else {
        file_data.file_compression = CIRCULAR_BUFFER_MEDIUM_COMPRESSION;
    }
    // Only add to list if file_name is not empty and index is not -1
    if ((!file_data.file_name.empty()) && (file_data.index_id != -1)) {
        file_data_list->push_back(file_data);
    }
    return 0;
}

bool delete_whole_session_from_db_and_sdcard(string base_file_name){
    bool ret = false;
    string folder_name = "", file_name = "";
    get_folder_file_names(base_file_name, folder_name, file_name);
    if(file_name.empty()){
        LOG_E(TAG, "Invalid filename %s", base_file_name.c_str());
        return false;
    }
    size_t y_pos = file_name.find("_y.");
    if (y_pos == string::npos) {
        LOG_E(TAG, "Filename %s does not contain '_y.'", file_name.c_str());
        return false;
    }

    string filename_matching = file_name.substr(0, y_pos); // to match all file
    vector<file_info_str> file_data_list;
    if (!matching_file_list_info_from_xattrs(filename_matching, file_data_list)) {
        file_data_list.clear();
        LOG_I(TAG, "Falling back to DB query for session deletion for file %s", file_name.c_str());

        string filename_matching_str = file_name.substr(0, y_pos) + "%"; // to match all file
        filename_matching_str[0] = '%'; // to match all camera types for this session
        LOG_I(TAG, "Deleting whole session for file matching string %s", filename_matching_str.c_str());
        string val_string;
        std::stringstream val_stream;
        val_stream << "SELECT INDEXID, NAME, TYPE, FILE_SIZE, FILE_COMPRESSION  from VIDFILES WHERE NAME LIKE '" << filename_matching_str << "' AND STATUS != " << CIRCULAR_BUFFER_STATUS_DELETE;
        val_string = val_stream.str();

        int rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string,
                            callback_get_file_data_list, (void*)&file_data_list);
        if(rc == false){
            LOG_E(TAG, "failed to execute query");
            return false;
        }
    }
    LOG_I(TAG, "Number of files to be deleted for this session %d", file_data_list.size());
    int limit_cnt = 0;
    bool is_session_alert = false;
    for(const auto& file_data : file_data_list){
        if(limit_cnt >= MAX_NO_OF_FILE_TO_DELETE_AT_ONE_GO){
            LOG_I(TAG, "Reached max limit of files to delete in one go %d", MAX_NO_OF_FILE_TO_DELETE_AT_ONE_GO);
            break;
        }
        if(file_data.file_compression != CIRCULAR_BUFFER_NO_COMPRESSION){
            LOG_I(TAG, "Deleting name %s, type %d, size %lld", file_data.file_name.c_str(), file_data.file_type, file_data.file_size);
            // Delete entry from DB
            delete_entry_from_db(file_data.file_name, file_data.file_type);
            // Delete file form SD card
            // Don't take decision for observation files here, it needs to be decided based on if sesion has alert or not.
            bool del_status = true;
            if(file_data.file_name.find(".zip") == string::npos){
                del_status = delete_file_from_sdcard(file_data.file_name);
            }
            ret = ret || del_status;
            if(del_status == false){
                LOG_E(TAG, "Failed to delete file %s", file_data.file_name.c_str());
            }
            limit_cnt++;
        }
        else{
            LOG_I(TAG, "Not deleting name %s, as it is alert file %d", file_data.file_name.c_str(), file_data.file_compression);
            is_session_alert = true;
        }
    }
    // Check if session has alert, if NO then delete the observation file, if YES then keep the observation and mark the status as delete in extended attributes
    if(is_session_alert == false){
        string obs_file_name = filename_matching + "_y.zip";
        LOG_I(TAG, "Session does not have alert file, deleting observation file %s", obs_file_name.c_str());
        // Delete file form SD card
        bool del_status = delete_file_from_sdcard(obs_file_name);
        ret = ret || del_status;
        if(del_status == false){
            LOG_E(TAG, "Failed to delete observation file %s", obs_file_name.c_str());
        }
    }else{
        string obs_file_full_name = nd_device_obj->get_external_eMMC_mount_path() + filename_matching + "_y.zip";
        LOG_I(TAG, "Session has alert file, marking observation file %s as deleted in xattr", obs_file_full_name.c_str());
        circular_buffer_filestatus_t status = CIRCULAR_BUFFER_STATUS_DELETE;
        if(set_file_xattr(obs_file_full_name, FileMetadataKey::status, &status, sizeof(status)) == 0) {
            LOG_D(TAG, "Successfully updated status xattr for file: %s", obs_file_full_name.c_str());
        }
    }
    return ret;
}

const int64_t get_min_safe_outward_ld_files(){
    NDDeviceTypeT device_type = nd_device_obj->getBuildDeviceType();
    if(device_type == NDDeviceTypeT::krait || device_type == NDDeviceTypeT::krait2){
        return MIN_SAFE_OUTWARD_LD_FILES_D2XX;
    }
    return MIN_SAFE_OUTWARD_LD_FILES_D4XX; // D4XX and others will have same number of minimum safe files, 100 hr
}

bool check_free_space_less_than_reserved(){
    int64_t free_space = file_getfreespace_err(nd_device_obj->get_external_eMMC_mount_path().c_str());
    if((free_space <  MIN_EMMC_FREE_SPACE) || (free_space > MAX_EMMC_FREE_SPACE)){
        LOG_E(TAG, "Free space value from file_getfreespace_err is invalid : %lld range : (%lld, %lld), using backup method to determine free space", free_space, MIN_EMMC_FREE_SPACE, MAX_EMMC_FREE_SPACE);
        // Call backup function which uses number of outward LD files to check free space
        string cmd_str = "echo " + nd_device_obj->get_external_eMMC_mount_path() + "0_trip*.ld.mp4 | wc -w" ; // command to check the number of outward ld files present in DB
        string output = "";
        if(false == system_execute_with_resp(TAG, cmd_str, output)){
            LOG_E(TAG, "Failed to execute command to get number of files present in DB : %s", cmd_str.c_str());
        }else{
            int64_t num_files = -1;
            if((false == string_to_int64(output, num_files)) || (num_files < 0)){
                LOG_E(TAG, "Failed to convert string to int64 : %s", output.c_str());
            }
            if(num_files <= get_min_safe_outward_ld_files()){
                LOG_I(TAG, "Number of files present in DB is less than minimum threshold : %lld < %d, no need to delete files", num_files, get_min_safe_outward_ld_files());
                return false;
            }
        }
        return true; // Delete by default
    }
    if(free_space > RESERVED_SPACE){
        return false;
    }
    LOG_I(TAG, "Free space less than reserved space, need to delete old files %lld < %lld", free_space, RESERVED_SPACE);
    return true;
}

bool add_file_normal(sqlite3 *db, char* base_file_name, int file_size, int session_deleted)
{
    if(session_deleted == MAX_SESSION_TO_DELETE_FOR_ADD_FILE_DB){
        return true;
    }

    int rc;
    bool ret = true;

    string val_string;
    alldata_str_t data = {-1, ""};
    std::stringstream val_stream;
#if 0
    val_stream <<
                "SELECT FILE_SIZE from VIDFILES WHERE STATUS != "
                << CIRCULAR_BUFFER_STATUS_DELETE; 
    val_string = val_stream.str();
    int64_t count_size[3] = {0, 0, 0};
    rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string,
                     callback_count_size, (void*)&count_size[0]);
    if(rc == false){
        LOG_E(TAG, "failed to execute");
        return false;
    }

    LOG_D(TAG, "count_size Return values %lld %lld %lld, filling_limit = %lld", count_size[0], count_size[1], count_size[2], (int64_t)FILLING_LIMIT);
    int64_t size_vid = count_size[1];
    int64_t size_meta = 0;
#endif
    
    if(false == check_free_space_less_than_reserved()){ // Only check if free space is less than backup space
        // change type to norm in DB without deleting any entry
        LOG_I(TAG, "Adding without deleting: free space is not less than RESERVED SPACE");
        ret = true;
    }
#if 0 // Commenting this as query itself removed
    else if(count_size[0] == 0) {
        // this means the cb list is empty and u cant delete any more files
        LOG_E(TAG, "Cant accomdate this file deleting without storing");
        ret = false;
    }
#endif 
    else {
        // logic to delete for oldest file/s with NORM type to accomdate present file
        LOG_I(TAG, "Free space less than reserved space deleting oldest session");
        val_stream.str("");
        val_stream.clear();
        val_stream << "SELECT min(INDEXID), NAME from VIDFILES WHERE STATUS != " << CIRCULAR_BUFFER_STATUS_DELETE << " AND FILE_COMPRESSION != " << CIRCULAR_BUFFER_NO_COMPRESSION;
        val_string = val_stream.str();
        int64_t count_indx[3] = {0, 0, 0};
        rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string, 
                                callback_indx_filename, ((void*)&data));
        if(rc == false){
            LOG_E(TAG, "failed to execute");
            return false;
        }
        if((data.index_id == -1) || (data.file_name.empty())){
            LOG_E(TAG, "Index id/filename invalid or all file present in DB are marked as deleted, return true keeping the file in this scenario");
            return true;
        }
        LOG_I(TAG, "callback_count_index: index %d, filename %s",
                 data.index_id, data.file_name.c_str());
        val_stream.str("");
        val_stream.clear();
        bool del_status = false;

        // Compare the oldest file with files present in misc_insert_failed_files
        if(!misc_insert_failed_files.empty()){
            int64_t oldest_udid_db = udid_from_file(data.file_name);
            int64_t oldest_session_count_db = sessionCount_from_file(data.file_name);
            for (auto it = misc_insert_failed_files.begin(); it != misc_insert_failed_files.end(); ++it) {
                if ((it->udid < oldest_udid_db) ||
                    ((it->udid == oldest_udid_db) && (it->sessionCount < oldest_session_count_db))) {
                    LOG_C(TAG, "Oldest file in DB is present in misc_insert_failed_files, deleting %s", it->base_file_name);
                    del_status = delete_file_from_sdcard(it->base_file_name);
                    misc_insert_failed_files.erase(it); // Remove the file from the list after deletion, regardless of whether deletion was successful or not, to avoid repeatedly trying to delete the same file.
                    break; // Break after deleting one file, as we only want to delete one file at a time to accommodate the new file.
                }
            }
            // Oldest file is not present in misc_insert_failed_files, break the loop and proceed with normal deletion flow
        }
        
        if(false == del_status){
            // Delete the whole session, which matches with the oldest uploadable file in DB
            del_status = delete_whole_session_from_db_and_sdcard(data.file_name);
        }
#if 0 // Commenting this code as we are deleting whole session above
        val_stream << "SELECT * from VIDFILES WHERE INDEXID == " << count_indx[1];
        val_string = val_stream.str();
        circular_buffer_file_t fileinfo;
        rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string, 
                            callback_complete_fileinfo, (void*)&fileinfo);
        if(rc == false){
            LOG_E(TAG, "failed to execute");
            return false;
        }
        delete_entry_from_db(fileinfo.index_id, fileinfo.file_type);
        string file_name(fileinfo.base_file_name);
        bool del_status = delete_file_from_sdcard(file_name);
#endif
        if(del_status == false){
            delete_entry_from_db(data.index_id, CIRCULAR_BUFFER_TYPE_ERROR); //marking as deleted in DB to skip in next query
            LOG_E(TAG, "Failed to delete oldest session : %s. Marking as deleted in DB", data.file_name.c_str());
        }
        session_deleted++;//bounding recursion to 2 trials

        // calling twice recursively add_file_normal till we have space to accommodate new file
        ret = add_file_normal(db, base_file_name, file_size, session_deleted);
    }
    return ret;
}
// This function is used to sort the misc_insert_failed_files list based on udid and session count.
// Used inside insert_batch_file_DB function when batch insertion to DB fails
// After reaching filling limit oldest file will be compared with this list incase if list is not empty, to decide whether delete file from DB or from  
bool compare_udid_session_count(const circular_buffer_fileinfo_t &file1, const circular_buffer_fileinfo_t &file2){
    if(file1.udid == file2.udid){
        return file1.sessionCount < file2.sessionCount; // If udid is same, sort based on session count
    }
    return file1.udid < file2.udid; // Sort based on udid
}
// This batch insertion is used only in case of MISC addition / DB Corruption scenario where we have multiple files to be added in DB at one go, and we want to optimize DB insertion by doing it in batch instead of one by one.
bool insert_batch_file_DB(circular_buffer_fileinfo_t fileinfo, int batch_mode, circular_buffer_filestatus_t status, circular_buffer_file_compression_t fc_type){
    static vector<tuple<circular_buffer_fileinfo_t, circular_buffer_filestatus_t, circular_buffer_file_compression_t>> fileinfo_batch;
    // Flag used to indicate whether to flush the batch or add to batch, in normal scenario it will be false and batch will be flushed when batch size reaches threshold or this flag is true.
    // Function will be called with flush_batch true when we want to flush, after processing all files.
    if(BATCH_MODE_FLUSH != batch_mode){
        if((fileinfo.camtype == CIRCULAR_BUFFER_CAM_DMS) && (store_lq_dms_file == false)){
            status = CIRCULAR_BUFFER_STATUS_NOTIFIED; // DMS files should not be added in cloud video list if store_lq_dms_file is false.
        }
        fileinfo_batch.push_back({fileinfo, status, fc_type});
    }
    int batch_size = fileinfo_batch.size();
    if((batch_size >= BATCH_SIZE_FOR_DB_INSERTION) || ((BATCH_MODE_FLUSH == batch_mode) && (batch_size > 0))){
        std::stringstream val_stream;
        LOG_I(TAG, "Inserting batch of %zu rows in DB", fileinfo_batch.size());
        for(int i = 0; i < batch_size; i++){
            const auto &f_info = std::get<0>(fileinfo_batch[i]);
            const auto &f_status = std::get<1>(fileinfo_batch[i]);
            const auto &f_compression = std::get<2>(fileinfo_batch[i]);
            val_stream << "("
                       << f_info.time << ", "
                       << "'" << f_info.base_file_name << "'" << ", "
                       << f_info.file_size << ", "
                       << f_info.file_type << ", "
                       << f_status << ", "
                       << f_info.camtype << ", "
                       << f_info.duration << ", "
                       << f_info.udid << ", "
                       << f_info.sessionCount << ", "
                       << f_info.tc_status << ", "
                       << f_compression << ", "
                       << f_info.upl_vid_enabled << ", "
                       << f_info.rec_vid_enabled
                       << ")";
            if(i != batch_size - 1) val_stream << ", "; // comma between rows
        }

        string val_string = insert_str_with_file_compression + val_stream.str() + ";";

        bool ret = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string, NULL, 0);

        if(!ret){
            LOG_E(TAG, "Batch insert failed! Discarding batch. Error inserting batch of %zu rows in DB", fileinfo_batch.size());
            // In case of failure also clearing the batch to avoid retrying with same data.
            // This batch of files will get inserted in next boot, when the files comes as misc again
            // Insert this batch into another global vector, check if files from this batch needs to be deleted based on how old the file is
            if(misc_insert_failed_files.size() < MAX_MISC_INSERT_FAILED_FILES){ // Vector can grow to max size of MAX_MISC_INSERT_FAILED_FILES + BATCH_SIZE_FOR_DB_INSERTION
                for (const auto &entry : fileinfo_batch) {
                    misc_insert_failed_files.push_back(std::get<0>(entry));
                }
                // sort the misc files list based on udid and session count
                sort(misc_insert_failed_files.begin(), misc_insert_failed_files.end(), compare_udid_session_count);
                LOG_I(TAG, "Added %zu rows to misc_insert_failed_files list, total files in the list is now %zu", fileinfo_batch.size(), misc_insert_failed_files.size());
            }
            fileinfo_batch.clear();
            return false;
        }

        LOG_I(TAG, "Inserted %zu rows in batch", fileinfo_batch.size());

        fileinfo_batch.clear();
        return ret;
    }

    return true;
}

void send_critical_info(err_code_t err_code,int aux_code,string err_msg){
    LOG_C(TAG,err_msg.c_str());
    if(no_of_critical_info_send < MAX_NO_OF_CRITICAL_INFO){
        nd_service_obj->send_err_msg(err_code,aux_code,err_msg);
        no_of_critical_info_send++;
    }
    return ;
}
// Validate privacy status and change to Default Privacy in case of invalid
bool validate_upl_and_rec_vid_enabled(circular_buffer_fileinfo_t &fileinfo, bool& deleteFile ){
	LOG_D(TAG,"File: %s file type: %d, ", fileinfo.base_file_name, fileinfo.file_type);
    switch(fileinfo.file_type){
		case CIRCULAR_BUFFER_TYPE_TEXT_ALERTS: 
			{
				LOG_D(TAG,"setting default value for File %s file type: %d, ", fileinfo.base_file_name, fileinfo.file_type);
				fileinfo.upl_vid_enabled = DEFAULT_VALUE_UPL_VID_ENABLED_COLUMN;
				fileinfo.rec_vid_enabled = DEFAULT_VALUE_REC_VID_ENABLED_COLUMN;
				return true;
			}
		case CIRCULAR_BUFFER_TYPE_NORMAL: 
		case CIRCULAR_BUFFER_TYPE_TEXT: 
			{
				if(fileinfo.upl_vid_enabled != UPL_VID_ENABLED_RESET && fileinfo.upl_vid_enabled != UPL_VID_ENABLED_SET && fileinfo.upl_vid_enabled != UPL_VID_ENABLED_DO_NOT_UPDATE){
					LOG_E(TAG,"Invalid value received in upl_vid_enabled changing it to default");
					fileinfo.upl_vid_enabled = DEFAULT_VALUE_UPL_VID_ENABLED_COLUMN;
				}
				if(fileinfo.rec_vid_enabled != REC_VID_ENABLED_RESET && fileinfo.rec_vid_enabled != REC_VID_ENABLED_SET && fileinfo.rec_vid_enabled != REC_VID_ENABLED_DO_NOT_UPDATE){
					LOG_E(TAG,"Invalid value received in rec_vid_enabled changing it to default");
					fileinfo.rec_vid_enabled = DEFAULT_VALUE_REC_VID_ENABLED_COLUMN;
				}
				if(fileinfo.rec_vid_enabled == REC_VID_ENABLED_RESET){
					fileinfo.upl_vid_enabled = UPL_VID_ENABLED_RESET; // If rec_vid_enabled is zero then upl_vid_enabled should also be zero
				}
				return true;
			}
/*
		case CIRCULAR_BUFFER_TYPE_ALERT :
			{
				LOG_I(TAG,"Alert File %s file type: %d, Please handle it.", fileinfo.base_file_name, fileinfo.file_type);
				fileinfo.upl_vid_enabled = DEFAULT_VALUE_UPL_VID_ENABLED_COLUMN;
				fileinfo.rec_vid_enabled = DEFAULT_VALUE_REC_VID_ENABLED_COLUMN;
				return true;
			}
*/
        default: 
            {
                LOG_E(TAG,"File %s Unknown file type: %d", fileinfo.base_file_name, fileinfo.file_type);
                bool delStatus = true;
                string errMsg = std::string("Error: ") + fileinfo.base_file_name;  
                if(deleteUnkownFileType == true) {
                    LOG_E(TAG,"Deleting flie %s, Unknown file type: %d", fileinfo.base_file_name, fileinfo.file_type);
                    delStatus = file_delete(nd_device_obj->get_external_eMMC_mount_path() + "/" + fileinfo.base_file_name);
                    deleteFile = true;
                }
                int auxCode = fileinfo.file_type ;
                if(delStatus == false) {
                    auxCode = -auxCode;
                }
                nd_service_obj->send_err_msg(SM_E_CB_UNKNOWN_FILE_TYPE, auxCode, errMsg);
                fileinfo.upl_vid_enabled = DEFAULT_VALUE_UPL_VID_ENABLED_COLUMN;
                fileinfo.rec_vid_enabled = DEFAULT_VALUE_REC_VID_ENABLED_COLUMN;
                return false;
            }

	}
	return true;
}

bool validate_fileinfo(circular_buffer_fileinfo_t &fileinfo, bool& deleteFile){
    // check camera_id in filename is correct
    int camera_id = get_cam_num_from_filename(fileinfo.base_file_name);
    if(!((camera_id >= CIRCULAR_BUFFER_CAM_FRONT) and (camera_id <= CIRCULAR_BUFFER_CAM_DMS))){
        string err_msg = "Invalid camera id present in filename. camera id -> " + to_string(camera_id);
        send_critical_info(SM_E_CB_FILENAME_WITH_INVALID_CAM_NO,camera_id,err_msg);
        return false;
    }
    string src_file = fileinfo.base_file_name;
    size_t position = src_file.find_last_of(".");
    string extn = "";
    if ( position != src_file.npos ) {
        extn = src_file.substr( src_file.find_last_of(".") );
    }
    // check for file size
    string file_type = to_string(camera_id);
    if(strstr(fileinfo.base_file_name,ld_extn.c_str()) != NULL){
            file_type += "_ld";
    }else if(strstr(fileinfo.base_file_name,".zip")){
        file_type += "_zip";
    }else if(strstr(fileinfo.base_file_name,".aac")){
        file_type += "_aac";
    }
    if(CIRC_BUFF_ctx->camera_video_file_size_bytes.find(file_type) != CIRC_BUFF_ctx->camera_video_file_size_bytes.end()) {
        string err_msg = "file size for " + file_type;
        if(fileinfo.file_size < CIRC_BUFF_ctx->camera_video_file_size_bytes[file_type].min_file_size and fileinfo.rec_vid_enabled == REC_VID_ENABLED_SET){
            err_msg += ", is less than expected " + std::to_string(fileinfo.file_size);
            send_critical_info(SM_E_CB_FILESIZE_LESS_THAN_EXPECTED,fileinfo.file_size,err_msg);
        }
        else if( fileinfo.file_size > CIRC_BUFF_ctx->camera_video_file_size_bytes[file_type].max_file_size) {
            err_msg += ", is more than expected " + std::to_string(fileinfo.file_size);
            send_critical_info(SM_E_CB_FILESIZE_MORE_THAN_EXPECTED,fileinfo.file_size,err_msg); // Discuss and raise a critical event if required
        }else{
            LOG_D(TAG,"file size in the expected range, file size -> %d",fileinfo.file_size);
        }
    }else{
        LOG_E(TAG,"file size not present in dictionary for file type %s",file_type.c_str());
    }
    // check cam_type present in fileinfo
    if(fileinfo.camtype < CIRCULAR_BUFFER_CAM_FRONT || fileinfo.camtype > CIRCULAR_BUFFER_CAM_DMS){
        // Intialize the cam type from camera id in filename (issue mostly seen in observation files)
        string err_msg = "changing invalid cam_type in fileinfo to camera id "+ to_string(fileinfo.camtype) + "->" + to_string(camera_id);
        send_critical_info(SM_E_CB_INVALID_CAM_TYPE_PRESENT_IN_FILEINFO,fileinfo.camtype,err_msg);
        fileinfo.camtype = (circular_buffer_camtype_t) camera_id;
    }
    // what should be done in case, if invalid values present in file_type, status, transcode_status,  file compression
    if(fileinfo.file_type < CIRCULAR_BUFFER_TYPE_CREATING || fileinfo.file_type > CIRCULAR_BUFFER_TYPE_TEXT_ALERTS){
        string err_msg = "Invalid file type present in file info -> " + to_string(fileinfo.file_type);
        send_critical_info(SM_E_CB_INVALID_FILE_TYPE_PRESENT_IN_FILEINFO,fileinfo.file_type,err_msg);
        if(extn == ".mp4" || extn == ".mkv"){
            fileinfo.file_type = CIRCULAR_BUFFER_TYPE_NORMAL;
        }
        else if(extn == ".txt" || extn == ".json" || extn == ".zip" || extn == ".aac" ){
            fileinfo.file_type = CIRCULAR_BUFFER_TYPE_TEXT;
        }
        LOG_I(TAG,"File type changed to -> %d",fileinfo.file_type);
    }
    if((fileinfo.tc_status < CIRCULAR_BUFFER_TC_STATUS_WAITING) || (fileinfo.tc_status > CIRCULAR_BUFFER_TC_STATUS_MIGRATING)){
        string err_msg = "Invalid tc_status present in file info " + to_string(fileinfo.tc_status);
        send_critical_info(SM_E_CB_INVALID_TC_STATUS_PRESENT_IN_FILEINFO,fileinfo.tc_status,err_msg);
    }
    bool ret = validate_upl_and_rec_vid_enabled(fileinfo, deleteFile);
    if(ret == false) {
		LOG_E(TAG,"Check why validate_upl_and_rec_vid_enabled() returned Fail");
    }
    return true;
}
/*
 * add_file_DB adds/updates an entry to the database.
 * uses filename as the unique identifier
 * first checks if any entry with filename is already present
 *   if filename is already present in db, updates the file size
 *      retruns true if sql query is successfull else false
 *   if filename is not present in db, inserts a new entry
 *      returns true if sql query to insert is successfull, else false
 *   if more than one entry is present for a filename, returns false
 */
bool add_file_DB(circular_buffer_fileinfo_t fileinfo, int batch_mode, circular_buffer_filestatus_t status, circular_buffer_file_compression_t fc_type)
{
    bool rc = false;

    string val_string;
    std::stringstream val_stream;
    // check for error
    bool deleteFile = false;
    bool fileinfo_valid = validate_fileinfo(fileinfo, deleteFile); // if validate_fileinfo returns false, is there a need to take any action or file should be added to DB manadatorily
    if(deleteFile == true ){  // Don't add in DB. File deleted because of unknown file type.
        return false;
    }
    // Call function which will handle batch data
    if(BATCH_MODE_ADD == batch_mode){
        return insert_batch_file_DB(fileinfo, batch_mode, status, fc_type);
    }

    val_stream <<
            "SELECT FILE_SIZE, STATUS, UPL_VID_ENABLED, REC_VID_ENABLED from VIDFILES WHERE NAME == "
            << "'" << fileinfo.base_file_name << "';";
    val_string = val_stream.str();
    int64_t count[5] = {0, 0, 0, 0, 0};

    rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string, 
                                callback_count_size_status, (void*)&count[RES_ENTRY_COUNT_IND]);
    if(false == rc){
        // This will result in duplicate entries, from which oldest one will be deleted
        string err_msg = "Select query failed to execute. Inserting the file regardless to avoid data loss.";
        LOG_C(TAG,err_msg.c_str());
        nd_service_obj->send_err_msg(SM_E_CB_QUERY_EXECUTION_FAILED,count[RES_ENTRY_COUNT_IND],err_msg);
        // Removing duplicate file logic
        #if 0
        if(false == insert_file_DB(fileinfo)){
            LOG_E(TAG,"Insert file DB failed for file %s",fileinfo.base_file_name);
            return false;
        }
        if(false == delete_oldest_entry_with_filename(fileinfo.base_file_name)){
            LOG_E(TAG,"Deletion for duplicate entires in DB failed for file %s",fileinfo.base_file_name);
        }
        return true;
        #endif
        return false;
    }
    if (count[RES_ENTRY_COUNT_IND] == 0){
        LOG_I(TAG, "Found no entry with NAME %s. Inserting new entry.", fileinfo.base_file_name );
        return insert_file_DB(fileinfo);
    }
    else if( count[RES_ENTRY_COUNT_IND] >= MAX_NO_OF_DUPLICATE_ENTRIES_DB )
    {
        std::ostringstream oss;
        oss << "Found more than one entries with NAME = " << fileinfo.base_file_name << " ,count = " << count[RES_ENTRY_COUNT_IND] << " in DB.";
        // LOG_E(TAG, "Found more than one entries with NAME = %s ,count = %lld in DB",fileinfo.base_file_name, count[0]);
        LOG_C(TAG,oss.str().c_str());
        nd_service_obj->send_err_msg(SM_E_CB_MULTIPLE_FILES_WITH_SAME_NAME_IN_DB,count[RES_ENTRY_COUNT_IND],oss.str());
        return false;
    }

    LOG_I(TAG,"Number of files already present with same name %d",count[RES_ENTRY_COUNT_IND]);
    // Removing duplicate file logic
    #if 0
    LOG_I(TAG,"Deleting oldest from duplicate files, if duplicates are present");
    bool del_succ = delete_oldest_entry_with_filename(fileinfo.base_file_name);
    if(false == del_succ){
        LOG_E(TAG,"Deletion of oldest file in case of duplicates failed");
    }
    #endif
    // if file size in db matches file size in input data and if status of entry in
    // db is not CIRCULAR_BUFFER_STATUS_DELETE, then don't update the file size
    circular_buffer_filestatus_t file_status_in_db = (circular_buffer_filestatus_t) count[RES_STATUS_IND];
    
    //this was an issue identified during testing, wrong indexes were used
    if (count[RES_FILE_SIZE_IND] == fileinfo.file_size && count[RES_UPL_VID_ENABLED_IND] == fileinfo.upl_vid_enabled && count[RES_REC_VID_ENABLED_IND] == fileinfo.rec_vid_enabled && file_status_in_db != CIRCULAR_BUFFER_STATUS_DELETE){
        LOG_I(TAG, "%s : filesize matches (%ld) and fileinfo.upl_vid_enabled matches (%d). Update not required.",
            fileinfo.base_file_name, fileinfo.file_size, fileinfo.upl_vid_enabled);
        return true; //file size matches and upl_vid_enabled matches
    }
    // update required
    // check for limit (number and memory)
    // make space if needed and then only push
    bool ret;
    ret = add_file_normal(CIRC_BUFF_ctx->db_handle, fileinfo.base_file_name,  fileinfo.file_size);
    if(ret == false)
    {
        LOG_E(TAG,"Something went wrong in add_file_normal");
        file_delete(nd_device_obj->get_external_eMMC_mount_path() + "/" + fileinfo.base_file_name);
        return false;
    }
    LOG_I(TAG,"Updating the existing row as filename is present in DB.");
    if (count[RES_FILE_SIZE_IND] != fileinfo.file_size){
        LOG_I(TAG, "%s : updating db file size from %lld to %d", fileinfo.base_file_name, count[RES_FILE_SIZE_IND], fileinfo.file_size);
    }
    if ((fileinfo.upl_vid_enabled != UPL_VID_ENABLED_DO_NOT_UPDATE) && (count[RES_UPL_VID_ENABLED_IND] != fileinfo.upl_vid_enabled)){
        LOG_I(TAG, "%s : updating db upl_vid_enabled from %lld to %d", fileinfo.base_file_name, count[RES_UPL_VID_ENABLED_IND], fileinfo.upl_vid_enabled);
    }
    // CHECK FOR REC_VID_ENABLED cannot be changed from 0 to 1
    if ((fileinfo.rec_vid_enabled != REC_VID_ENABLED_DO_NOT_UPDATE) && (count[RES_REC_VID_ENABLED_IND] != fileinfo.rec_vid_enabled)){
        LOG_I(TAG, "%s : updating db rec_vid_enabled from %lld to %d", fileinfo.base_file_name, count[RES_REC_VID_ENABLED_IND], fileinfo.rec_vid_enabled);
    }

    if (file_status_in_db == CIRCULAR_BUFFER_STATUS_DELETE){
        LOG_I(TAG, "%s : updating db status from DELETE(%d) to NEW(%d)",
            fileinfo.base_file_name, file_status_in_db, CIRCULAR_BUFFER_STATUS_NEW);
        file_status_in_db = CIRCULAR_BUFFER_STATUS_NEW;
    }

    val_stream.str("");
    val_stream.clear();
    val_stream << "UPDATE VIDFILES SET " 
                << "FILE_SIZE = " << fileinfo.file_size << ", "
                << "TYPE = " << fileinfo.file_type << ", ";
    if(fileinfo.upl_vid_enabled != UPL_VID_ENABLED_DO_NOT_UPDATE)
        val_stream << "UPL_VID_ENABLED = " << fileinfo.upl_vid_enabled << ", ";
    if(fileinfo.rec_vid_enabled != REC_VID_ENABLED_DO_NOT_UPDATE)
        val_stream << "REC_VID_ENABLED = " << fileinfo.rec_vid_enabled << ", ";
    val_stream << "STATUS = " << file_status_in_db << " "
                << " WHERE NAME == "<< "'" <<fileinfo.base_file_name << "';" ;

    val_string = val_stream.str();

    rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string, callback, 0);
    write_updatefile_metadata_to_xattrs(fileinfo);
    if(rc == false){
        LOG_E(TAG, "Query to update db entry with %s failed.",fileinfo.base_file_name);
        return false;
    }else{
        LOG_I(TAG,"Update query executed successfully.");
    }
    return true;
}

int get_previous_DMS_file_index(int64_t sessionCnt){

    int rc;
    const char *sql;
    string val_string;
    std::stringstream val_stream;
    int latest_DMS_index ;
    // get the maximum index of DMS video file
    val_stream.str("");
    val_stream.clear();
    val_stream <<
            "SELECT max(INDEXID) from VIDFILES WHERE NAME LIKE \"8_trip%y.mp4\" AND SESSIONCOUNT < " << sessionCnt  ;
            //"SELECT max(INDEXID) FROM VIDFILES WHERE INDEXID < ( SELECT max(INDEXID) from VIDFILES WHERE NAME LIKE \"8_trip%\" ) " ;
            //"SELECT max(INDEXID) from VIDFILES WHERE NAME LIKE \"8_trip%\" " ;
    val_string = val_stream.str();
    sql = val_string.c_str();
    rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, sql, callback_indx,
                        (void*)&(latest_DMS_index));
    LOG_I(TAG, "latest_DMS_index %d", latest_DMS_index);
    return latest_DMS_index  ;
}

/* 
 * find_add_del_files functions returns the newest (max NUM_CLOUD_NOTIFY_ENTRIES) files added to the database and the
 * newest (max NUM_CLOUD_NOTIFY_ENTRIES) files deleted from the database
 *
 */

bool find_add_del_files(pair < vector< json_add_del >, vector< json_add_del > > &add_del_files, int &oldest_video_index,
                                   string &db_identifier, int64_t &db_creation_time)
{
    int rc;
    const char *sql;

    string val_string;
    std::stringstream val_stream;
    
    val_stream <<
            "SELECT INDEXID, NAME, DURATION, TYPE , UDID, SESSIONCOUNT, FILE_COMPRESSION from VIDFILES WHERE STATUS == "
            << CIRCULAR_BUFFER_STATUS_NEW << " AND TYPE >= " << CIRCULAR_BUFFER_TYPE_START 
            << " AND TYPE <= " << CIRCULAR_BUFFER_TYPE_STOP << " ORDER BY INDEXID DESC LIMIT "
            << NUM_CLOUD_NOTIFY_ENTRIES;
    val_string = val_stream.str();
    sql = val_string.c_str();
    rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, sql, callback_add_del,
                        (void*)&(add_del_files.first));
    if( rc == false ){
      return false;
    }
    val_stream.str("");
    val_stream.clear();
    val_stream <<
            "SELECT INDEXID, NAME, DURATION, TYPE, UDID, SESSIONCOUNT, FILE_COMPRESSION from VIDFILES WHERE STATUS == "
            << CIRCULAR_BUFFER_STATUS_DELETE << " AND TYPE >= " << CIRCULAR_BUFFER_TYPE_START 
            << " AND TYPE <= " << CIRCULAR_BUFFER_TYPE_STOP << " ORDER BY INDEXID DESC LIMIT "
            << NUM_CLOUD_NOTIFY_ENTRIES;
    val_string = val_stream.str();
    sql = val_string.c_str();
    rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, sql, callback_add_del, 
                        (void*)&(add_del_files.second));
    LOG_D(TAG, "add_del_files.second.size() %d", add_del_files.second.size());
    if( rc == false ){
      return false;
    }
    LOG_I(TAG, "add files stack size = %d, delete stack size = %d",
                add_del_files.first.size(),
                add_del_files.second.size());
    // get the minimum index having video file with status not set to delete yet
    val_stream.str("");
    val_stream.clear();
    val_stream <<
            "SELECT min(INDEXID) from VIDFILES WHERE STATUS != "
            << CIRCULAR_BUFFER_STATUS_DELETE << " AND TYPE >= " << CIRCULAR_BUFFER_TYPE_START
            << " AND TYPE <= " << CIRCULAR_BUFFER_TYPE_STOP << " AND ( FILE_COMPRESSION != " << CIRCULAR_BUFFER_NO_COMPRESSION << " ) ";
    val_string = val_stream.str();
    sql = val_string.c_str();
    rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, sql, callback_indx,
                        (void*)&(oldest_video_index));
    LOG_I(TAG, "oldest_video_index %d", oldest_video_index);
    if( rc == false ){
        return false;
    }

    val_stream.str("");
    val_stream.clear();
    val_stream <<
            "SELECT DB_IDENTIFIER, DB_CREATIONTIME from DB_DETAILS";
    val_string = val_stream.str();
    sql = val_string.c_str();

    pair <string, int64_t> db_details;
    rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, sql, callback_db_details,
                        (void*)&(db_details));
    db_identifier = db_details.first;
    db_creation_time = db_details.second;
    LOG_I(TAG, "db_identifier: %s", db_identifier.c_str());
    LOG_I(TAG, "db_creation_time: %lld", db_creation_time);

    return true;
}

bool update_add_del_files_db(pair < vector< json_add_del >, vector< json_add_del > > add_del_files)
{
    bool rc;
    
    int i = 0;
    int add_file_count = add_del_files.first.size();
    int del_file_count = add_del_files.second.size();
    string val_string;
    std::stringstream val_stream;
    for( i=0;i<add_file_count;i++) {
        val_stream.str("");
        val_stream.clear();
        LOG_D(TAG, "File Name from add list : %s", add_del_files.first[i].file_name.c_str());
        val_stream << "UPDATE VIDFILES SET " 
            << "STATUS = " << CIRCULAR_BUFFER_STATUS_NOTIFIED
            << " WHERE INDEXID == " << add_del_files.first[i].index_id ;
        val_string = val_stream.str();
        
        rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string, NULL, (void*)NULL);
        update_status_xattr(i, add_del_files);
        if(rc == false) {
            LOG_E(TAG, "failed to execute in update_add_del_files_db 1");
            return false;
        }
    }
    for( i=0;i<del_file_count;i++) {
        val_stream.str("");
        val_stream.clear();
        LOG_D(TAG, "File Name from delete list : %s", add_del_files.second[i].file_name.c_str());
        val_stream << "DELETE FROM VIDFILES "
            << " WHERE INDEXID == " << add_del_files.second[i].index_id ;
        val_string = val_stream.str();

        rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string, NULL, (void*)NULL);
        if(rc == false){
            LOG_E(TAG, "failed to execute in update_add_del_files_db 2");
            return false;
        }
    }
    return true;
};

bool get_db_details(vector<file_data_str_db_t> & allfiles_db)
{
    int rc;
    const char *sql;

    string val_string;
    std::stringstream val_stream;
    // The check for rec_vid_enabled was added in x.6.12 to avoid fetching the files which are in privacy mode, as files were not present in directory which can lead one extra check for each file
    // As part of x.6.14 dummy files are created for privacy mode also, so fetching all files except deleted ones
    // Added full metadata fields required by file_data_str_db_t.
    val_stream <<
            "SELECT INDEXID, NAME, FILE_SIZE, TYPE, SESSIONCOUNT, UDID, TIME, REC_VID_ENABLED, UPL_VID_ENABLED, CAM_TYPE, TRANSCODE_STATUS, STATUS, FILE_COMPRESSION from VIDFILES WHERE STATUS != "
                << CIRCULAR_BUFFER_STATUS_DELETE << " ;";
    val_string = val_stream.str();
    sql = val_string.c_str();
    rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, sql, callback_allfiles_db,
                        (void*)&(allfiles_db));

    return rc;
}

bool delete_entry_from_db(int index_id, int file_type)
{
    string val_string;
    std::stringstream val_stream;
    bool rc;

    if( (file_type == CIRCULAR_BUFFER_TYPE_TEXT) || (file_type == CIRCULAR_BUFFER_TYPE_TEXT_ALERTS) ) {
        val_stream.str("");
        val_stream << "DELETE FROM VIDFILES "
            << " WHERE INDEXID == " << index_id ;
        val_string = val_stream.str();
        LOG_D(TAG, "SQL %s", val_string.c_str());
        
        rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string, NULL, (void*)NULL);
        if(rc == false){
            LOG_E(TAG, "failed to execute in update_add_del_files_db 2");
            return false;
        }
    }
    else {
        val_stream.str("");
        val_stream.clear();
        val_stream << "UPDATE VIDFILES SET " 
            << "STATUS = " << CIRCULAR_BUFFER_STATUS_DELETE
            << " WHERE INDEXID == " << index_id ;
        val_string = val_stream.str();
        LOG_D(TAG, "SQL %s", val_string.c_str());
        
        rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string, NULL, (void*)NULL);
        if(rc == false){
            LOG_E(TAG, "failed to update VIDFILES with STATUS = %d for INDEXID = %d", CIRCULAR_BUFFER_STATUS_DELETE, index_id);
            return false;
        }
    }
    return true;
}

bool delete_entry_from_db(string filename, int file_type)
{
    string val_string;
    std::stringstream val_stream;
    bool rc;

    if( (file_type == CIRCULAR_BUFFER_TYPE_TEXT) || (file_type == CIRCULAR_BUFFER_TYPE_TEXT_ALERTS) ) {
        val_stream.str("");
        val_stream << "DELETE FROM VIDFILES "
            << " WHERE NAME == '" << filename << "'" ;;
        val_string = val_stream.str();
        LOG_D(TAG, "SQL %s", val_string.c_str());
        
        rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string, NULL, (void*)NULL);
        if(rc == false){
            LOG_E(TAG, "failed to execute in update_add_del_files_db 2");
            return false;
        }
    }
    else {
        val_stream.str("");
        val_stream.clear();
        val_stream << "UPDATE VIDFILES SET " 
            << "STATUS = " << CIRCULAR_BUFFER_STATUS_DELETE
            << " WHERE NAME == '" << filename << "'" ;;
        val_string = val_stream.str();
        LOG_D(TAG, "SQL %s", val_string.c_str());
        
        rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string, NULL, (void*)NULL);
        if(rc == false){
            LOG_E(TAG, "failed to update VIDFILES with STATUS = %d for NAME = %s", CIRCULAR_BUFFER_STATUS_DELETE, filename.c_str());
            return false;
        }
    }
    return true;
}


/*
 * Add a new entry in the db
 */
bool insert_file_DB(circular_buffer_fileinfo_t fileinfo)
{
    int rc;

    string val_string;
    std::stringstream val_stream;
    // If store_lq_dms_files is true for CAM_8 or for all other cameras, STATUS should be set to NEW always and it will be considered in cloud notify add list.
    bool is_cloud_notify = true;
    if((fileinfo.camtype == CIRCULAR_BUFFER_CAM_DMS) && (store_lq_dms_file == false)){
        // Check if file is DMS and store_lq_dms_files is false, then STATUS should be set to Notified. So that cloud notify doesn't pick it up.
        is_cloud_notify = false;
        // In case of alert STATUS will be updated to NEW by the file compression update, then it will consumed by cloud notify and reported as an alert.
    }

    bool ret;
    ret = add_file_normal(CIRC_BUFF_ctx->db_handle, fileinfo.base_file_name,  fileinfo.file_size);
    if(ret == false)
    {   // Notify health mon
        string str_msg = "Something went wrong in add_file_normal";
        LOG_E(TAG, str_msg.c_str() );
        nd_service_obj->send_err_msg(SM_E_CB_MAKE_SPACE_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
        file_delete(nd_device_obj->get_external_eMMC_mount_path() + "/" + fileinfo.base_file_name);
        return false;
    }
    val_stream  << insert_str
                << "(" << fileinfo.time << ", "
                << "'" << fileinfo.base_file_name << "'" << ", "
                << fileinfo.file_size << ", "
                << fileinfo.file_type << ", "
                << ((is_cloud_notify == false) ? CIRCULAR_BUFFER_STATUS_NOTIFIED : CIRCULAR_BUFFER_STATUS_NEW) << ", "
                << fileinfo.camtype << ", "
                << fileinfo.duration << ", "
                << fileinfo.udid << ", "
                << fileinfo.sessionCount << ", "
                << fileinfo.tc_status << ", "
                << fileinfo.upl_vid_enabled << ", "
                << fileinfo.rec_vid_enabled << " );" ;

    val_string = val_stream.str();
    LOG_I(TAG, "udid: %lld, sessionCount: %lld", fileinfo.udid,  fileinfo.sessionCount );

    rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string, NULL, 0);
    if(CIRC_BUFF_ctx->extended_attr_enabled){
        write_addfile_metadata_to_xattrs(fileinfo);
    }
    if(rc == false){
        // Notify health mon
        string str_msg = "Insertion in CB DB failed.";
        LOG_E(TAG, str_msg.c_str() );
        nd_service_obj->send_err_msg(SM_E_CB_QUERY_EXECUTION_FAILED, NDService::UNUSED_ERR_AUX_CODE, str_msg );
        return false;
    }
    return true;
}

int delete_clock_hours_drp_minified_obs(string INT_COMPLETE_OBS_PATH, int64_t udid, int64_t sessionCount){

    int cnt = 0 ;   // delete in a batch of 10 files
    while(cnt++ < MAX_FILE_DELETE_DRP) { 
        string response  = "";
        string command = "ls " + INT_COMPLETE_OBS_PATH + " | wc -l";
        system_execute_with_resp(TAG, command, response);
        int totalminifiedObsFile;
        int const maxminifiedObsFile = drp_clock_hours*60 ;

        string_to_integer(response , totalminifiedObsFile);
        LOG_I(TAG, "cmd: %s , totalminifiedObsFile: %d ", command.c_str(),  totalminifiedObsFile);
        if(totalminifiedObsFile < 1) 
            return cnt ;
        response  = "";
        if(cnt%2 == 0) // Find the oldest file
            command = "echo $( ls " + INT_COMPLETE_OBS_PATH + "|head -n 1)"; 
        else      // find the latest maxminifiedObsFile and delete the oldest file from this.
                  // Both are same normally. But if genprop db corrupts, it will make sure no files are piled up.
            command = "echo $( ls " + INT_COMPLETE_OBS_PATH + "| tail -n " + to_string(maxminifiedObsFile) + "|head -n 1)";
        system_execute_with_resp(TAG, command, response);
        LOG_D(TAG, "cmd: %s response: %s", command.c_str(),response.c_str());
        string tmpFile = response.substr(0, response.find("_y_") ) + "_y.mp4" ; 
        int64_t tmp_sessionCount = sessionCount_from_file(tmpFile) ;
        if(tmp_sessionCount < sessionCount ) {
            LOG_D(TAG, "Deleted SC: %lld",  sessionCount_from_file(tmpFile) );
            string oldest_minified_obs_file = INT_COMPLETE_OBS_PATH + "/" + response;
            command = "rm " + oldest_minified_obs_file ;
            system_execute_with_resp(TAG, command, response);
            LOG_I(TAG, "Deleted oldest_minified_obs_file: %s response: %s", oldest_minified_obs_file.c_str(),response.c_str());
        }
        else {
            LOG_I(TAG, "exiting delete_clock_hours_drp_minified_obs(), tmp_sessionCount: %lld, sessionCount: %lld ", tmp_sessionCount , sessionCount );
            return cnt ;
        }
    }
    return cnt ;
}

/*
    delete_clock_hours_drp_sign_crops has 2 functions 
    1. Delete backlog sign crop files that have failed to get deleted in the past
    2. Delete the sign crop files of current cycle based on received session count
*/
void delete_clock_hours_drp_sign_crops(int64_t expiredSessionCount){
    int backlogSignCropFilesSize = backlogSignCropFiles.size();
    const string SIGN_CROP_OUTPUT_PATH = nd_device_obj->get_sign_crop_base_path();  

    int deletedBacklogFilesCnt = 0;
    if (!backlogSignCropFiles.empty()) {
        auto it = backlogSignCropFiles.begin();
        while (it != backlogSignCropFiles.end()) {
            const string& expiredFilePath = SIGN_CROP_OUTPUT_PATH + it->file_name;
            if (file_delete(expiredFilePath)) {
                LOG_I(TAG, "Deleted backlog expired sign crop file: %s", expiredFilePath.c_str());
                it = backlogSignCropFiles.erase(it); // Erase and move to the next element
                deletedBacklogFilesCnt++;
            } else {
                LOG_E(TAG, "Failed to delete backlog expired sign crop file: %s", expiredFilePath.c_str());
                ++it; // Move to the next element
            }
        }
    }

    int totalSignCropFiles = signCropFiles.size();
    int batchCnt = 0 ;   // delete in a batch of 20 files
    int deletedFilesCnt = 0;
    while(batchCnt++ < MAX_SIGN_CROP_FILE_DELETE_DRP && sign_crop_file_idx < totalSignCropFiles) {  
        int64_t curIdxSessionCount = signCropFiles[sign_crop_file_idx].session_count;
        if(curIdxSessionCount <= expiredSessionCount ) {
            LOG_I(TAG, "Deleting SC: %lld",  curIdxSessionCount);
            string expired_file_path = SIGN_CROP_OUTPUT_PATH + signCropFiles[sign_crop_file_idx].file_name; 
            if (file_delete(expired_file_path)) {
                LOG_I(TAG, "Deleted expired sign crop file: %s", expired_file_path.c_str());
                deletedFilesCnt++;
            }
            else {
                backlogSignCropFiles.push_back(signCropFiles[sign_crop_file_idx]);
                LOG_E(TAG, "Failed to delete expired sign crop file: %s", expired_file_path.c_str());
            }
        }else{
            LOG_I(TAG, "exiting delete_clock_hours_drp_sign_crops(), curIdxSessionCount: %lld, expiredSessionCount: %lld ", curIdxSessionCount , expiredSessionCount );
            break;
        }
        sign_crop_file_idx++;
    }
    LOG_I(TAG, "Total file deleted inside delete_clock_hours_drp_sign_crops() deletedFilesCnt: %d deletedBacklogFilesCnt: %d", deletedFilesCnt, deletedBacklogFilesCnt);
}

int delete_clock_hours_drp(){
    int64_t current_time_local = 0;

    pthread_mutex_lock(&CIRC_BUFF_ctx->current_time_mtx);
    current_time_local = CIRC_BUFF_ctx->current_time;
    pthread_mutex_unlock(&CIRC_BUFF_ctx->current_time_mtx);

    if(current_time_local <= 0) {    // current_time is not valid when it is 0, it will get updated by ndcentral.
        LOG_E(TAG, "exiting delete_clock_hours_drp()  current time < 0 "  );
        return 0;
    }
    int rc;
    circular_buffer_file_t fileinfo;
    string val_string;
    std::stringstream val_stream;
    int deletedFileCnt1 = 0;   // file with correct timestamp 
    int deletedFileCnt2 = 0;   // file with incorrect timestamp
///////////////////////// Delete video file //////////////////////////////
    while(1) {
        int64_t udid = 0;
        int64_t sessionCount = 0;
        fileinfo.base_file_name[0] = '\0' ;
        val_stream.str("");
        val_stream.clear();
        static int64_t drp_clock_milliseconds = convert_time_unit(nd_time_unit::time_in_hours,
                 nd_time_unit::time_in_milliseconds, drp_clock_hours) ;
        LOG_I(TAG, "drp_clock_hours: %lld drp_clock_milliseconds : %lld ", drp_clock_hours, drp_clock_milliseconds );
        if(drp_clock_milliseconds <= 0 ) { // -ve case in case it is overflowed.
            LOG_E(TAG, "drp_clock_hours: %lld drp_clock_milliseconds : %lld ", drp_clock_hours, drp_clock_milliseconds );
            nd_service_obj->send_err_msg(SM_E_CB_INVALID_TIME_RESULT, drp_clock_milliseconds, "Invalid time drp_clock_milliseconds");
            drp_clock_milliseconds = drp_clock_hours*hours_to_milliseconds; 
        }
        int64_t drp_expiry_time = current_time_local - drp_clock_milliseconds ;
        val_stream <<
		       "SELECT * FROM VIDFILES WHERE TIME < " << drp_expiry_time << " AND TIME > " << 0
                        << " AND STATUS != " << CIRCULAR_BUFFER_STATUS_DELETE  << " ORDER BY INDEXID ASC LIMIT 1" ;

        val_string = val_stream.str();
        LOG_I(TAG, "delete_clock_hours_drp() cmd: %s ", val_string.c_str() );
        rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string,
                         callback_complete_fileinfo, (void*)&fileinfo);
        if (rc == false){
            LOG_E(TAG, "Failed to execute %s", val_string.c_str());
            return deletedFileCnt1 + deletedFileCnt2;
        }
        if (fileinfo.base_file_name[0] == '\0'){
            LOG_I(TAG, "delete_clock_hours_drp() No entry in DB found ");
            break;
        }
        else {
            udid = fileinfo.udid;
            sessionCount = fileinfo.sessionCount;

            val_stream.str("");
            val_stream.clear();
            delete_entry_from_db(fileinfo.index_id, fileinfo.file_type);
            string file_full_path = nd_device_obj->get_external_eMMC_mount_path() + fileinfo.base_file_name ;
            bool del_status = delete_file_from_sdcard(file_full_path);
            deletedFileCnt1++ ;
            if(del_status == false){
                LOG_I(TAG, "delete_clock_hours_drp() couldn't delete file %s", file_full_path.c_str() );
            }
            else LOG_D(TAG, "delete_clock_hours_drp() deleted %s, index: %d", file_full_path.c_str() , fileinfo.index_id);
            if(0) {  //if (fileinfo.base_file_name[0] == outward_file_prefix) {
            // delete detailed obs file or zip file
                std::string::size_type pos = file_full_path.find("_y.");
                string zipFilePath = file_full_path.substr(0, pos) + "_y.zip";
                string audioFilePath = file_full_path.substr(0, pos) + "_y.aac";
                string alertZipFilePath = file_full_path.substr(0, pos) + "_y_summary.json.zip";
                LOG_D(TAG, "deleting zip file %s", zipFilePath.c_str() );
                del_status = delete_file_from_sdcard(zipFilePath);
            // delete zip file from db
                val_stream.str("");
                val_stream.clear();
                val_stream << "DELETE FROM VIDFILES "
                    << " WHERE NAME == '" <<  zipFilePath.substr(zipFilePath.find("0_trip") ) << "'" ; // get filename from full path filename
                val_string = val_stream.str();
                LOG_I(TAG, "zip file SQL %s", val_string.c_str());

                rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string, NULL, (void*)NULL);
                if(rc == false){
                    LOG_E(TAG, "failed to execute in update_add_del_files_db 2");
                    return false;
                }
                if(file_is_present(alertZipFilePath)){
                // delete alert obs file summary.json.zip
                    LOG_I(TAG, "deleting alert zip file %s", alertZipFilePath.c_str() );
                    del_status = delete_file_from_sdcard(alertZipFilePath);
                // delete summary.json.zip file from db
                    val_stream.str("");
                    val_stream.clear();
                    val_stream << "DELETE FROM VIDFILES "
                        << " WHERE NAME == '" <<  alertZipFilePath.substr(alertZipFilePath.find("0_trip") ) << "'" ; // get filename from full path filename
                    val_string = val_stream.str();
                    LOG_I(TAG, "zip file SQL %s", val_string.c_str());

                    rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string, NULL, (void*)NULL);
                    if(rc == false){
                        LOG_E(TAG, "failed to execute in update_add_del_files_db 2");
                        return false;
                    }
                }
                if(file_is_present(audioFilePath)){
                // delete audio file
                    LOG_I(TAG, "deleting audio  file %s", audioFilePath.c_str() );
                    del_status = delete_file_from_sdcard(audioFilePath);
                // delete audio file from db
                    val_stream.str("");
                    val_stream.clear();
                    val_stream << "DELETE FROM VIDFILES "
                        << " WHERE NAME == '" <<  audioFilePath.substr(audioFilePath.find("0_trip") ) << "'" ; // get filename from full path filename
                    val_string = val_stream.str();
                    LOG_I(TAG, "audi file SQL %s", val_string.c_str());

                    rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string, NULL, (void*)NULL);
                    if(rc == false){
                        LOG_E(TAG, "failed to execute in update_add_del_files_db 2");
                        return false;
                    }
                }

            }
            // delete corresponding minified obs file if available.
            string minifiedObsFile = INT_COMPLETE_OBS_PATH + fileinfo.base_file_name;
            std::string::size_type pos = minifiedObsFile.find("_y.mp4"); 
            minifiedObsFile = minifiedObsFile.substr(0, pos);

            string response  = "";
            string command = "ls " + minifiedObsFile + "*";
            system_execute_with_resp(TAG, command, response);
            command = "rm " + response ;
	        response  = "";
            system_execute_with_resp(TAG, command, response);
            LOG_I(TAG, "Deleted oldest_minified_obs_file: %s ", command.c_str() );

            //    Delete all files older than fileinfo.sessionCount
            int lastSessionWithTimeStamp = fileinfo.sessionCount ;
            while(1){
                fileinfo.base_file_name[0] = '\0' ;
                val_stream.str("");
                val_stream.clear();
                val_stream <<
                     "SELECT * FROM VIDFILES WHERE "
                        << "SESSIONCOUNT < " <<  lastSessionWithTimeStamp
                        << " AND TIME < "  << drp_expiry_time
                        << " AND STATUS != " << CIRCULAR_BUFFER_STATUS_DELETE
                        << " ORDER BY INDEXID ASC LIMIT 1"  ;
                val_string = val_stream.str();
                LOG_D(TAG, "delete_clock_hours_drp() 2 cmd: %s ", val_string.c_str() );
                rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string,
                         callback_complete_fileinfo, (void*)&fileinfo);
                LOG_I(TAG, " file(old and wrong timestamp): %s, index: %d, time: %lld ", fileinfo.base_file_name , fileinfo.index_id, fileinfo.time);
                if (rc == false){
                    LOG_E(TAG, "Failed to execute %s", val_string.c_str());
                    break ;
                }
                if (fileinfo.base_file_name[0] == '\0'){
                    LOG_I(TAG, "delete_clock_hours_drp() 2 No file older than session %d ", lastSessionWithTimeStamp);
                    break;
                }
                else {
                    val_stream.str("");
                    val_stream.clear();
                    // Previous OTA zip file has sessionCount -1 and getting deleted. Correct it from file name.
                    if(fileinfo.sessionCount < 0) {
                        int64_t sessionCnt =  sessionCount_from_file(fileinfo.base_file_name);
                        val_stream << "UPDATE VIDFILES SET "
                            << "SESSIONCOUNT = " << sessionCnt
                            << " WHERE INDEXID == " << fileinfo.index_id ;
                        val_string = val_stream.str();
                        LOG_I(TAG, "SQL %s", val_string.c_str());
                        rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string, NULL, (void*)NULL);
                        if(rc == false){
                            LOG_E(TAG, "failed to update VIDFILES, sessionCnt: %d for INDEXID = %d", sessionCnt, fileinfo.index_id);
                            break;
                        }

                        break;
                    }
                    delete_entry_from_db(fileinfo.index_id, fileinfo.file_type);
                    string file_full_path = nd_device_obj->get_external_eMMC_mount_path() + fileinfo.base_file_name ;
                    bool del_status = delete_file_from_sdcard(file_full_path);
                    if(del_status == false){
                        LOG_I(TAG, "delete_clock_hours_drp() 2 couldn't delete file %s", file_full_path.c_str() );
                    }
                     else LOG_D(TAG, "delete_clock_hours_drp() 2 deleted %s, index: %d, time: %lld ", fileinfo.base_file_name , fileinfo.index_id, fileinfo.time);

                    if(strstr(fileinfo.base_file_name, "y.mp4") != NULL ) deletedFileCnt2++ ;
                    fileinfo.base_file_name[0] = '\0' ;
                }
                if(deletedFileCnt2 > MAX_FILE_DELETE_DRP ) break; // delete older files with wrong time in a batch of 10 files.
            }

        }
        // Delete minified observation file
            LOG_I(TAG, "Delete minified observation file: %lld , %lld ", udid, sessionCount );
        delete_clock_hours_drp_minified_obs(INT_COMPLETE_OBS_PATH, udid, sessionCount );
        delete_clock_hours_drp_sign_crops(sessionCount);
     }
    LOG_I(TAG, "total file deleted inside delete_clock_hours_drp() deletedFileCnt1: %d, deletedFileCnt2:  %d", deletedFileCnt1 ,  deletedFileCnt2 );

    return deletedFileCnt1 + deletedFileCnt2 ;
}
int delete_n_old_session_drp_minified_obs(string minified_obs_path){
    int cnt = 0 ;
    while(cnt++ < MAX_FILE_DELETE_DRP) {
        string response  = "";
        string command = "ls " + minified_obs_path + " | wc -l";
        system_execute_with_resp(TAG, command, response);
        int totalminObsFile;
        string_to_integer(response , totalminObsFile);
        LOG_I(TAG, "cmd: %s , totalminObsFile: %d ", command.c_str(),  totalminObsFile);
        if(totalminObsFile > drp_clock_hours * 60){   // 60 7z file is expected every hour
            response  = "";
            command = "echo $( ls " + minified_obs_path + "|head -n 1)";
            system_execute_with_resp(TAG, command, response);
            LOG_I(TAG, "cmd: %s response: %s", command.c_str(),response.c_str());
            string oldest_minified_obs_file = minified_obs_path + "/" + response;
            command = "rm " + oldest_minified_obs_file ;
            system_execute_with_resp(TAG, command, response);
            LOG_I(TAG, "Deleted oldest_minified_obs_file: %s ", oldest_minified_obs_file.c_str() );
        }
        else break ;
    }
    return 0 ;

}

void initialize_sign_crop_files(vector<circular_buffer_drp_t> &signCropFiles, const string& SIGN_CROP_OUTPUT_PATH){
    signCropFiles.clear();
    backlogSignCropFiles.clear();

    if(!get_files_by_session_count(SIGN_CROP_OUTPUT_PATH, signCropFiles)){ //sorts the files in ascending order of session count
        LOG_E(TAG, "get_files_by_session_count function failed");
    }else{
        LOG_I(TAG, "Populated signCropFiles cache with %d files", signCropFiles.size());
    }

    // Resize the vector to keep only the first MAX_SIGN_CROP_FILES_IN_CACHE entries
    if(signCropFiles.size() > MAX_SIGN_CROP_FILES_IN_CACHE){
        signCropFiles.resize(MAX_SIGN_CROP_FILES_IN_CACHE);
    }

    sign_crop_file_idx = 0;
}

/*
    delete_n_old_session_drp_sign_crops has 2 functions 
    1. Re-populate signCropFiles cache(vector) every "SIGN_CROP_DRP_CYCLE" minutes
    2. Delete sign crop files of current cycle based on purely file count(MAX_SIGN_CROP_FILES) that can be max present
*/
void delete_n_old_session_drp_sign_crops(){
    const string SIGN_CROP_OUTPUT_PATH = nd_device_obj->get_sign_crop_base_path();  

    int totalSignCropFiles = signCropFiles.size();
    const int MAX_SIGN_CROP_FILES = drp_clock_hours*60*MAX_SIGN_CROP_PER_SESSION;

    int deletedFilesCnt = 0;

    int batchCnt = 0;
    while(batchCnt++ < MAX_SIGN_CROP_FILE_DELETE_DRP && sign_crop_file_idx < totalSignCropFiles) {
        if(totalSignCropFiles-sign_crop_file_idx > MAX_SIGN_CROP_FILES){
            string expiredFilePath = SIGN_CROP_OUTPUT_PATH + signCropFiles[sign_crop_file_idx].file_name; 
            if(file_delete(expiredFilePath)) {
                LOG_I(TAG, "Deleted expired sign crop file: %s", expiredFilePath.c_str());
                deletedFilesCnt++;
            }
            else {
                backlogSignCropFiles.push_back(signCropFiles[sign_crop_file_idx]);
                LOG_E(TAG, "Failed to delete expired sign crop file: %s", expiredFilePath.c_str());
            }
        }
        else break ;
        sign_crop_file_idx++;
    }
    LOG_I(TAG, "Total file deleted inside delete_n_old_session_drp_sign_crops() deletedFilesCnt: %d", deletedFilesCnt);
}

int delete_n_old_session_drp() {
    int sessions_in_drp_clock_hours = drp_clock_hours*60;
    if(current_sessionCount < sessions_in_drp_clock_hours ){
        // This function will return if device has not run for sessions_in_drp_clock_hours sessions
        LOG_I(TAG, "current_sessionCount: %lld is less than %d", current_sessionCount , sessions_in_drp_clock_hours );
        return 0;
    }
    int rc;
    int oldSession_fileCount = 0 ;
    circular_buffer_file_t fileinfo;
    string val_string;
    std::stringstream val_stream;
///////////////////////// Delete video file //////////////////////////////
    while(1) {
        fileinfo.base_file_name[0] = '\0' ;
        val_stream.str("");
        val_stream.clear();
        // Select oldest file with sessionCount less than (current_sessionCount - sessions_in_drp_clock_hours) and status is not deleted
        // keep it only for zip file. zip files with older OTA will have sessionCount as -1 always. In few scenario, older zip file may not get deleted.
	 val_stream <<
             "SELECT * FROM VIDFILES WHERE SESSIONCOUNT < " <<  current_sessionCount - sessions_in_drp_clock_hours
                        << " AND STATUS != " << CIRCULAR_BUFFER_STATUS_DELETE   << " ORDER BY INDEXID ASC LIMIT 1" ;
        val_string = val_stream.str();
        LOG_I(TAG, "cmd: %s ", val_string.c_str() );
        rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string,
                         callback_complete_fileinfo, (void*)&fileinfo);
        if (rc == false){
            LOG_E(TAG, "Failed to execute %s", val_string.c_str());
            return oldSession_fileCount;
        }
        if (fileinfo.base_file_name[0] == '\0'){
            LOG_I(TAG, "delete_n_old_session_drp() No entry in DB found");
            break;
        }
        else {
            val_stream.str("");
            val_stream.clear();

            if(fileinfo.sessionCount < 0) {
                int64_t sessionCnt =  sessionCount_from_file(fileinfo.base_file_name);
                val_stream << "UPDATE VIDFILES SET "
                    << "SESSIONCOUNT = " << sessionCnt
                    << " WHERE INDEXID == " << fileinfo.index_id ;
                val_string = val_stream.str();
                LOG_I(TAG, "SQL %s", val_string.c_str());
                rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string, NULL, (void*)NULL);
                if(rc == false){
                    LOG_E(TAG, "failed to update VIDFILES, sessionCnt: %d for INDEXID = %d", sessionCnt, fileinfo.index_id);
                    break;
                }

                break;
            }


            delete_entry_from_db(fileinfo.index_id, fileinfo.file_type);

            string file_full_path = nd_device_obj->get_external_eMMC_mount_path() + fileinfo.base_file_name ;
            bool del_status = delete_file_from_sdcard(file_full_path);
            if(del_status == false){
                LOG_E(TAG, "couldn't delete file %s", file_full_path.c_str() );
            }
            else LOG_D(TAG, "delete_n_old_session_drp() deleted file %s", file_full_path.c_str() );
            if(0) {//if (fileinfo.base_file_name[0] == outward_file_prefix) {
            // delete detailed obs file or zip file
                std::string::size_type pos = file_full_path.find("_y.");
                string audioFilePath = file_full_path.substr(0, pos) + "_y.aac";
                string zipFilePath = file_full_path.substr(0, pos) + "_y.zip";
                string alertZipFilePath = file_full_path.substr(0, pos) + "_y_summary.json.zip";
                LOG_D(TAG, "deleting zip file %s", zipFilePath.c_str() );
                del_status = delete_file_from_sdcard(zipFilePath);
            // delete zip file from db
                val_stream.str("");
                val_stream.clear();
                val_stream << "DELETE FROM VIDFILES "
                    << " WHERE NAME == '" <<  zipFilePath.substr(zipFilePath.find("0_trip") ) << "'" ; // get filename from full path filename
                val_string = val_stream.str();
                LOG_I(TAG, "zip file SQL %s", val_string.c_str());

                rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string, NULL, (void*)NULL);
                if(rc == false){
                    LOG_E(TAG, "failed to execute in update_add_del_files_db 2");
                    return false;
                }
                if(file_is_present(alertZipFilePath)){
                // delete alert obs file
                    LOG_I(TAG, "deleting alert zip file %s", alertZipFilePath.c_str() );
                    del_status = delete_file_from_sdcard(alertZipFilePath);
                // delete zip file from db
                    val_stream.str("");
                    val_stream.clear();
                    val_stream << "DELETE FROM VIDFILES "
                        << " WHERE NAME == '" <<  alertZipFilePath.substr(alertZipFilePath.find("0_trip") ) << "'" ; // get filename from full path filename
                    val_string = val_stream.str();
                    LOG_I(TAG, "zip file SQL %s", val_string.c_str());

                    rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string, NULL, (void*)NULL);
                    if(rc == false){
                        LOG_E(TAG, "failed to execute in update_add_del_files_db 2");
                        return false;
                    }
                }
                if(file_is_present(audioFilePath)){
                // delete audio file
                    LOG_I(TAG, "deleting audio  file %s", audioFilePath.c_str() );
                    del_status = delete_file_from_sdcard(audioFilePath);
                // delete audio file from db
                    val_stream.str("");
                    val_stream.clear();
                    val_stream << "DELETE FROM VIDFILES "
                        << " WHERE NAME == '" <<  audioFilePath.substr(audioFilePath.find("0_trip") ) << "'" ; // get filename from full path filename
                    val_string = val_stream.str();
                    LOG_I(TAG, "audi file SQL %s", val_string.c_str());

                    rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string, NULL, (void*)NULL);
                    if(rc == false){
                        LOG_E(TAG, "failed to execute in update_add_del_files_db 2");
                        return false;
                    }
                }
            }

        }
        val_stream.str("");
        val_stream.clear();

        oldSession_fileCount++ ;
        if(oldSession_fileCount > max_file_deletion_drp) break;  
    }
    LOG_D(TAG, "oldSession_fileCount: %d ", oldSession_fileCount);
    // Delete minified observation file 
    delete_n_old_session_drp_minified_obs(INT_COMPLETE_OBS_PATH);
    // Delete sign crop files
    delete_n_old_session_drp_sign_crops(); 
    return oldSession_fileCount ;
}

int64_t get_timeStamp_from_DB(int64_t sessionCount)
{
    int rc;
        LOG_I(TAG, "Get the session creation time of sessionCount: %lld", sessionCount );
        string val_string;
        std::stringstream val_stream;
val_stream << "SELECT TIME FROM VIDFILES WHERE SESSIONCOUNT = " << sessionCount << " ORDER BY INDEXID ASC LIMIT 1 " ;
    val_string = val_stream.str();
        int64_t time_stamp[3] = {0, 0, 0};
        rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string, 
                                callback_count_indx, (void*)&time_stamp[0]);
    LOG_I(TAG, "sqlquery: %s", val_string.c_str() );
        LOG_I(TAG, "time_stamp: %lld, %lld, %lld", time_stamp[0], time_stamp[1] , time_stamp[2]  );
        if(rc == false){
            LOG_E(TAG, "failed to execute");
            return 0;
        }
        return time_stamp[1] ;
    
}
int check_and_delete_null_row_db()
{
    bool rc = false;
    LOG_I(TAG, "Check if DB has empty entry for any field" );
    string val_string;
    std::stringstream val_stream;
    string sql_cond = "TIME IS NULL OR NAME IS NULL OR DURATION IS NULL OR FILE_SIZE IS NULL \
    OR TYPE IS NULL OR STATUS IS NULL OR CAM_TYPE IS NULL OR TRANSCODE_STATUS IS NULL OR FILE_COMPRESSION IS NULL \
    OR UDID IS NULL OR SESSIONCOUNT IS NULL OR UPL_VID_ENABLED IS NULL ";
    val_stream << "SELECT COUNT(*) FROM VIDFILES WHERE " + sql_cond ; 
    val_string = val_stream.str();
    LOG_I(TAG, "sqlquery: %s", val_string.c_str() );
    int64_t emptyRowCount[3] = {0, 0, 0};
    rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string, 
                            callback_count_indx, (void*)&emptyRowCount[0]);
    if(rc == false){
        LOG_E(TAG, "failed to execute");
        return -1;
    }
    if(emptyRowCount[1] > 0){
        LOG_E(TAG, "Total Row with Empty entry: %lld",emptyRowCount[1]  );

        string db_path_file = nd_device_obj->get_db_base_path() + "/" + CIRCULAR_BUFF_DBFILE_NAME;
        LOG_I(TAG, "copying CB db to log folder: %s", db_path_file.c_str() );
        string resp = "";
        int64_t curr_time = get_system_time();
        stringstream ss_temp;
        ss_temp << curr_time ; 
        string dest_path = log_dir + "/" + CIRCULAR_BUFF_DBFILE_NAME + ss_temp.str() + ".log" ;
        if( file_copy(db_path_file, dest_path) ) {
            LOG_I(TAG, "Copied %s", db_path_file.c_str()); 
        } else {
            LOG_E(TAG, "Copy error %s", db_path_file.c_str());
        }
        val_stream.str("");
        val_stream.clear();
        val_stream << "DELETE FROM VIDFILES WHERE " + sql_cond ;
        val_string = val_stream.str();
        LOG_I(TAG, "sqlquery: %s", val_string.c_str() );
        rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string, NULL, (void*)NULL);
        if(rc == false){
            LOG_E(TAG, "failed to execute in update_add_del_files_db 2");
            return -1;
        }
    }
    return emptyRowCount[1];

}
int callback_upl_and_rec_vid_enabled(void *result, int argc, char **argv, char **azColName){
    if(NULL == result){
        LOG_E(TAG,"callback_upl_and_rec_vid_enabled NULL value present in result.");
        return 1;
    }
    circular_buffer_privacy_check_query_result_t* query_result = (circular_buffer_privacy_check_query_result* )result;
    if(query_result->row_count > 0){
        LOG_E(TAG,"More than one file with same name present in DB."); // Suggest better way to handle this scenario
    }
    query_result->row_count++;
    if(NULL != argv[0]){
        if(false == string_to_integer(argv[0],query_result->upl_vid_enabled)){
            LOG_E(TAG,"string to integer conversion for upl_vid_enabled failed");
            query_result->upl_vid_enabled = DEFAULT_VALUE_UPL_VID_ENABLED_COLUMN; // Is this the correct approach or return QUERY_FAILED to uploader
        }
    }
    if(NULL != argv[1]){
        if(false == string_to_integer(argv[1],query_result->rec_vid_enabled)){
            LOG_E(TAG,"string to integer conversion for rev_vid_enabled failed");
            query_result->rec_vid_enabled = DEFAULT_VALUE_REC_VID_ENABLED_COLUMN; // Is this the correct approach or return QUERY_FAILED to uploader
        }
    }
    return 0;
}
// Check privacy for file name present in DB, if HQ file not present check for LQ
upl_to_cb_query_result_t check_video_upload_enabled_based_on_privacy(string filename){
    upl_to_cb_query_result_t upload_reason = CB_UPLOAD_CHECK_QUERY_FAILED;
    LOG_I(TAG,"checking privacy for filename %s",filename.c_str());
    std::stringstream query;
    circular_buffer_privacy_check_query_result_t query_result;
    query_result.row_count = 0;
    query <<
        "SELECT UPL_VID_ENABLED, REC_VID_ENABLED FROM VIDFILES"
        << " WHERE NAME = " << "'" << filename << "';";
    bool query_success = false;
    query_success = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, query.str(),
                                callback_upl_and_rec_vid_enabled, (void*)&query_result);
    if(false == query_success){
        return upload_reason;
    }
    if(query_result.row_count == 0){
        query_success = false;
        upload_reason = CB_UPLOAD_CHECK_QUERY_FAILED;
        LOG_I(TAG,"HQ file not present checking for LQ");
        std::stringstream query_for_ld;
        query_for_ld << "SELECT UPL_VID_ENABLED, REC_VID_ENABLED FROM VIDFILES"
        << " WHERE NAME LIKE " << "'" << filename << "%';";
        query_success = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, query_for_ld.str(),
                                callback_upl_and_rec_vid_enabled, (void*)&query_result);
        if(false == query_success){
            return upload_reason;
        }
	if(query_result.row_count == 0){
	    size_t pos = filename.find_last_of('/');
    	    if (pos != std::string::npos) {
        	filename = filename.substr(pos + 1);
    	    }
	    string file_full_path = nd_device_obj->get_external_eMMC_mount_path() + filename ;
            if(file_is_present(file_full_path)){
		upload_reason = CB_FILE_AVAILABLE_BUT_NOT_IN_DB;
        	LOG_C(TAG,"File is available in storage but not in DB");
	    }
	    else {
		upload_reason = CB_FILE_NOT_PRESENT_IN_DB;
	    }
            return upload_reason;
        }
    }

    if(query_result.rec_vid_enabled == REC_VID_ENABLED_RESET){
        upload_reason = CB_RECORD_PRIVACY_ENABLED;
    }else {
        if(query_result.upl_vid_enabled == UPL_VID_ENABLED_RESET){
            upload_reason = CB_UPLOAD_PRIVACY_ENABLED;
        }else{
            upload_reason = CB_UPLOAD;
        }
    }
    // While updating privacy xattrs, check if extended attributes are enabled, and only update if valid values are present
    // This check is being performed for ADD_FILE_DB flow as well as privacy update flow, while validating the message from bagheera
    if (CIRC_BUFF_ctx->extended_attr_enabled &&
        ((query_result.rec_vid_enabled == REC_VID_ENABLED_SET) || (query_result.rec_vid_enabled == REC_VID_ENABLED_RESET)) &&
        ((query_result.upl_vid_enabled == UPL_VID_ENABLED_SET) || (query_result.upl_vid_enabled == UPL_VID_ENABLED_RESET)) ){
        update_privacy_xattrs(filename, query_result.rec_vid_enabled, query_result.upl_vid_enabled);
        update_privacy_xattrs(filename+ld_extn, query_result.rec_vid_enabled, query_result.upl_vid_enabled);
        if(is_ext_cam((circular_buffer_camtype_t)get_cam_num_from_filename(filename))){
            write_ext_cam_privacy_attr(filename, query_result.rec_vid_enabled, query_result.upl_vid_enabled);
        }
    }
    LOG_I(TAG,"Checked privacy for %s ,reason -> %d",filename.c_str(),upload_reason);
    return upload_reason;
}
#if 0
// Delete all the file with oldest index if multiple files of same name present in DB
bool delete_oldest_entry_with_filename(string filename){
    bool res = false;
    std::stringstream val_stream;
    val_stream  << "DELETE FROM VIDFILES "
                << "WHERE INDEXID NOT IN ( "
                    << "SELECT MAX(INDEXID) "
                    << "FROM VIDFILES "
                    << "WHERE NAME = '" << filename << "' "
                << ") AND "
                << "NAME = '" << filename << "';";
    res = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_stream.str(), NULL, (void*)NULL);
    if(false == res){
        string err_msg = "Unable to delete the duplicate older file present in CB DB.";
        send_critical_info(SM_E_CB_QUERY_EXECUTION_FAILED,NDService::UNUSED_ERR_AUX_CODE,err_msg);
    }
    return res;
}
bool get_fileinfo_from_filename(circular_buffer_file_t* fileinfo, char* base_file_name)
{
    char *zErrMsg = 0;
    int rc;
    bool ret = true;

    string val_string;
    std::stringstream val_stream;
    val_stream <<
                 "SELECT * FROM VIDFILES WHERE NAME == '"
                 << base_file_name << "'";

    val_string = val_stream.str();
    rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string,
                     callback_complete_fileinfo, (void*)fileinfo, &zErrMsg);
    if (rc == false){
        LOG_E(TAG, "Failed to execute %s", val_string.c_str());
        return false;
    }

    return ret;
}
int64_t get_timeStamp_from_DB_test(char filename[250] )
{
    int rc;
        LOG_I(TAG, "Get the session creation time of file: %s", filename );
        string val_string, name_str = filename;
        std::stringstream val_stream;
val_stream << "SELECT TIME FROM VIDFILES WHERE NAME LIKE  \"" << name_str << "%\"" << " ORDER BY INDEXID ASC LIMIT 1 " ;
//val_stream << "SELECT TIME FROM VIDFILES WHERE NAME = " << name_str << " ORDER BY INDEXID ASC LIMIT 1 " ;
    val_string = val_stream.str();
        int64_t time_stamp[3] = {0, 0, 0};
        rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string,
                                callback_count_indx, (void*)&time_stamp[0]);
    LOG_I(TAG, "sqlquery: %s", val_string.c_str() );
        LOG_I(TAG, "time_stamp: %lld, %lld, %lld", time_stamp[0], time_stamp[1] , time_stamp[2]  );
        if(rc == false){
            LOG_E(TAG, "failed to execute");
            return 0;
        }
        return time_stamp[1] ;

}

#endif

//Writing update file metadata as xattrs
bool write_updatefile_metadata_to_xattrs(const circular_buffer_fileinfo_t& fileinfo){
    bool success = true;
    if (CIRC_BUFF_ctx->extended_attr_enabled){
        string filepath = nd_device_obj->get_external_eMMC_mount_path() + fileinfo.base_file_name;
        

        success &= (set_file_xattr(filepath, FileMetadataKey::size, &fileinfo.file_size, sizeof(fileinfo.file_size)) == XATTR_OK);

        success &= (set_file_xattr(filepath, FileMetadataKey::file_type, &fileinfo.file_type, sizeof(fileinfo.file_type)) == XATTR_OK);

        if(fileinfo.upl_vid_enabled != UPL_VID_ENABLED_DO_NOT_UPDATE)
        success &= (set_file_xattr(filepath, FileMetadataKey::upl_vid, &fileinfo.upl_vid_enabled, sizeof(fileinfo.upl_vid_enabled)) == XATTR_OK);

        if(fileinfo.rec_vid_enabled != REC_VID_ENABLED_DO_NOT_UPDATE)
        success &= (set_file_xattr(filepath, FileMetadataKey::rec_vid, &fileinfo.rec_vid_enabled, sizeof(fileinfo.rec_vid_enabled)) == XATTR_OK);

        // Set cam_type as well in xattr
        success &= (set_file_xattr(filepath, FileMetadataKey::cam_type, &fileinfo.camtype, sizeof(fileinfo.camtype)) == XATTR_OK);

        if(success){
            LOG_I(TAG, "Successfully updated xattrs for file: %s", filepath.c_str());
        } else {
            LOG_E(TAG, "Failed to update some xattrs for file: %s", filepath.c_str());
        }
    }    
    return success;
}
bool update_status_xattr(int i, pair < vector< json_add_del >, vector< json_add_del > >& add_del_files){
    if (CIRC_BUFF_ctx->extended_attr_enabled){
        string base_file_name=add_del_files.first[i].file_name;
        string filepath = nd_device_obj->get_external_eMMC_mount_path() + base_file_name;
         circular_buffer_filestatus_t status=CIRCULAR_BUFFER_STATUS_NOTIFIED;
        if(set_file_xattr(filepath, FileMetadataKey::status, &status, sizeof(status)) == 0) {
            LOG_D(TAG, "Successfully updated status xattr for file: %s", filepath.c_str());
        } else {
            LOG_E(TAG, "Failed to update status xattr for file: %s", filepath.c_str());
            return false;
        }
    }
    return true;   
}
bool update_privacy_xattrs(string filename, int rec_vid_enabled, int upl_vid_enabled ){
    string filepath=nd_device_obj->get_external_eMMC_mount_path()+filename;
    if(file_is_present(filepath)){
      LOG_D(TAG,"file present in directory %s, rec_vid_enabled: %d, upl_vid_enabled: %d",filepath.c_str() ,rec_vid_enabled, upl_vid_enabled);
      bool success=true;
      success&=(set_file_xattr(filepath, FileMetadataKey::rec_vid, &rec_vid_enabled, sizeof(rec_vid_enabled)) == XATTR_OK);
      success&=(set_file_xattr(filepath, FileMetadataKey::upl_vid, &upl_vid_enabled, sizeof(upl_vid_enabled)) == XATTR_OK);
      if(success)
       LOG_I(TAG,"Successfully updated privacy xattrs for file %s",filename.c_str());
      else
       LOG_E(TAG,"Failed to update privacy xattrs for file %s",filename.c_str());
    } 
    return true; 
}

 bool retrieve_file_info_from_xattrs(string file_full_path, file_info_str& file_data){
  
    // Retrieve file type
     if(get_file_xattr(file_full_path, FileMetadataKey::file_type, &file_data.file_type, sizeof(file_data.file_type)) != XATTR_OK)
       return false;

    // Retrieve file_compression
    if(get_file_xattr(file_full_path, FileMetadataKey::compr_type, &file_data.file_compression, sizeof(file_data.file_compression)) != XATTR_OK)
      return false;  

    // Retrieve file size
    if(get_file_xattr(file_full_path, FileMetadataKey::size, &file_data.file_size, sizeof(file_data.file_size)) != XATTR_OK)
      return false; 

   return true;
 }

bool matching_file_list_info_from_xattrs(string filename_matching, vector<file_info_str>& file_data_list){
    
    if (CIRC_BUFF_ctx->extended_attr_enabled){
        string hd_extn="_y.mp4";
        string path=nd_device_obj->get_external_eMMC_mount_path();
        //outward and inward cameras(0,1)
        for(int i=CIRCULAR_BUFFER_CAM_FRONT; i<=CIRCULAR_BUFFER_CAM_DRIVER; i++){
            string filename=filename_matching;
            filename[0]='0' + i;
            //hd file
            filename=filename + hd_extn;
            string file_full_path=path+filename;
            if(file_is_present(file_full_path)){
                file_info_str file_data;
                file_data.file_name=filename;
                if(retrieve_file_info_from_xattrs(file_full_path, file_data)){
                    file_data_list.push_back(file_data);
                }
                else {
                    return false;
                }       
            }
            //ld file      
            filename=filename + ld_extn;
            file_full_path=path+filename;
            if(file_is_present(file_full_path)){
                file_info_str file_data;
                file_data.file_name=filename;
                if(retrieve_file_info_from_xattrs(file_full_path, file_data)){
                    file_data_list.push_back(file_data);    
                }
                else {
                    return false;
                }    
            }
        }

        // zip file
        string filename=filename_matching;
        filename[0]='0';
        string zipFileName=filename + "_y.zip";
        string file_full_path=path+zipFileName;
        if(file_is_present(file_full_path)){
            file_info_str file_data;
            file_data.file_name=zipFileName;
            if(retrieve_file_info_from_xattrs(file_full_path, file_data)){
                file_data_list.push_back(file_data);
            }
            else {
                return false;
            }
        }

        // audio file
        filename=filename_matching;
        filename[0]='0';
        string audioFileName=filename + "_y.aac";
        file_full_path=path+audioFileName;
        if(file_is_present(file_full_path)){
            file_info_str file_data;
            file_data.file_name=audioFileName;
            if(retrieve_file_info_from_xattrs(file_full_path, file_data)){
                file_data_list.push_back(file_data);
            }
            else {
                return false;
            }
        }
        
        //side cameras and ext cameras(2,3,4,5,6,7)
        for(int i=CIRCULAR_BUFFER_CAM_LEFT; i<=CIRCULAR_BUFFER_CAM_EXT4; i++){
            filename=filename_matching;
            filename[0]='0' + i;
            filename=filename + hd_extn;
            file_full_path=path+filename;
            if(file_is_present(file_full_path)){
                file_info_str file_data;
                file_data.file_name=filename;
                if(retrieve_file_info_from_xattrs(file_full_path, file_data)){
                    file_data_list.push_back(file_data);
                }
                else {
                    return false;
                }       
            }  
        }

        //DMS camera
        filename=filename_matching;
        filename[0]='0' + CIRCULAR_BUFFER_CAM_DMS;;
        filename=filename + hd_extn;
        file_full_path=path+filename;
        if(file_is_present(file_full_path)){
            file_info_str file_data;
            if(retrieve_file_info_from_xattrs(file_full_path, file_data)){
                file_data.file_name=filename;
                file_data_list.push_back(file_data);
            }
            else {
                return false;
            }       
        }  
        //DMS ld file    
        filename=filename + ld_extn;
        file_full_path=path+filename;
        if(file_is_present(file_full_path)){
            file_info_str file_data;
            file_data.file_name=filename;
            if(retrieve_file_info_from_xattrs(file_full_path, file_data)){
                file_data_list.push_back(file_data);
            }
            else {
                return false;
            }       
        }
        
        return true;
        
    } 
    return false;    
 }