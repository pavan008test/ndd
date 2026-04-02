
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

#define DEBUG 1 
#define DMS_CAM_TYPE 8 

#if DEBUG
#define CALLBACK callback
#else
#define CALLBACK NULL
#endif

static const char *TAG="TC_SQL";


extern circular_buffer_vid *CIRC_BUFF_ctx;
extern int64_t NUM_DMS_ALERT_VIDEOS_MAX; 
extern bool store_dms_file;
static const string ld_extn = ".ld.mp4";

/// call backs for sqlite start

static int callback(void *NotUsed, int argc, char **argv, char **azColName){
   int i;
   for(i=0; i<argc; i++){
      LOG_D(TAG, "%s = %s\n", azColName[i], argv[i] ? argv[i] : "NULL");
   }
   LOG_D(TAG, " &&&&& ARGC count %d &&&&& ",argc);
   return 0;
}

static int callback_count_rows(void *count, int argc, char **argv, char **azColName){
    int i;
    int64_t *temp_count = (int64_t* )count;

    temp_count[0] = temp_count[0] + argc;
    if (argc != 1){
        temp_count[1] = 0;
    } else {
        temp_count[1] += argv[0] ? atoi(argv[0]) : 0;
    }
    return 0;
}

static int callback_delete_status(void* status, int argc, char** argv, char **azColName)
{
    int i;
    int64_t* temp_status = (int64_t *) status;
    temp_status[0] = temp_status[0] + argc;
    if (argc != 1){
        temp_status[1] = (int)CIRCULAR_BUFFER_STATUS_ERROR;
    } else {
        temp_status[1] = argv[0] ? atoi(argv[0]) : 0;
    }

    return 0;
}

static int callback_transcode_status(void* status, int argc, char** argv, char **azColName)
{
    int i;
    int64_t* temp_status = (int64_t *) status;
    temp_status[0] = temp_status[0] + argc;
    if (argc != 1){
        temp_status[1] = (int)CIRCULAR_BUFFER_TC_STATUS_ERROR;
    } else {
        temp_status[1] = argv[0] ? atoi(argv[0]) : 0;
    }

    return 0;
}

static int callback_file_compression_type(void* fc_type, int argc, char** argv, char** azColName)
{
    int i;
    int64_t* temp_fc_type = (int64_t*) fc_type;
    temp_fc_type[0] = temp_fc_type[0] + argc;
    if (argc !=1){
        temp_fc_type[1] = (int) CIRCULAR_BUFFER_COMPRESSION_ERROR;
    }else{
        temp_fc_type[1] = argv[0] ? atoi(argv[0]) : 0;
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
bool get_oldest_dms_file(circular_buffer_file_t* fileinfo, bool hq_file)
{
    int rc ;
    std::stringstream val_stream;
    string name_like = "8%_y.mp4";
    if (!hq_file) {
	name_like += ld_extn;
    }
	val_stream << "SELECT * FROM VIDFILES WHERE INDEXID = ("
               << "SELECT MIN(INDEXID) FROM VIDFILES "
               << "WHERE STATUS != " << CIRCULAR_BUFFER_STATUS_DELETE
               << " AND CAM_TYPE = " << CIRCULAR_BUFFER_CAM_DMS
               << " AND NAME LIKE \"" << name_like << "\"";

    if (!hq_file) {
        val_stream << " AND FILE_COMPRESSION != " << CIRCULAR_BUFFER_NO_COMPRESSION;
    }
    else {
        val_stream << " AND TRANSCODE_STATUS != " << CIRCULAR_BUFFER_TC_STATUS_TRANSCODED;

    }

    val_stream << ")";

    std::string val_string = val_stream.str();
    LOG_I(TAG, "SQL Query: %s", val_string.c_str());

    rc = CIRC_BUFF_ctx->exec_cmd_db(
        CIRC_BUFF_ctx->db_handle,
        val_string,
        callback_complete_fileinfo,
        (void*)fileinfo);

    if (!rc) {
        LOG_E(TAG, "Failed to execute: %s", val_string.c_str());
        return false;
    }
	return true;

}
bool get_file_to_transcode(circular_buffer_file_t* fileinfo, circular_buffer_camtype_t cam_type)
{
    int rc;
    bool ret = true;
    int tc_status = CIRCULAR_BUFFER_TC_STATUS_WAITING;
    string val_string;
    std::stringstream val_stream;
    // choose oldest normal or alert videos that is not deleted or currently transcoding or
    // transcode failed or transcoded
    // choosing only front camera 
    int oldest_transcode_file_index = 0 ;
    if(cam_type == DMS_CAM_TYPE && store_dms_file == false){
        int64_t count_rows[2] = {0, 0};
// Count total alert dms file
        val_stream <<
            "SELECT COUNT(*) from VIDFILES WHERE NAME LIKE \"8%_y.mp4\" AND (STATUS != " 
            << CIRCULAR_BUFFER_STATUS_DELETE << ")" << " AND ( FILE_COMPRESSION IN ("
            << CIRCULAR_BUFFER_NO_COMPRESSION << ",  "
            << CIRCULAR_BUFFER_NO_COMPRESSION_ADJ << ") )" ;

        val_string = val_stream.str();
        rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string,
                callback_count_rows, (void*)&count_rows[0]);
        if(rc == false){
            LOG_E(TAG, "failed to execute");
            return false;
        }

        LOG_D(TAG, "cmd: %s" , val_string.c_str() );
        LOG_D(TAG, "total alert dms file count %lld , %lld", count_rows[0], count_rows[1]);
        int total_alert_dms_file = count_rows[1] ;
        int oldest_alert_dms_file_index = 0 ;
        int oldest_non_alert_dms_file_index = 0 ;

        val_stream.str("");
        val_stream.clear();
// oldest alert dms file index
        val_stream <<
            "SELECT min(INDEXID) from VIDFILES WHERE NAME LIKE \"8%_y.mp4\"  AND (STATUS != " 
            << CIRCULAR_BUFFER_STATUS_DELETE << ")" << " AND ( FILE_COMPRESSION IN ("
            << CIRCULAR_BUFFER_NO_COMPRESSION << ",  "
            << CIRCULAR_BUFFER_NO_COMPRESSION_ADJ << ") )" ;
        val_string = val_stream.str();
        int64_t count_indx[3] = {0, 0, 0};
        rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string, 
                callback_count_indx, (void*)&count_indx[0]);
        if(rc == false){
            LOG_E(TAG, "failed to execute %s", val_string.c_str());
            return false;
        }
        LOG_D(TAG, "get_file_to_transcode callback_count_indx Return values %lld %lld %lld",
                count_indx[0], count_indx[1], count_indx[2]);
        if (count_indx[0] == 0){
            LOG_I(TAG, "Sql query for get_file_to_transcode returned 0 results");
            return false;
        }
        oldest_alert_dms_file_index = count_indx[1] ;

        val_stream.str("");
        val_stream.clear();
// oldest non-alert dms file index
        val_stream <<
            "SELECT min(INDEXID) from VIDFILES WHERE NAME LIKE \"8%_y.mp4\" AND (STATUS != " 
            << CIRCULAR_BUFFER_STATUS_DELETE << ")" << " AND ( FILE_COMPRESSION NOT IN ("
            << CIRCULAR_BUFFER_NO_COMPRESSION << ",  "
            << CIRCULAR_BUFFER_NO_COMPRESSION_ADJ << ") )" ;
        val_string = val_stream.str();
        count_indx[0] = 0 ;
        count_indx[1] = 0 ;
        rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string, 
                callback_count_indx, (void*)&count_indx[0]);
        if(rc == false){
            LOG_E(TAG, "failed to execute %s", val_string.c_str());
            return false;
        }
        LOG_D(TAG, "get_file_to_transcode callback_count_indx %lld %lld %lld",
                count_indx[0], count_indx[1], count_indx[2]);
        if (count_indx[0] == 0){
            LOG_I(TAG, "Sql query for get_file_to_transcode returned 0 results");
            return false;
        }
        oldest_non_alert_dms_file_index = count_indx[1] ;
        if(total_alert_dms_file > NUM_DMS_ALERT_VIDEOS_MAX && oldest_alert_dms_file_index < oldest_non_alert_dms_file_index ) {
            LOG_I(TAG, "total_alert_dms_file: %d, oldest_alert index: %lld oldest_non_alert index: %lld", \
                    total_alert_dms_file,oldest_alert_dms_file_index,oldest_non_alert_dms_file_index );
            oldest_transcode_file_index = oldest_alert_dms_file_index ;
        }
        else {

            LOG_D(TAG, "total_alert_dms_file: %d , oldest_alert index: %lld oldest_non_alert index: %lld", \
                    total_alert_dms_file,oldest_alert_dms_file_index,oldest_non_alert_dms_file_index );
            oldest_transcode_file_index = oldest_non_alert_dms_file_index ;
        }

    }
    else{
        val_stream <<
            "SELECT min(INDEXID) from VIDFILES WHERE (TRANSCODE_STATUS == "
            << tc_status << " ) AND ( TYPE IN (" 
            << CIRCULAR_BUFFER_TYPE_NORMAL << ",  " 
            << CIRCULAR_BUFFER_TYPE_ALERT << ") ) AND (STATUS != " 
            << CIRCULAR_BUFFER_STATUS_DELETE << ")" << " AND (CAM_TYPE == "
            << cam_type << ")" ;

        val_string = val_stream.str();
        int64_t count_indx[3] = {0, 0, 0};
        rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string, 
                callback_count_indx, (void*)&count_indx[0]);
        if(rc == false){
            LOG_E(TAG, "failed to execute %s", val_string.c_str());
            return false;
        }
        LOG_D(TAG, "get_file_to_transcode callback_count_indx Return values %lld %lld %lld",
                count_indx[0], count_indx[1], count_indx[2]);
        if (count_indx[0] == 0){
            LOG_I(TAG, "Sql query for get_file_to_transcode returned 0 results");
            return false;
        }
        oldest_transcode_file_index = count_indx[1];
    }

    val_stream.str("");
    val_stream.clear();
    val_stream << "SELECT * from VIDFILES WHERE INDEXID == " << oldest_transcode_file_index ; 
    val_string = val_stream.str();
    rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string, 
            callback_complete_fileinfo, (void*)fileinfo);
    if(rc == false){
        LOG_E(TAG, "failed to execute");
        return false;
    }

    return ret;
}


bool check_delete_status(int64_t indexid)
{
    int rc;

    string val_string;
    std::stringstream val_stream;
    //check if the file was deleted
    val_stream << 
                "SELECT STATUS from VIDFILES WHERE INDEXID == "
                << indexid; 
    val_string = val_stream.str();
    
    int64_t data[2] = {0, 0};
    rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string,
                     callback_delete_status, (void*)&data[0]);
    if(rc == false){
        LOG_E(TAG, "failed to execute");
        return false;
    }
    
    LOG_D(TAG, "Check delete status result =  %lld %lld", data[0], data[1]);
    if (data[1] == (int)CIRCULAR_BUFFER_STATUS_DELETE){
        LOG_I(TAG, "File deleted index_id == %d", indexid);
        return true;
    } else {
        return false;
    }
}


bool check_file_transcode_status(int64_t indexid, circular_buffer_transcode_status_t tc_status)
{   
    int rc;
    bool ret = true;

    string val_string;
    std::stringstream val_stream;
    //check if the file was deleted
    val_stream << 
                "SELECT TRANSCODE_STATUS from VIDFILES WHERE INDEXID == "
                << indexid; 
    val_string = val_stream.str();
    
    int64_t data[2] = {0, 0};
    rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string,
                     callback_transcode_status, (void*)&data[0]);
    if(rc == false){
        LOG_E(TAG, "failed to execute");
        return false;
    }
    
    LOG_D(TAG, "Check transcode_status result =  %lld %lld", data[0], data[1]);
    if (data[1] == (int)tc_status){
        LOG_I(TAG, "index_id == %d tc_status in db matches with passed parameter %d", indexid, (int) tc_status);
        return true;
    } else {
        return false;
    }
}

bool check_file_compression_type(int64_t indexid, circular_buffer_file_compression_t fc_type)
{
    int rc;
    bool ret = true;

    string val_string;
    std::stringstream val_stream;
    //check if the file was deleted
    val_stream <<
                "SELECT FILE_COMPRESSION from VIDFILES WHERE INDEXID == "
                << indexid;
    val_string = val_stream.str();

    int64_t data[2] = {0, 0};
    rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string,
                     callback_file_compression_type, (void*)&data[0]);
    if(rc == false){
        LOG_E(TAG, "failed to execute");
        return false;
    }

    LOG_D(TAG, "Check file_compression_type result =  %lld %lld", data[0], data[1]);
    if (data[1] == (int)fc_type){
        LOG_I(TAG, "index_id == %d file_compression in db matches with passed parameter %d", indexid, (int) fc_type);
        return true;
    }

    return false;
}


bool get_num_hq_videos_db(int64_t* num_hq_videos_db, circular_buffer_camtype_t cam_type)
{
    int rc;
    bool ret = true;
    int tc_status = CIRCULAR_BUFFER_TC_STATUS_WAITING ;
    string val_string;
    std::stringstream val_stream;
    if(cam_type == DMS_CAM_TYPE && store_dms_file == false){
        // fetching the number of DMS camera videos
        val_stream << 
            "SELECT COUNT(*) from VIDFILES WHERE STATUS != " 
            << CIRCULAR_BUFFER_STATUS_DELETE << " AND NAME LIKE \"8%_y.mp4\" " ; 
    }
    else {
        //Only fetching the number of HQ front camera videos
        val_stream <<
            "SELECT COUNT(*) from VIDFILES WHERE (TRANSCODE_STATUS == "
            << tc_status << " ) AND ( TYPE IN ("
            << CIRCULAR_BUFFER_TYPE_NORMAL << ",  "
            << CIRCULAR_BUFFER_TYPE_ALERT << ") ) AND (STATUS != "
            << CIRCULAR_BUFFER_STATUS_DELETE << ")" << " AND (CAM_TYPE == "
            << cam_type << ")"
            << "AND NAME LIKE \"%_y.mp4\" " ;
    }
    val_string = val_stream.str();
    int64_t count_rows[2] = {0, 0};
    rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string,
            callback_count_rows, (void*)&count_rows[0]);
    if(rc == false){
        LOG_E(TAG, "failed to execute");
        return false;
    }

    LOG_D(TAG, "get_num_hq_videos : count_rows Return values %lld %lld", count_rows[0], count_rows[1]);

    *num_hq_videos_db = count_rows[1];

    return ret;
}

bool get_num_lq_videos_db(int64_t* num_lq_videos_db, circular_buffer_camtype_t cam_type)
{
	int rc;
	bool ret = true;

	string val_string;
	std::stringstream val_stream;

	val_stream <<
		"SELECT COUNT(*) from VIDFILES WHERE ( TYPE IN ("
		<< CIRCULAR_BUFFER_TYPE_NORMAL << ",  "
		<< CIRCULAR_BUFFER_TYPE_ALERT << ") ) AND STATUS != "
		<< CIRCULAR_BUFFER_STATUS_DELETE << " AND CAM_TYPE == "
		<< cam_type << " AND FILE_COMPRESSION != "
        <<CIRCULAR_BUFFER_NO_COMPRESSION
		<< " AND NAME LIKE \"%_y.mp4.ld.mp4\" " ;
	val_string = val_stream.str();
	int64_t count_rows[2] = {0, 0};
	rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string,
			callback_count_rows, (void*)&count_rows[0]);
	if(rc == false){
		LOG_E(TAG, "failed to execute");
		return false;
	}

	LOG_I(TAG, "get_num_lq_videos : count_rows Return values %lld %lld", count_rows[0], count_rows[1]);

	*num_lq_videos_db = count_rows[1];

	return ret;
}



bool update_tc_status_db_cleanup()
{
    //function to update status of TRANSCODING entries in db to WAITING
    //this function is to be run at clean up 
    //main aim is to update status of those videos which were being transcoded and 
    //the transcoding process was interrupted resulting in an unresolved status. 
    //typical situations when this might occur is if the device undergoes a shutdown
    //or circular_buffer service is restarted in the middle of a transcoding session
    int rc;
    bool ret;

    string val_string;
    std::stringstream val_stream;
    val_stream.str("");
    val_stream.clear();
    val_stream << "UPDATE VIDFILES SET " 
                << "TRANSCODE_STATUS = " << CIRCULAR_BUFFER_TC_STATUS_WAITING
                << " WHERE TRANSCODE_STATUS ==" << CIRCULAR_BUFFER_TC_STATUS_TRANSCODING;

    val_string = val_stream.str();
    rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, 
                                val_string, CALLBACK, 0);
    if(rc == false){
        LOG_E(TAG, "failed to execute %s", val_string.c_str());
        return false;
    }
    return true;
}

bool update_tc_status_filesize_DB(int indexid, int filesize, circular_buffer_transcode_status_t status)
{
    int rc;
    bool ret;

    string val_string;
    std::stringstream val_stream;
    val_stream.str("");
    val_stream.clear();
    val_stream << "UPDATE VIDFILES SET"
                << " FILE_SIZE = " << filesize << ","
                << " TRANSCODE_STATUS = " << status
                << " WHERE INDEXID==" << indexid;

    val_string = val_stream.str();
    rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle,
                                val_string, CALLBACK, 0);
    if(rc == false){
        LOG_E(TAG, "failed to execute %s", val_string.c_str());
        return false;
    }
    return true;
}



bool update_name_type_status_DB(circular_buffer_file_t update_fileinfo)
{
    int rc;
    bool ret;

    string val_string;
    std::stringstream val_stream;
    val_stream.str("");
    val_stream.clear();
    val_stream << "UPDATE VIDFILES SET" 
                << " NAME = '" << update_fileinfo.base_file_name <<"',"
                << " STATUS = " << update_fileinfo.status << ","
                << " FILE_SIZE = " << update_fileinfo.file_size << ","
                << " TYPE = " << update_fileinfo.file_type 
                << " WHERE INDEXID==" << update_fileinfo.index_id ;

    val_string = val_stream.str();
        rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, 
                                val_string, CALLBACK, 0);
    if(rc == false){
        LOG_E(TAG, "failed to execute %s", val_string.c_str());
        return false;
    }
    return true;
}


static int callback_col_names(void* str_vector, int argc, char** argv, char **azColName){
    vector<string>* t_str_vector = (vector < string > *) str_vector;
    string tstring;
    
    if (argc < 2){
        LOG_E(TAG, "Something went wrong in callback_col_names\n");
        return -1;
    }

    tstring = argv[1];
    t_str_vector->push_back(tstring);

    return 0;
}

static int callback_num_video_files(void* vector_num_video_files, int argc, char** argv, char** azColName){

    vector<num_video_file_data_t>* t_vector_num_video_files = (vector<num_video_file_data_t> *) vector_num_video_files;

    num_video_file_data_t t_data;

    LOG_D(TAG, "Inside callback_num_video_files, %d values returned", argc);

    if (argc != 3){
        LOG_E(TAG, "Error in getting num_video files - expected 3 cols, got back %d columns\n", argc);
        return -1;
    }

    t_data.cam_type = (circular_buffer_camtype_t) (argv[0] ? strtoll (argv[0], NULL, 10) : CIRCULAR_BUFFER_CAM_ERROR);
    t_data.tc_status = (circular_buffer_transcode_status_t) (argv[1] ? strtoll (argv[1], NULL, 10) : CIRCULAR_BUFFER_TC_STATUS_ERROR);
    t_data.num_files = argv[2] ? strtoll (argv[2], NULL, 10) : 0;

    t_vector_num_video_files->push_back(t_data);

    return 0;
}
    


bool get_num_video_audio_files(vector<num_video_file_data_t>& video_file_data)
{
    bool rc=false;
    std::stringstream val_stream;

    val_stream  << "SELECT CAM_TYPE, TRANSCODE_STATUS, COUNT(TRANSCODE_STATUS) as NUMFILES FROM " 
                << "VIDFILES WHERE TYPE >= " << CIRCULAR_BUFFER_TYPE_START << " AND TYPE <= " 
                << CIRCULAR_BUFFER_TYPE_STOP << " GROUP BY CAM_TYPE, TRANSCODE_STATUS";
    string val_string;
    val_string = val_stream.str();

    rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string, callback_num_video_files, 
                    (void *) &video_file_data);

    if (!rc){
        LOG_E(TAG, "Failed to execute %s", val_string.c_str());
        return false;
    }

    val_string = "SELECT COUNT(*) from VIDFILES WHERE NAME like \"%.aac\"";
    int64_t count_rows[2] = {0, 0};
    rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string,
                     callback_count_rows, (void*)&count_rows[0]);
    if (!rc){
        LOG_E(TAG, "Failed to execute %s", val_string.c_str());
        return false;
    }
    LOG_I(TAG, "number of aac files in DB are %lld", count_rows[1]);

    return true;
}

bool check_col_exists(string column_name)
{
    bool ret = false, rc=false;
    string sql;
    vector<string> col_names;
    sql = "PRAGMA table_info(VIDFILES)";

    rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, sql, callback_col_names, (void*) &col_names);
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



bool add_tc_status_col()
{
    bool rc;
    string column_name = "TRANSCODE_STATUS";
    string val_string;
    std::stringstream val_stream;
    val_stream << "ALTER TABLE VIDFILES ADD COLUMN " << column_name << " INTEGER DEFAULT 0;";
    val_string = val_stream.str();
 
    rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string, NULL, (void*) NULL);
    if(rc == false){
        LOG_E(TAG, "failed to execute %s", val_string.c_str());
        return rc;
    }
    LOG_I(TAG, "TRANSCODE_STATUS column created using %s", val_string.c_str());
    
    val_stream.str("");
    val_stream.clear();    
    val_stream << "UPDATE VIDFILES SET TRANSCODE_STATUS=" << CIRCULAR_BUFFER_TC_STATUS_IGNORE; 
    
    val_string = val_stream.str();
    rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string, NULL, (void*) NULL);
    
    if(rc == false){
        LOG_E(TAG, "failed to execute %s", val_string.c_str());
    }
    
    LOG_I(TAG, "TRANSCODE_STATUS column SET using %s", val_string.c_str());
    return rc;
}

//Note only adds integer columns with default == 0
bool add_col_to_db(string column_name, int default_value)
{
    bool rc;
    string val_string;
    std::stringstream val_stream;
    val_stream << "ALTER TABLE VIDFILES ADD COLUMN " << column_name
               << " INTEGER DEFAULT " << default_value <<";";
    val_string = val_stream.str();

    rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string, NULL, (void*) NULL);
    if(rc == false){
        LOG_E(TAG, "failed to execute %s", val_string.c_str());
        return rc;
    }
    LOG_I(TAG, "%s column created using %s", column_name.c_str(), val_string.c_str());

    return rc;
}

//Note only works for integer colunms
bool update_col_with_value(string column_name, int value){
    bool rc;
    string val_string;
    std::stringstream val_stream;
    val_stream << "UPDATE VIDFILES SET " << column_name << " = " << value;

    val_string = val_stream.str();
    rc = CIRC_BUFF_ctx->exec_cmd_db(CIRC_BUFF_ctx->db_handle, val_string, NULL, (void*) NULL);

    if(rc == false){
        LOG_E(TAG, "failed to execute %s", val_string.c_str());
    }

    LOG_I(TAG, "%s column SET using %s", column_name.c_str(), val_string.c_str());
    return rc;
}

bool sanitize_db_for_tc()
{
    bool tc_status_col_exists = false, add_col_attempt = false;
    string tc_col_name = "TRANSCODE_STATUS";
    tc_status_col_exists = check_col_exists(tc_col_name);
    if (!tc_status_col_exists){
        LOG_I(TAG, "%s column does not exist", tc_col_name.c_str());
        add_col_attempt = add_tc_status_col();
        if (!add_col_attempt){
            LOG_E(TAG, "Unable to add %s column to DB. Sanitize DB failed", tc_col_name.c_str());
            return false;
        }
    }

    //TRANSCODE STATUS Column exists / has been created
    //Cleanup the TRANSCODE_STATUS
    bool tc_cleanup = update_tc_status_db_cleanup();
    if (!tc_cleanup){
        LOG_E(TAG, "Unable to clean TRANSCODE_STATUS column in DB. Sanitize DB failed");
        return false;
    }

    //Add column for Video compression
    bool file_compression_col_exists = false;
    string compression_col_name = "FILE_COMPRESSION";
    file_compression_col_exists = check_col_exists(compression_col_name);
    if (!file_compression_col_exists){
        LOG_I(TAG, "%s column does not exist", compression_col_name.c_str());
        add_col_attempt = add_col_to_db(compression_col_name, (int) CIRCULAR_BUFFER_MEDIUM_COMPRESSION);
        if (!add_col_attempt){
            LOG_E(TAG, "unable to add %s column to DB. Sanitize DB failed",
                compression_col_name.c_str());
            return false;
        }

    }

    return true;
}
