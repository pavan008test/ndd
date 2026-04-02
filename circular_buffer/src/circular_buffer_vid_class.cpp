

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
#include <nd_msg_utils.h> 
#include <sys/statvfs.h>
#include <nd_file_utils.h>
#include "service_utils.h"
#include "nd_msg_types.h"

using namespace std;

static const char *TAG="CB_VID";
static int no_of_critical_info = 0;
extern NDService *nd_service_obj;
static const int SQLITE3_BUSY_TIMEOUT = 5000; //sqlite3 timeout in ms

circular_buffer_vid::circular_buffer_vid(string db, string table_format)
{
}

circular_buffer_vid::~circular_buffer_vid()
{
}

circular_buffer_vid* circular_buffer_vid::get_circular_buffer(string db, string table_format)
{
    circular_buffer_vid* circular_buffer = new circular_buffer_vid(db, table_format);

    if(circular_buffer == NULL)
        return NULL;

    return circular_buffer;
}

bool circular_buffer_vid::delete_circular_buffer( circular_buffer_vid* circular_buffer ){
    if( circular_buffer != NULL ) {
        delete circular_buffer;
        return true;
    }
    return false;
}


bool circular_buffer_vid::init_circular_buffer(circular_buffer_header_t header)
{
    this->header = header;
    return true;
}

void circular_buffer_vid::handle_db_corruption(db_handle_t* db_handle, int rc){
    //close connection
    bool ret;
    LOG_E(TAG, "DB_CORRUPTION_HANDLING function invoked");
    // DBFILENAME returns the full path
    const char* DBFILENAME = sqlite3_db_filename(db_handle, "main");
    LOG_I(TAG, "DB_CORRUPTION_HANDLING: filename = %s", DBFILENAME);
    pthread_mutex_lock(&db_handle_mutex);
    ret = close_db(db_handle);
    pthread_mutex_unlock(&db_handle_mutex);
    if (!ret){
        LOG_E(TAG, "DB_CORRUPTION_HANDLING: Unable to close db handle. Deleting db file anyways");
    }
    string db_path_file = DBFILENAME;
    ret = file_delete(db_path_file);
    if (!ret){
        LOG_E(TAG, "DB_CORRUPTION_HANDLING: File delete failed. Exiting and restarting process anyways");
    }
    string str_msg = db_path_file + " deleting because of corrupted DB error";
    nd_service_obj->send_err_msg(SM_E_CB_DB_CORRUPT, rc, str_msg );
    // sleep before exiting just to make sure all jobs are done
    sleep(2);
    file_touch(CB_DB_CORRUPTION_FILE);
    LOG_I(TAG, "DB_CORRUPTION_HANDLING: Exiting process");
    _exit(1);
}

bool circular_buffer_vid::exec_cmd_db(db_handle_t* db_handle, const string command, 
        int (*callback)(void*,int,char**,char**), void* cb_data, bool sql_busy_retry)
{
    int rc;
    bool ret = false;
    char* errmsgs = 0;
    if (!db_handle){
        LOG_E(TAG, "db_handle passed is NULL");
        return ret;
    }

    pthread_mutex_lock(&db_handle_mutex);
    rc = sqlite3_exec(db_handle, command.c_str(), callback, cb_data, &errmsgs);
    pthread_mutex_unlock(&db_handle_mutex);
    switch (rc){
        case SQLITE_OK:
            if(errmsgs == NULL) {
                LOG_D(TAG, "Success in exec_cmd_db");
            } else {
                LOG_E(TAG, "Success in exec_cmd_db with SQL Error Code = %d, SQL error : %s, command : %s", rc, errmsgs, command.c_str());
                sqlite3_free(errmsgs);
            }
            ret = true;

            break;
        case SQLITE_CORRUPT:
            // db file is corrupted
            // delete file and start over
        case SQLITE_NOTADB:
            // file is not a sqlite3 db. in this situation the opening
            // and closing of db handle will succeed but no sql queries
            // will run
            // Delete file and start over
        case SQLITE_READONLY:
            // db file has become readonly. In this instance, insert/update
            // queries will fail. This can cause the sdcard to fill up to
            // 100%
            // delete file and start over
            LOG_C(TAG, "Database error. SQL Error Code = %d,  SQL error : %s", rc, errmsgs);
            handle_db_corruption(db_handle,rc);
            ret = false;
            sqlite3_free(errmsgs);
            LOG_C(TAG, "SQLITE_READONLY handle_db_corruption failed. Not expected to be here.");
            break;
        case SQLITE_BUSY:
            LOG_E(TAG, "Database is Busy. SQL Error Code = %d,  SQL error : %s", rc, errmsgs);
            ret = false;
            sqlite3_free(errmsgs);
            if(sql_busy_retry) {
                // make sure we retry only once for SQLITE_BUSY cases
                LOG_I(TAG, "Calling just one more time in recurssion");
                ret = exec_cmd_db(db_handle, command, callback , cb_data, false);
            }
            break;
        case SQLITE_LOCKED:
            LOG_E(TAG, "Database is locked, SQL Error Code = %d, SQL error : %s, command : %s", rc, errmsgs, command.c_str());
            sqlite3_free(errmsgs);
            break;

        default :
            // Any error code other than SQLITE_OK is an error
            LOG_E(TAG, "SQL error code : %d, SQL error: %s",rc, errmsgs);
            sqlite3_free(errmsgs);
            ret = false;
            break;
    }

    return ret;
}

bool circular_buffer_vid::open_db(string db_file, db_handle_t** db_handle)
{
   char *zErrMsg = 0;
   int rc;

   rc = sqlite3_open(db_file.c_str(), db_handle);
   if( rc ){
      LOG_E(TAG, "Can't open database: %s", sqlite3_errmsg(*db_handle));
      return false;
   }

   rc = sqlite3_busy_timeout((*db_handle), SQLITE3_BUSY_TIMEOUT); //setting timeout
   if (rc){
       LOG_E(TAG, "Unable to set timeout for db. Timeout is set to default value = 0");
   }else{
       LOG_I(TAG, "Set db timeout to %dms", SQLITE3_BUSY_TIMEOUT);
   }

   return true;
}

bool circular_buffer_vid::close_db(db_handle_t* db_handle)
{
    if(db_handle == NULL)
        return false;
    sqlite3_close(db_handle);
    LOG_I(TAG, "Closing DB");
    return true;
}

bool circular_buffer_vid::create_table_db(db_handle_t* db_handle)
{
    bool  rc;
    string sql;
    sql = "CREATE TABLE IF NOT EXISTS VIDFILES(" \
            "INDEXID INTEGER PRIMARY KEY  AUTOINCREMENT," \
            "TIME           INT     NOT NULL," \
            "NAME           TEXT    NOT NULL," \
            "DURATION       INT     NOT NULL," \
            "FILE_SIZE      INT     NOT NULL," \
            "TYPE           INT     NOT NULL," \
            "STATUS         INT             ," \
            "CAM_TYPE       INT     NOT NULL," \
            "TRANSCODE_STATUS INT   DEFAULT 0," \
            "FILE_COMPRESSION INT   DEFAULT 0);" ;


    rc = exec_cmd_db(db_handle, sql.c_str(), NULL, 0);
    return rc;
}

void create_db_details(string &db_identifier, int64_t &db_creation_time)
{
    uuid_t binuuid;
    uuid_generate_random(binuuid);
    char *uuid = (char *)malloc(37);
    uuid_unparse(binuuid, uuid);

    db_identifier = uuid;
    db_creation_time = get_system_time();
}

bool circular_buffer_vid::create_table_db_details(db_handle_t* db_handle)
{
    char *zErrMsg = 0;
    int  rc;
    string sql = "CREATE TABLE DB_DETAILS(" \
            "DB_IDENTIFIER  TEXT     NOT NULL," \
            "DB_CREATIONTIME  INT     NOT NULL);" ;

    rc = sqlite3_exec(db_handle, sql.c_str(), NULL, 0, &zErrMsg);

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

    string db_identifier = "";
    int64_t db_creation_time = -1;
    create_db_details(db_identifier, db_creation_time);
    LOG_I(TAG, "create_db_details: identifier %s, creation_time: %ld", db_identifier.c_str(), db_creation_time);

    stringstream ss;
    ss << "INSERT INTO DB_DETAILS (DB_IDENTIFIER, DB_CREATIONTIME) VALUES "
       << "('" << db_identifier << "', "
       << db_creation_time << ");" ;

    bool ret = exec_cmd_db(db_handle, ss.str().c_str(), NULL, 0);
    if(!ret) {
        LOG_E(TAG, "Failed to insert db details to table");
    }
    return ret;
}


