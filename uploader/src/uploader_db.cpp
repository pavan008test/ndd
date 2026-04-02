#include <log.h>
#include <sqlite3.h>
#include <string>
#include <sstream>
#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>
#include <config_parser.h>
#include "nd_msg_types.h"
#include "nd_file_utils.h"
#include <storage_utils.h>
#include <sdcard_utils.h>
#include <nd_factory.h>
#include "nd_ext_cam_utils.h"
#include <mutex>

using namespace std;
#define TAG "UDB"
#define MAX_LINE_LEN 256
extern ND_DeviceFactory *nd_device_obj;
extern NDService *nd_service_obj;
typedef sqlite3 *db_handle_t;
std::mutex dbMutex; // Mutex for synchronizing access from multiple threads
string db_file_name;
static const string DB_FOLDER = "/home/ubuntu/.nddevice/.uploader";

extern string ALERTS_PATH_DIR;
extern string ALERTS_PATH_DIR_PHYSMNT;
extern string MOUNT_SRC;

static bool get_device_id(string &device_id);
static bool clear_db (db_handle_t handle);
bool check_create_dir(string path);
string exec_cmd(const char* cmd);
void create_vod_table_if_not_exists(db_handle_t handle);
bool alter_db_add_columns_ver_1(db_handle_t handle);
bool update_db_ver_2(db_handle_t handle);
void create_ea_imgs_table_if_not_exists(db_handle_t handle);

static string get_db_fname()
{
    if (!db_file_name.empty())
        return db_file_name;
    else
        return "";
}

db_handle_t create_db (string filepath)
{
    char *sql = NULL;
    char *zErrMsg = NULL;
    bool exists = false;
    db_handle_t handle;
    db_file_name = filepath;

    if (access (filepath.c_str(), F_OK) == 0)
    {
        exists = true;
    }
    else
    {
        check_create_dir(DB_FOLDER);
    }

    int ret = sqlite3_open (filepath.c_str(), &handle);
    if (ret)
    {
        LOG_I(TAG,"Can't open db:%s",sqlite3_errmsg(handle));
//        sqlite3_close(handle);
        return NULL;
    }
    else {
        LOG_I(TAG,"opened db");
        const char * const command = "PRAGMA integrity_check;";
        ret = sqlite3_exec (handle, command, NULL, 0, &zErrMsg);
        if (ret != SQLITE_OK) {
            LOG_E(TAG,"DB integrity check failed: %s",zErrMsg);
            sqlite3_free(zErrMsg);
            sqlite3_close(handle);
            file_delete(filepath);
            ret = sqlite3_open (filepath.c_str(), &handle);

            if (ret != SQLITE_OK) {
                LOG_I(TAG,"Can't recreate db:%s",sqlite3_errmsg(handle));
                sqlite3_close(handle);
                return NULL;
            }
            else {
                exists = false;
                LOG_C(TAG,"Recreated db");
            }
        }
    }

    create_vod_table_if_not_exists(handle);

    //add columns if if the DB version is 0
    alter_db_add_columns_ver_1(handle);

    //Update ext video fetch path of existing records if the DB version is 1
    update_db_ver_2(handle);

    if (exists)
    {
        LOG_I (TAG,"DB already exists. Not creating table again");
        return handle;
    }

    sql = "CREATE TABLE UPLOADER(" \
            "INDEXID INTEGER PRIMARY KEY  AUTOINCREMENT," \
            "MSG_TYPE     INT     NOT NULL," \
            "LENGTH         INT     NOT NULL," \
            "CLIENT_ID      TEXT    NOT NULL," \
            "MSG_ID         INT     NOT NULL," \
            "RES_REQD       INT     NOT NULL," \
            "LEVEL          INT     NOT NULL," \
            "PAYLOAD_SIZE   INT     NOT NULL," \
            "F_NAME         TEXT    NOT NULL," \
            "JSON_NAME      TEXT    NOT NULL," \
            "REQUEST_ID     INT     NOT NULL," \
            "RETRY_COUNT    INT     NOT NULL," \
            "REQ_TIME       TEXT    NOT NULL," \
            "MD_SUM         TEXT    NOT NULL," \
            "STATUS         INT     NOT NULL);";
    ret = sqlite3_exec (handle, sql, NULL, 0, &zErrMsg);
    if (ret != SQLITE_OK)
    {
        LOG_E(TAG,"can't create db table: %s",zErrMsg);
        sqlite3_free(zErrMsg);
        sqlite3_close(handle);
        return NULL;
    }
    else
    {
        LOG_I (TAG,"db table created: UPLOADER");
    }
    return handle;
}

void create_vod_table_if_not_exists(db_handle_t handle){
    //If not exists, create table for VODs
    char *sql_err;
    char *sql_create_VOD_table = "CREATE TABLE IF NOT EXISTS UPLOADER_VOD(" \
            "INDEXID INTEGER PRIMARY KEY  AUTOINCREMENT," \
            "MSG_TYPE           INT     NOT NULL," \
            "LENGTH             INT     NOT NULL," \
            "CLIENT_ID          TEXT    NOT NULL," \
            "MSG_ID             INT     NOT NULL," \
            "LEVEL              INT     NOT NULL," \
            "PAYLOAD_SIZE       INT     NOT NULL," \
            "F_NAME             TEXT    NOT NULL," \
            "JSON_NAME          TEXT    NOT NULL," \
            "REQUEST_ID         INT     NOT NULL," \
            "RETRY_COUNT        INT     NOT NULL," \
            "REQ_TIME           TEXT    NOT NULL," \
            "TRIM               INT     NOT NULL," \
            "START_SEC          INT     NOT NULL," \
            "END_SEC            INT     NOT NULL," \
            "PART_ID            INT     NOT NULL," \
            "UPLOAD_OBS         INT     NOT NULL," \
            "UPLOAD_AUD         INT     NOT NULL," \
            "QUALITY            INT     NOT NULL," \
            "ALERT_ID           INT     NOT NULL," \
            "IS_EXT_VIDEO       INT     NOT NULL," \
            "FETCHED_EXT_VIDEO  INT     DEFAULT 0," \
            "VOD_FAILURE        INT     DEFAULT 0," \
            "ELAPSED_TIME_MIN   INT     DEFAULT 0);";

    int ret = sqlite3_exec(handle, sql_create_VOD_table, NULL, 0, &sql_err);
    if (ret != SQLITE_OK)
    {
        LOG_E(TAG,"can't create db table UPLOADER_VOD: %s",sql_err);
        sqlite3_free(sql_err);
        sqlite3_close(handle);
    }
    else
    {
        LOG_I (TAG,"db table created: UPLOADER_VOD");
    }
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

bool alter_db_add_column(db_handle_t handle, const string& column, const string& defaultValue)
{
    string alter_query = "ALTER TABLE UPLOADER_VOD ADD COLUMN " + column + " " + defaultValue + ";";
    char* zErrMsg = nullptr;
    int rc = sqlite3_exec(handle, alter_query.c_str(), nullptr, 0, &zErrMsg);
    if (rc != SQLITE_OK)
    {
        LOG_E(TAG, "Can't alter db table UPLOADER_VOD: %s",  zErrMsg);
        sqlite3_free(zErrMsg);
        return false;
    }
    return true;
}

bool alter_db_add_columns_ver_1(db_handle_t handle)
{
    char *zErrMsg = NULL;
    int rc = 0;
    bool status = false;

    int currentVersion = -1;
    rc = sqlite3_exec(handle, "PRAGMA user_version;", db_version_cb, &currentVersion, &zErrMsg);
    if (rc != SQLITE_OK)
    {
        LOG_E(TAG, "Failed to get user_version: %s", sqlite3_errmsg(handle));
        sqlite3_free(zErrMsg);
        return status;
    }
    LOG_I(TAG, "alter_db_add_columns_ver_1 :: Current DB version: %d", currentVersion);

    if (currentVersion == 0)
    {
        // Begin a transaction
        rc = sqlite3_exec(handle, "BEGIN TRANSACTION;", NULL, 0, &zErrMsg);
        if (rc != SQLITE_OK)
        {
            LOG_E(TAG, "Failed to begin transaction: %s", zErrMsg);
            sqlite3_free(zErrMsg);
            return status;
        }

        // The below operations are atomic. If any of the operation fails, the transaction will be rolled back.
        do
        {
            status = alter_db_add_column(handle, "REQ_PRIORITY", "INT DEFAULT 8");
            if (!status) break;

            status = alter_db_add_column(handle, "VOD_ID", "TEXT DEFAULT 'unknown'");
            if (!status) break;

            status = alter_db_add_column(handle, "CANCELLED", "INT DEFAULT 0");
            if (!status) break;

            status = alter_db_add_column(handle, "NOT_AVAILABLE", "INT DEFAULT 0");
            if (!status) break;

            status = alter_db_add_column(handle, "FAILURE_REASON", "TEXT DEFAULT 'NA'");
            if (!status) break;

            status = alter_db_add_column(handle, "RGB_STATUS", "TEXT DEFAULT 'not-applicable'");
            if (!status) break;

            status = alter_db_add_column(handle, "EXT_VOD_REQ_STATUS", "INT DEFAULT 0");
            if (!status) break;

            rc = sqlite3_exec(handle, "PRAGMA user_version = 1;", nullptr, 0, nullptr);

            if (rc != SQLITE_OK) {
                LOG_E(TAG, "Failed to set user_version: %s", sqlite3_errmsg(handle));
                status = false;
                break;
            }
            LOG_I(TAG, "db table altered: UPLOADER_VOD");

            status = true;
        } while (false);

        if (status)
        {
            // Commit the transaction
            rc = sqlite3_exec(handle, "COMMIT TRANSACTION;", NULL, 0, &zErrMsg);
            if (rc != SQLITE_OK)
            {
                LOG_E(TAG, "Failed to commit transaction: %s", zErrMsg);
                status = false;
                sqlite3_free(zErrMsg);
            }
        }
        if (!status)
        {
            // Rollback the transaction
            LOG_E(TAG, "Rollback:: Failed to alter uploader DB V1");
            rc = sqlite3_exec(handle, "ROLLBACK TRANSACTION;", NULL, 0, &zErrMsg);
            if (rc != SQLITE_OK)
            {
                LOG_E(TAG, "Failed to rollback transaction: %s", zErrMsg);
                sqlite3_free(zErrMsg);
            }
            string str_msg = "Failed to alter uploader DB V1";
            nd_service_obj->send_err_msg(SM_E_UPLD_ALTER_DB_V1_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
        }
    }
    return status;
}

/**
 * @brief Function to modify the UPLOADER_VOD table to change the
 * ext cam fetch path of existing records (if any) from "/home/iriscli/saveMP4/" to CB path
 */
bool update_db_ver_2(db_handle_t handle)
{
    char *zErrMsg = NULL;
    int rc = 0;
    bool status = false;

    int currentVersion = -1;
    rc = sqlite3_exec(handle, "PRAGMA user_version;", db_version_cb, &currentVersion, &zErrMsg);
    if (rc != SQLITE_OK)
    {
        LOG_E(TAG, "Failed to get user_version: %s", sqlite3_errmsg(handle));
        sqlite3_free(zErrMsg);
        return status;
    }
    LOG_I(TAG, "update_db_ver_2 :: Current DB version: %d", currentVersion);

    if (currentVersion == 1)
    {
        // Begin a transaction
        rc = sqlite3_exec(handle, "BEGIN TRANSACTION;", NULL, 0, &zErrMsg);
        if (rc != SQLITE_OK)
        {
            LOG_E(TAG, "Failed to begin transaction: %s", zErrMsg);
            sqlite3_free(zErrMsg);
            return status;
        }

        // The below operations are atomic. If any of the operation fails, the transaction will be rolled back.
        do
        {
            rc = sqlite3_exec(handle,
                    "UPDATE UPLOADER_VOD SET JSON_NAME = F_NAME WHERE IS_EXT_VIDEO = 1;",
                    nullptr, 0, nullptr);
            if (rc != SQLITE_OK) {
                LOG_E(TAG, "Failed to update ext video fetch path: %s", sqlite3_errmsg(handle));
                status = false;
                break;
            }

            rc = sqlite3_exec(handle, "PRAGMA user_version = 2;", nullptr, 0, nullptr);
            if (rc != SQLITE_OK) {
                LOG_E(TAG, "Failed to set user_version: %s", sqlite3_errmsg(handle));
                status = false;
                break;
            }

            LOG_I(TAG, "DB version 2: Ext VOD fetch path updated for existing records (if any)");
            status = true;
        } while (false);

        if (status)
        {
            // Commit the transaction
            rc = sqlite3_exec(handle, "COMMIT TRANSACTION;", NULL, 0, &zErrMsg);
            if (rc != SQLITE_OK)
            {
                LOG_E(TAG, "Failed to commit transaction: %s", zErrMsg);
                status = false;
                sqlite3_free(zErrMsg);
            }
        }
        if (!status)
        {
            // Rollback the transaction
            LOG_E(TAG, "Rollback:: Failed to alter uploader DB V2");
            rc = sqlite3_exec(handle, "ROLLBACK TRANSACTION;", NULL, 0, &zErrMsg);
            if (rc != SQLITE_OK)
            {
                LOG_E(TAG, "Failed to rollback transaction: %s", zErrMsg);
                sqlite3_free(zErrMsg);
            }
            string str_msg = "Failed to update uploader DB V2";
            nd_service_obj->send_err_msg(SM_E_UPLD_ALTER_DB_V2_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
        }
    }
    return status;
}

bool insert_db (db_handle_t handle, req_upload_msg_t *upload_req, int status, string md5sum)
{
    string insert_str = "INSERT INTO UPLOADER (MSG_TYPE, LENGTH, CLIENT_ID, MSG_ID, RES_REQD, LEVEL, PAYLOAD_SIZE, F_NAME, JSON_NAME, REQUEST_ID, RETRY_COUNT, REQ_TIME, MD_SUM, STATUS) "
                        "VALUES ";

    int ret = 0;
    char *zErrMsg = NULL;

    stringstream ss;
    ss  << insert_str
        << "(" << "'" << upload_req->msg_type << "'" <<", "
        << "'" << upload_req->length << "'" << ", "
        << "'" << upload_req->client_id << "'" << ", "
        << "'" << upload_req->msg_idx << "'" << ", "
        << "'" << upload_req->res_reqd << "'" << ", "
        << "'" << upload_req->level << "'" << ", "
        << "'" << upload_req->payload_size << "'" << ", "
        << "'" << upload_req->fname << "'" << ", "
        << "'" << upload_req->json_fname << "'" << ", "
        << "'" << upload_req->req_id << "'" << ", "
        << "'" << upload_req->retry_count << "'" << ", "
        << "'" << upload_req->request_time << "'" << ", "
        << "'" << md5sum << "'" << ", "
        << "'" << status << "'" <<");";

    string cmd = ss.str();
    stringstream ss1;
    ss1  << insert_str
        << "(" << "'" << upload_req->msg_type << "'" <<", "
        << "'" << upload_req->request_time << "'" << ", "
        << "'" << md5sum << "'" << ", "
        << "'" << status << "'" <<");";

    LOG_I (TAG,"INSERT: %s",ss1.str().c_str());
    {
        std::lock_guard<std::mutex> lock(dbMutex);
        ret = sqlite3_exec (handle, cmd.c_str(), NULL,0,&zErrMsg);
    }
    if (ret != SQLITE_OK)
    {
        LOG_E(TAG,"can't insert upload req %s to table: %s",upload_req->fname, zErrMsg);
        sqlite3_free(zErrMsg);
        return false;
    }
    else
    {
        LOG_I (TAG,"Inserted upload req %s, time %llu to table",upload_req->fname, upload_req->request_time);
    }
    return true;
}

bool insert_db_vod (db_handle_t handle, req_upload_msg_t *upload_req, bool is_ext_video)
{
    std::lock_guard<std::mutex> lock(dbMutex);

    LOG_I (TAG,"Inserting VOD to DB: req_id  %llu , file %s",upload_req->req_id, upload_req->fname);

    string insert_str = "INSERT INTO UPLOADER_VOD (MSG_TYPE, LENGTH, CLIENT_ID, MSG_ID, LEVEL, PAYLOAD_SIZE, F_NAME, JSON_NAME, REQUEST_ID, RETRY_COUNT, REQ_TIME, TRIM, START_SEC, END_SEC, PART_ID, UPLOAD_OBS, UPLOAD_AUD, QUALITY, ALERT_ID, IS_EXT_VIDEO, REQ_PRIORITY, VOD_ID) "
                        "SELECT ";
    int ret = 0;
    char *zErrMsg = NULL;

    stringstream ss;
    ss  << insert_str
        << "'" << upload_req->msg_type << "'" <<", "
        << "'" << upload_req->length << "'" << ", "
        << "'" << upload_req->client_id << "'" << ", "
        << "'" << upload_req->msg_idx << "'" << ", "
        << "'" << upload_req->level << "'" << ", " //effective priority
        << "'" << upload_req->payload_size << "'" << ", "
        << "'" << upload_req->fname << "'" << ", "
        << "'" << upload_req->json_fname << "'" << ", "
        << "'" << upload_req->req_id << "'" << ", "
        << "'" << upload_req->retry_count << "'" << ", "
        << "'" << upload_req->request_time << "'" << ", "
        << "'" << upload_req->trim << "'" << ", "
        << "'" << upload_req->start_sec << "'" << ", "
        << "'" << upload_req->end_sec << "'" << ", "
        << "'" << upload_req->part_id << "'" << ", "
        << "'" << upload_req->upload_observation << "'" << ", "
        << "'" << upload_req->upload_audio << "'" << ", "
        << "'" << upload_req->quality << "'" << ", "
        << "'" << upload_req->alert_id << "'" << ", "
        << "'" << is_ext_video << "'" << ", "

        << "'" << upload_req->level << "'" << ", " //request priority
        << "'" << upload_req->vod_id << "'"
        << " WHERE NOT EXISTS (SELECT 1 FROM UPLOADER_VOD WHERE F_NAME = "
        << "'" << upload_req->fname << "'" << " AND VOD_ID = "
        << "'" << upload_req->vod_id << "'" << " AND REQ_PRIORITY = "
         << upload_req->req_priority << ");";


    string cmd = ss.str();
    ret = sqlite3_exec (handle, cmd.c_str(), NULL,0,&zErrMsg);
    if (ret != SQLITE_OK) {
        LOG_E(TAG,"can't insert upload VOD req %s to table: %s",upload_req->vod_id, zErrMsg);
        sqlite3_free(zErrMsg);
        return false;
    }
    LOG_I (TAG,"Inserted upload VOD req %s, time %llu to table",upload_req->vod_id, upload_req->request_time);
    return true;
}

bool update_vod_elapsed_time (db_handle_t handle, int elapsed_min) {
    std::lock_guard<std::mutex> lock(dbMutex);

    stringstream update_str;
    update_str << "UPDATE UPLOADER_VOD SET ELAPSED_TIME_MIN = ELAPSED_TIME_MIN + " << elapsed_min ;
    string update_cmd = update_str.str();
    int ret = 0;
    char *zErrMsg = NULL;
    LOG_I (TAG,"Executing command: %s",update_cmd.c_str());
    ret = sqlite3_exec (handle, update_cmd.c_str(), NULL,0,&zErrMsg);
    if (ret != SQLITE_OK)
    {
        LOG_E(TAG,"update_vod_elapsed_time :: Couldn't update DB!");
        sqlite3_free(zErrMsg);
        return false;
    }
    LOG_I(TAG,"update_vod_elapsed_time :: incremented by %d min",elapsed_min);
    return true;
}

bool is_vod_cancelled(db_handle_t handle, req_upload_msg_t *upload_req) {
    std::lock_guard<std::mutex> lock(dbMutex);

    sqlite3_stmt *stmt;
    stringstream select_ss;
    select_ss << "SELECT COUNT(*) FROM UPLOADER_VOD WHERE REQ_PRIORITY = " << upload_req->req_priority
            << " AND VOD_ID = " << "'" << upload_req->vod_id
            << "'" << " AND F_NAME = " << "'" <<  upload_req->fname << "'"
            << " AND REQ_PRIORITY = " << upload_req->req_priority
            << " AND CANCELLED = " << true;
    string sel_stmt = select_ss.str();
    int ret = 0;
    ret = sqlite3_prepare_v2(handle, sel_stmt.c_str(), -1, &stmt, NULL);
    if (ret != SQLITE_OK) {
        LOG_E(TAG,"Failed to prepare statement: %s",sqlite3_errmsg(handle));
        sqlite3_finalize(stmt);
        return false;
    }
    ret = sqlite3_step(stmt);
    if (ret != SQLITE_ROW) {
        LOG_E(TAG,"Failed to check if VOD was cancelled: %s",sqlite3_errmsg(handle));
        sqlite3_finalize(stmt);
        return false;
    }
    int count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return (count != 0);
}

bool mark_vod_cancelled (db_handle_t handle, req_upload_msg_t *upload_req) {
    std::lock_guard<std::mutex> lock(dbMutex);

    stringstream update_str;
    update_str << "UPDATE UPLOADER_VOD SET CANCELLED = " << true
            << " WHERE VOD_ID = " << "'" <<  upload_req->vod_id << "'"
            << " AND F_NAME = " << "'" <<  upload_req->fname << "'"
            << " AND REQUEST_ID = " << upload_req->req_id;
    string update_cmd = update_str.str();
    int ret = 0;
    char *zErrMsg = NULL;
    LOG_I (TAG,"Executing command: %s",update_cmd.c_str());
    ret = sqlite3_exec (handle, update_cmd.c_str(), NULL,0,&zErrMsg);
    if (ret != SQLITE_OK)
    {
        LOG_E(TAG,"Couldn't mark vod as cancelled : %s", upload_req->vod_id, zErrMsg);
        sqlite3_free(zErrMsg);
        return false;
    }
    return true;
}

bool update_vod_priority (db_handle_t handle, req_upload_msg_t *upload_req) {
    std::lock_guard<std::mutex> lock(dbMutex);

    stringstream update_str;
    update_str << "UPDATE UPLOADER_VOD SET LEVEL = " << upload_req->level
            << " WHERE VOD_ID = " << "'" <<  upload_req->vod_id << "'"
            << " AND F_NAME = " << "'" <<  upload_req->fname << "'";
    string update_cmd = update_str.str();
    int ret = 0;
    char *zErrMsg = NULL;
    LOG_I (TAG,"Executing command: %s",update_cmd.c_str());
    ret = sqlite3_exec (handle, update_cmd.c_str(), NULL,0,&zErrMsg);
    if (ret != SQLITE_OK)
    {
        LOG_E(TAG,"Couldn't update DB with vod priority %d for  %s : %s",upload_req->level, upload_req->vod_id, zErrMsg);
        sqlite3_free(zErrMsg);
        return false;
    }
    LOG_I(TAG,"Updated DB with vod priority %d for %s",upload_req->level, upload_req->vod_id);
    return true;
}

bool update_fetch_ext_video_status (db_handle_t handle, req_upload_msg_t *upload_req, bool fetched_ext_video, string new_fname) {
    std::lock_guard<std::mutex> lock(dbMutex);
    stringstream update_str;
    update_str << "UPDATE UPLOADER_VOD SET FETCHED_EXT_VIDEO = " << fetched_ext_video << ", JSON_NAME = " << "'" << new_fname.c_str() << "'"
            << " WHERE F_NAME = " << "'" <<  upload_req->fname << "'"
            << " AND VOD_ID = " << "'" <<  upload_req->vod_id << "'";
    string update_cmd = update_str.str();
    int ret = 0;
    char *zErrMsg = NULL;
    LOG_I (TAG,"Executing command: %s",update_cmd.c_str());
    ret = sqlite3_exec (handle, update_cmd.c_str(), NULL,0,&zErrMsg);
    if (ret != SQLITE_OK)
    {
        LOG_E(TAG,"Couldn't update DB with fetched_ext_video %d for  %s : %s",fetched_ext_video, upload_req->vod_id, zErrMsg);
        sqlite3_free(zErrMsg);
        return false;
    }
    LOG_I(TAG,"Updated DB with fetched_ext_video %d for  %s",fetched_ext_video, upload_req->vod_id);
    return true;
}

//get failure_reason
string get_vod_failure_reason (db_handle_t handle, req_upload_msg_t *upload_req) {
    std::lock_guard<std::mutex> lock(dbMutex);

    sqlite3_stmt *stmt;
    stringstream select_ss;
    select_ss << "SELECT FAILURE_REASON FROM UPLOADER_VOD WHERE F_NAME = "
            << "'" << upload_req->fname << "'" << " AND VOD_ID = "
            << "'" << upload_req->vod_id << "'"
            << " AND CANCELLED = 0";
    string sel_stmt = select_ss.str();
    int ret = 0;
    ret = sqlite3_prepare_v2(handle, sel_stmt.c_str(), -1, &stmt, NULL);
    if (ret != SQLITE_OK) {
        LOG_E(TAG,"Failed to prepare statement: %s",sqlite3_errmsg(handle));
        sqlite3_finalize(stmt);
        return "NA";
    }
    ret = sqlite3_step(stmt);
    if (ret != SQLITE_ROW) {
        LOG_E(TAG,"Failed to get failure reason: %s",sqlite3_errmsg(handle));
        sqlite3_finalize(stmt);
        return "NA";
    }
    string failure_reason = string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
    sqlite3_finalize(stmt);
    return failure_reason;
}

bool update_vod_failure_reason (db_handle_t handle, req_upload_msg_t *upload_req, string failure_reason) {
    std::lock_guard<std::mutex> lock(dbMutex);

    stringstream update_str;
    update_str << "UPDATE UPLOADER_VOD SET FAILURE_REASON = " << "'" << failure_reason << "'"
            << " WHERE F_NAME = " << "'" <<  upload_req->fname << "'"
            << " AND VOD_ID = " << "'" <<  upload_req->vod_id << "'";
    string update_cmd = update_str.str();
    int ret = 0;
    char *zErrMsg = NULL;
    LOG_I (TAG,"Executing command: %s",update_cmd.c_str());
    ret = sqlite3_exec (handle, update_cmd.c_str(), NULL,0,&zErrMsg);
    if (ret != SQLITE_OK)
    {
        LOG_E(TAG,"Couldn't update DB with failure reason: %s . File %s - %s : %s", failure_reason.c_str(), upload_req->fname, upload_req->vod_id, zErrMsg);
        sqlite3_free(zErrMsg);
        return false;
    }
    LOG_I(TAG,"Updated DB with failure reason: %s . File %s - %s", failure_reason.c_str(), upload_req->fname, upload_req->vod_id);
    return true;
}

bool update_vod_retry_count (db_handle_t handle, req_upload_msg_t *upload_req) {
    std::lock_guard<std::mutex> lock(dbMutex);

    stringstream update_str;
    update_str << "UPDATE UPLOADER_VOD SET RETRY_COUNT = " << upload_req->retry_count
            << " WHERE F_NAME = " << "'" <<  upload_req->fname << "'"
            << " AND VOD_ID = " << "'" <<  upload_req->vod_id << "'"
            << " AND CANCELLED = 0";
    string update_cmd = update_str.str();
    int ret = 0;
    char *zErrMsg = NULL;
    LOG_I (TAG,"Executing command: %s",update_cmd.c_str());
    ret = sqlite3_exec (handle, update_cmd.c_str(), NULL,0,&zErrMsg);
    if (ret != SQLITE_OK)
    {
        LOG_E(TAG,"Couldn't update DB with retry count %d for file %s - %s : %s", upload_req->retry_count, upload_req->fname, upload_req->vod_id, zErrMsg);
        sqlite3_free(zErrMsg);
        return false;
    }
    LOG_I(TAG,"Updated DB with retry count %d for file %s - %s", upload_req->retry_count, upload_req->fname, upload_req->vod_id);
    return true;
}

bool update_vod_failure_details(db_handle_t handle,
        req_upload_msg_t *upload_req, bool vod_failure,
        string failure_reason, bool is_unavailable) {

    std::lock_guard<std::mutex> lock(dbMutex);

    stringstream update_str;
    update_str << "UPDATE UPLOADER_VOD SET VOD_FAILURE = " << vod_failure
            << ", FAILURE_REASON = " << "'" << failure_reason << "'"
            << ", NOT_AVAILABLE = " << is_unavailable
            << " WHERE F_NAME = " << "'" <<  upload_req->fname
            << "'" << " AND VOD_ID = " << "'" <<  upload_req->vod_id << "'"
            << " AND CANCELLED = 0";
    string update_cmd = update_str.str();
    int ret = 0;
    char *zErrMsg = NULL;
    LOG_I (TAG,"Executing command: %s",update_cmd.c_str());
    ret = sqlite3_exec (handle, update_cmd.c_str(), NULL,0,&zErrMsg);
    if (ret != SQLITE_OK)
    {
        LOG_E(TAG,"Couldn't update DB with vod_failure %d for file %s - %s : %s",vod_failure, upload_req->fname, upload_req->vod_id, zErrMsg);
        sqlite3_free(zErrMsg);
        return false;
    }
    LOG_I(TAG,"Updated DB with vod_failure %d for file %s - %s",vod_failure, upload_req->fname, upload_req->vod_id);
    return true;
}


bool update_vod_failure_status (db_handle_t handle, req_upload_msg_t *upload_req, bool vod_failure) {
    std::lock_guard<std::mutex> lock(dbMutex);

    stringstream update_str;
    update_str << "UPDATE UPLOADER_VOD SET VOD_FAILURE = " << vod_failure
            << " WHERE F_NAME = " << "'" <<  upload_req->fname
            << "'" << " AND VOD_ID = " << "'" <<  upload_req->vod_id
            << "'" << " AND CANCELLED = 0";
    string update_cmd = update_str.str();
    int ret = 0;
    char *zErrMsg = NULL;
    LOG_I (TAG,"Executing command: %s",update_cmd.c_str());
    ret = sqlite3_exec (handle, update_cmd.c_str(), NULL,0,&zErrMsg);
    if (ret != SQLITE_OK)
    {
        LOG_E(TAG,"Couldn't update DB with vod_failure %d for file %s - %s : %s",vod_failure, upload_req->fname, upload_req->vod_id, zErrMsg);
        sqlite3_free(zErrMsg);
        return false;
    }
    LOG_I(TAG,"Updated DB with vod_failure %d for file %s - %s",vod_failure, upload_req->fname, upload_req->vod_id);
    return true;
}

bool update_upload_status (db_handle_t handle, req_upload_msg_t *upload_req, int status)
{
    std::lock_guard<std::mutex> lock(dbMutex);

    stringstream update_str;
    update_str << "UPDATE UPLOADER SET STATUS = " << status << " WHERE REQ_TIME = "<< upload_req->request_time;
    string update_cmd = update_str.str();
    int ret = 0;
    char *zErrMsg = NULL;
    LOG_I (TAG,"Executing command: %s",update_cmd.c_str());
    ret = sqlite3_exec (handle, update_cmd.c_str(), NULL,0,&zErrMsg);
    if (ret != SQLITE_OK)
    {
        LOG_E(TAG,"can't update status %d upload status %s to table: %s",status, upload_req->fname, zErrMsg);
        sqlite3_free(zErrMsg);
        return false;
    }
    else
    {
        LOG_I (TAG,"Updated status %d  to upload req %s to table",status, upload_req->json_fname);
    }
    return true;
}

bool get_vod_rank (db_handle_t db_handle , req_upload_msg_t * upload_req, int &rank) {
    bool ret = false;
    sqlite3_stmt *stmt;
    do {
        std::lock_guard<std::mutex> lock(dbMutex);
        stringstream query_cmd;
        query_cmd << "SELECT (SELECT count(*)  FROM UPLOADER_VOD b WHERE b.LEVEL < a.LEVEL OR (b.LEVEL = a.LEVEL AND b.REQ_TIME <= a.REQ_TIME)) AS RANK FROM UPLOADER_VOD a WHERE CANCELLED = 0 AND VOD_ID = ";
        query_cmd << "'" << upload_req->vod_id << "' ORDER BY LEVEL ASC, REQ_TIME ASC;";
        string cmd = query_cmd.str();
        int rc = sqlite3_prepare_v2(db_handle, cmd.c_str(), -1, &stmt, NULL);
        if (rc != SQLITE_OK) {
            LOG_E(TAG,"Failed to prepare statement: %s",sqlite3_errmsg(db_handle));
            break;
        }
        rc = sqlite3_step(stmt);
        if (rc != SQLITE_ROW) {
            LOG_E(TAG,"Failed to get rank: %s",sqlite3_errmsg(db_handle));
            break;
        }
        rank = sqlite3_column_int(stmt, 0);
        ret = true;
    } while (false);
    if(stmt){
        sqlite3_finalize(stmt);
    }
    return ret;
}

void remove_alerts_from_disk(string fname){
    string summary_path = "";
    int r_index_sep = fname.rfind("/")+1;
    string alert_folder_name = fname.substr(r_index_sep,fname.length()-4 - r_index_sep);
    if(fname.find("zip") == string::npos){
        summary_path = ALERTS_PATH_DIR + "/" + alert_folder_name + "_summary.json";
    }else{
        summary_path = ALERTS_PATH_DIR + "/" + alert_folder_name + "_summary.json.zip";
    }

    if( access( summary_path.c_str(), F_OK ) == 0 ){
        remove(summary_path.c_str());
        LOG_I (TAG,"removed file %s from disk",summary_path.c_str());
    }
}

bool delete_upload_request (db_handle_t handle, req_upload_msg_t *upload_req)
{
    stringstream update_str;
    update_str << "DELETE FROM UPLOADER WHERE REQ_TIME = "<< upload_req->request_time;
    string update_cmd = update_str.str();
    int ret = 0;
    char *zErrMsg = NULL;
    {
        std::lock_guard<std::mutex> lock(dbMutex);
        ret = sqlite3_exec (handle, update_cmd.c_str(), NULL,0,&zErrMsg);

    }
    if (ret != SQLITE_OK)
    {
        LOG_E(TAG,"can't delete upload req %s from table: %s",upload_req->json_fname, zErrMsg);
        sqlite3_free(zErrMsg);
        return false;
    }
    else
    {
        LOG_I (TAG,"deleted upload req %s from table",upload_req->json_fname);
        remove_alerts_from_disk(upload_req->fname);
    }
    return true;
}

bool delete_cancelled_vod_requests (db_handle_t handle) {
    std::lock_guard<std::mutex> lock(dbMutex);

    stringstream update_str;
    update_str << "DELETE FROM UPLOADER_VOD WHERE CANCELLED = " << true;
    string update_cmd = update_str.str();
    int ret = 0;
    char *zErrMsg = NULL;
    ret = sqlite3_exec (handle, update_cmd.c_str(), NULL,0,&zErrMsg);
    if (ret != SQLITE_OK)
    {
        LOG_E(TAG,"can't delete cancelled VOD upload requests: %s", zErrMsg);
        sqlite3_free(zErrMsg);
        return false;
    }
    LOG_I (TAG,"deleted cancelled VOD upload requests (if any).");
    return true;
}

bool delete_vod_upload_request (db_handle_t handle, req_upload_msg_t *upload_req) {
    std::lock_guard<std::mutex> lock(dbMutex);

    stringstream update_str;
    update_str << "DELETE FROM UPLOADER_VOD WHERE F_NAME = " << "'" << upload_req->fname << "'"
            << " AND VOD_ID = " << "'" << upload_req->vod_id << "'"
            << " AND MSG_TYPE = " << upload_req->msg_type;
    string update_cmd = update_str.str();
    int ret = 0;
    char *zErrMsg = NULL;
    ret = sqlite3_exec (handle, update_cmd.c_str(), NULL,0,&zErrMsg);
    if (ret != SQLITE_OK)
    {
        LOG_E(TAG,"can't delete VOD upload req %s - %s from DB: %s",upload_req->fname, upload_req->vod_id, zErrMsg);
        sqlite3_free(zErrMsg);
        return false;
    }
    LOG_I (TAG,"deleted VOD upload req %s - %s from DB. msg_type = %d",upload_req->fname, upload_req->vod_id, upload_req->msg_type);
    return true;
}

/**
 * @brief Get the pending VOD upload count excluding the ones that failed
*/
int get_pending_vod_count(db_handle_t handle) {
    std::lock_guard<std::mutex> lock(dbMutex);

    sqlite3_stmt *stmt;
    stringstream select_ss;
    select_ss << "SELECT COUNT(*) FROM UPLOADER_VOD WHERE VOD_FAILURE = " << false << " AND CANCELLED = 0";
    string sel_stmt = select_ss.str();
    int ret = 0;
    ret = sqlite3_prepare_v2(handle, sel_stmt.c_str(), -1, &stmt, NULL);
    if (ret != SQLITE_OK) {
        LOG_E(TAG,"Failed to prepare statement: %s",sqlite3_errmsg(handle));
        sqlite3_finalize(stmt);
        return 0;
    }
    ret = sqlite3_step(stmt);
    if (ret != SQLITE_ROW) {
        LOG_E(TAG,"Failed to get count: %s",sqlite3_errmsg(handle));
        sqlite3_finalize(stmt);
        return 0;
    }
    int count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    LOG_I(TAG,"get_pending_vod_count: %d",count);
    return count;
}

/**
 * @brief Get the pending ext cam VOD which needs to be fetched from MDVR count excluding the ones that failed and check if only ext_cam videos need to be uploaded and mdvr is offline.
*/
bool shutdown_check_for_dhub_offline_and_pending_ext_cam_vods(db_handle_t handle, int pend_vod_count) {
    std::lock_guard<std::mutex> lock(dbMutex);

    sqlite3_stmt *stmt;
    stringstream select_ss;
    select_ss << "SELECT COUNT(*) FROM UPLOADER_VOD WHERE VOD_FAILURE = " << false
            << " AND IS_EXT_VIDEO = " << true
            << " AND FETCHED_EXT_VIDEO = " << false
            << " AND CANCELLED = 0";
    string sel_stmt = select_ss.str();
    int ret = 0;
    ret = sqlite3_prepare_v2(handle, sel_stmt.c_str(), -1, &stmt, NULL);
    if (ret != SQLITE_OK) {
        LOG_E(TAG,"Failed to prepare statement: %s",sqlite3_errmsg(handle));
        sqlite3_finalize(stmt);
        return 0;
    }
    ret = sqlite3_step(stmt);
    if (ret != SQLITE_ROW) {
        LOG_E(TAG,"Failed to get count: %s",sqlite3_errmsg(handle));
        sqlite3_finalize(stmt);
        return 0;
    }
    int pend_ext_cam_vod_count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    LOG_I(TAG,"get_pending_ext_cam_vod_count: %d",pend_ext_cam_vod_count);

    if(!isMDVRConnected() && (pend_vod_count != 0 && (pend_vod_count == pend_ext_cam_vod_count)))
    {
        LOG_I(TAG,"Only Ext Cam Request Are Pending And MDVR Is Offline So Not Delaying Shutdown For VOD Upload");
        return true;
    }

    return false;

 }

int get_vod_elapsed_time (db_handle_t handle, req_upload_msg_t *upload_req) {
    std::lock_guard<std::mutex> lock(dbMutex);

    sqlite3_stmt *stmt;
    stringstream select_ss;
    select_ss << "SELECT ELAPSED_TIME_MIN FROM UPLOADER_VOD WHERE F_NAME = " << "'" << upload_req->fname << "'"
            << " AND VOD_ID = " << "'" << upload_req->vod_id << "'"
            << " AND CANCELLED = 0";
    string sel_stmt = select_ss.str();
    int ret = 0;
    ret = sqlite3_prepare_v2(handle, sel_stmt.c_str(), -1, &stmt, NULL);
    if (ret != SQLITE_OK) {
        LOG_E(TAG,"Failed to prepare statement: %s",sqlite3_errmsg(handle));
        sqlite3_finalize(stmt);
        return 0;
    }
    ret = sqlite3_step(stmt);
    if (ret != SQLITE_ROW) {
        LOG_E(TAG,"Failed to get ext_vod_path: %s",sqlite3_errmsg(handle));
        sqlite3_finalize(stmt);
        return 0;
    }
    int elapsed_min = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return elapsed_min;
}
/**
 * @brief Get the total pending VOD count
*/
int get_total_vod_req_count(db_handle_t handle) {
    std::lock_guard<std::mutex> lock(dbMutex);

    sqlite3_stmt *stmt;
    stringstream select_ss;
    select_ss << "SELECT COUNT(*) FROM UPLOADER_VOD WHERE CANCELLED = 0";
    string sel_stmt = select_ss.str();
    int ret = 0;
    ret = sqlite3_prepare_v2(handle, sel_stmt.c_str(), -1, &stmt, NULL);
    if (ret != SQLITE_OK) {
        LOG_E(TAG,"total_vod_count: Failed to prepare statement: %s",sqlite3_errmsg(handle));
        sqlite3_finalize(stmt);
        return 0;
    }
    ret = sqlite3_step(stmt);
    if (ret != SQLITE_ROW) {
        LOG_E(TAG,"Failed to get total_vod_count: %s",sqlite3_errmsg(handle));
        sqlite3_finalize(stmt);
        return 0;
    }
    int count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    LOG_I(TAG,"total_vod_count: %d",count);
    return (count);
}


bool check_ext_vod_req_present(db_handle_t handle) {
    std::lock_guard<std::mutex> lock(dbMutex);

    sqlite3_stmt *stmt;
    stringstream select_ss;
    select_ss << "SELECT COUNT(*) FROM UPLOADER_VOD WHERE IS_EXT_VIDEO = " << true;
    string sel_stmt = select_ss.str();
    int ret = 0;
    ret = sqlite3_prepare_v2(handle, sel_stmt.c_str(), -1, &stmt, NULL);
    if (ret != SQLITE_OK) {
        LOG_E(TAG,"check_ext_vod_req_present: Failed to prepare statement: %s",sqlite3_errmsg(handle));
        sqlite3_finalize(stmt);
        return false;
    }
    ret = sqlite3_step(stmt);
    if (ret != SQLITE_ROW) {
        LOG_E(TAG,"Failed to get total_vod_count: %s",sqlite3_errmsg(handle));
        sqlite3_finalize(stmt);
        return false;
    }
    int count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    LOG_I(TAG,"check_ext_vod_req_present : %d",count);
    return (count != 0);
}

bool is_vod_priority_changed(db_handle_t handle, req_upload_msg_t *upload_req) {
    std::lock_guard<std::mutex> lock(dbMutex);

    sqlite3_stmt *stmt;
    stringstream select_ss;
    select_ss << "SELECT COUNT(*) FROM UPLOADER_VOD WHERE F_NAME = " << "'" << upload_req->fname << "'"
            << " AND VOD_ID = " << "'" << upload_req->vod_id
            << "'" << " AND REQ_PRIORITY != " << upload_req->req_priority
            << " AND CANCELLED = 0";
    string sel_stmt = select_ss.str();
    int ret = 0;
    ret = sqlite3_prepare_v2(handle, sel_stmt.c_str(), -1, &stmt, NULL);
    if (ret != SQLITE_OK) {
        LOG_E(TAG,"Failed to prepare statement: %s",sqlite3_errmsg(handle));
        sqlite3_finalize(stmt);
        return false;
    }
    ret = sqlite3_step(stmt);
    if (ret != SQLITE_ROW) {
        LOG_E(TAG,"Failed to get count: %s",sqlite3_errmsg(handle));
        sqlite3_finalize(stmt);
        return false;
    }
    int count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return (count != 0);
}

bool is_vod_unavailable(db_handle_t handle, req_upload_msg_t *upload_req) {
    std::lock_guard<std::mutex> lock(dbMutex);

    sqlite3_stmt *stmt;
    stringstream select_ss;
    select_ss << "SELECT COUNT(*) FROM UPLOADER_VOD WHERE F_NAME = " << "'" << upload_req->fname << "'"
            << " AND VOD_ID = " << "'" << upload_req->vod_id << "'"
            << " AND NOT_AVAILABLE = " << true
            << " AND CANCELLED = 0";
    string sel_stmt = select_ss.str();
    int ret = 0;
    ret = sqlite3_prepare_v2(handle, sel_stmt.c_str(), -1, &stmt, NULL);
    if (ret != SQLITE_OK) {
        LOG_E(TAG,"Failed to prepare statement: %s",sqlite3_errmsg(handle));
        sqlite3_finalize(stmt);
        return false;
    }
    ret = sqlite3_step(stmt);
    if (ret != SQLITE_ROW) {
        LOG_E(TAG,"Failed to get count: %s",sqlite3_errmsg(handle));
        sqlite3_finalize(stmt);
        return false;
    }
    int count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return (count != 0);
}


bool mark_vod_unavailable (db_handle_t handle, req_upload_msg_t *upload_req) {
    std::lock_guard<std::mutex> lock(dbMutex);
    stringstream update_str;
    update_str << "UPDATE UPLOADER_VOD SET NOT_AVAILABLE = " << true
            << " WHERE VOD_ID = " << "'" <<  upload_req->vod_id << "'"
            << " AND F_NAME = " << "'" <<  upload_req->fname << "'";
    string update_cmd = update_str.str();
    int ret = 0;
    char *zErrMsg = NULL;
    LOG_I (TAG,"Executing command: %s",update_cmd.c_str());
    ret = sqlite3_exec (handle, update_cmd.c_str(), NULL,0,&zErrMsg);
    if (ret != SQLITE_OK)
    {
        LOG_E(TAG,"Couldn't mark vod as NOT_AVAILABLE : %s", upload_req->vod_id, zErrMsg);
        sqlite3_free(zErrMsg);
        return false;
    }
    LOG_I(TAG,"VOD marked as NOT_AVAILABLE for %s", upload_req->vod_id);
    return true;
}

bool is_vod_req_available(db_handle_t handle, req_upload_msg_t *upload_req) {
    std::lock_guard<std::mutex> lock(dbMutex);

    sqlite3_stmt *stmt;
    stringstream select_ss;
    select_ss << "SELECT COUNT(*) FROM UPLOADER_VOD WHERE F_NAME = " << "'" << upload_req->fname << "'"
            << " AND VOD_ID = " << "'" << upload_req->vod_id << "'"
            << " AND MSG_TYPE = " << upload_req->msg_type
            << " AND CANCELLED = 0";
    string sel_stmt = select_ss.str();
    int ret = 0;
    ret = sqlite3_prepare_v2(handle, sel_stmt.c_str(), -1, &stmt, NULL);
    if (ret != SQLITE_OK) {
        LOG_E(TAG,"Failed to prepare statement: %s",sqlite3_errmsg(handle));
        sqlite3_finalize(stmt);
        return false;
    }
    ret = sqlite3_step(stmt);
    if (ret != SQLITE_ROW) {
        LOG_E(TAG,"Failed to get count: %s",sqlite3_errmsg(handle));
        sqlite3_finalize(stmt);
        return false;
    }
    int count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return (count != 0);
}

bool check_existence_delete_incomplete_file(string fname, string md5sum){
    int r_index_sep = fname.rfind("/")+1;
    string new_mdsum = "";
    string alert_folder_name = fname.substr(r_index_sep,fname.length()-4 - r_index_sep);
    // string alert_folder_name = fname.substr(fname.rfind("/")+1);
    string summary_path = "";
    if(fname.find("zip") == string::npos){
        summary_path = ALERTS_PATH_DIR + "/" + alert_folder_name + "_summary.json";
    }else{
        summary_path = ALERTS_PATH_DIR + "/" + alert_folder_name + "_summary.json.zip";
    }
    LOG_I(TAG, "check_existence_delete_incomplete_file: %s\t1: %s", fname.c_str(), md5sum.c_str());

#if defined(NO_SDCARD)
    if(get_mount_status(ALERTS_PATH_DIR_PHYSMNT, MOUNT_SRC) != MOUNTED){
#else
    if(sdcard_get_mount_status(ALERTS_PATH_DIR) != SDCARD_MOUNTED){
#endif

        LOG_I(TAG,"Sdcard not ready. Not deleting alert from db.");
        return true;
    }
    LOG_I(TAG, "status: %d",access( summary_path.c_str(), F_OK ) == 0);
    if( access( summary_path.c_str(), F_OK ) == 0 ){
        if(!calculate_md5sum(summary_path, new_mdsum)) {
            LOG_E(TAG, "Failed to get md5sum for file %s",summary_path.c_str()); 
        }
        LOG_I(TAG,"check_existence_delete_incomplete_file %s", new_mdsum.c_str());
        if(new_mdsum.length() > 1){
            LOG_I(TAG, "comparing mdsums: %d\t1: %s\t2: %s", md5sum.length(), md5sum.c_str(), new_mdsum.c_str());
            if(md5sum == new_mdsum){
                return true;
            }
            else{
                LOG_I(TAG,"removing incomplete alert file %s", summary_path.c_str());
                remove_alerts_from_disk(fname);
            }
        }
    }
    return false;
}


bool is_ext_vod_fetched(db_handle_t handle, req_upload_msg_t *upload_req) {
    std::lock_guard<std::mutex> lock(dbMutex);

    sqlite3_stmt *stmt;
    stringstream select_ss;
    select_ss << "SELECT COUNT(*) FROM UPLOADER_VOD WHERE F_NAME = " << "'" << upload_req->fname << "'"
            << " AND VOD_ID = " << "'" << upload_req->vod_id << "'"
            << " AND IS_EXT_VIDEO = " << true
            << " AND FETCHED_EXT_VIDEO = " << true
            << " AND CANCELLED = 0";
    string sel_stmt = select_ss.str();
    int ret = 0;
    ret = sqlite3_prepare_v2(handle, sel_stmt.c_str(), -1, &stmt, NULL);
    if (ret != SQLITE_OK) {
        LOG_E(TAG,"Failed to prepare statement: %s",sqlite3_errmsg(handle));
        sqlite3_finalize(stmt);
        return false;
    }
    ret = sqlite3_step(stmt);
    if (ret != SQLITE_ROW) {
        LOG_E(TAG,"Failed to get count: %s",sqlite3_errmsg(handle));
        sqlite3_finalize(stmt);
        return false;
    }
    int count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return (count != 0);
}

bool is_vod_req_failed(db_handle_t handle, req_upload_msg_t *upload_req) {
    std::lock_guard<std::mutex> lock(dbMutex);

    sqlite3_stmt *stmt;
    stringstream select_ss;
    select_ss << "SELECT COUNT(*) FROM UPLOADER_VOD WHERE F_NAME = " << "'" << upload_req->fname << "'"
            << " AND VOD_ID = " << "'" << upload_req->vod_id << "'"
            << " AND VOD_FAILURE = " << true
            << " AND CANCELLED = 0";
    string sel_stmt = select_ss.str();
    int ret = 0;
    ret = sqlite3_prepare_v2(handle, sel_stmt.c_str(), -1, &stmt, NULL);
    if (ret != SQLITE_OK) {
        LOG_E(TAG,"Failed to prepare statement: %s",sqlite3_errmsg(handle));
        sqlite3_finalize(stmt);
        return false;
    }
    ret = sqlite3_step(stmt);
    if (ret != SQLITE_ROW) {
        LOG_E(TAG,"Failed to get count: %s",sqlite3_errmsg(handle));
        sqlite3_finalize(stmt);
        return false;
    }
    int count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return (count != 0);
}

string get_ext_vod_path(db_handle_t handle, req_upload_msg_t *upload_req) {
    std::lock_guard<std::mutex> lock(dbMutex);

    sqlite3_stmt *stmt;
    stringstream select_ss;
    select_ss << "SELECT JSON_NAME FROM UPLOADER_VOD WHERE F_NAME = " << "'" << upload_req->fname << "'"
            << " AND VOD_ID = " << "'" << upload_req->vod_id << "'"
            << " AND IS_EXT_VIDEO = " << true
            << " AND CANCELLED = 0";
    string sel_stmt = select_ss.str();
    int ret = 0;
    ret = sqlite3_prepare_v2(handle, sel_stmt.c_str(), -1, &stmt, NULL);
    if (ret != SQLITE_OK) {
        LOG_E(TAG,"Failed to prepare statement: %s",sqlite3_errmsg(handle));
        sqlite3_finalize(stmt);
        return "";
    }
    ret = sqlite3_step(stmt);
    if (ret != SQLITE_ROW) {
        LOG_E(TAG,"Failed to get ext_vod_path: %s",sqlite3_errmsg(handle));
        sqlite3_finalize(stmt);
        return "";
    }
    string path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    sqlite3_finalize(stmt);
    return path;
}

bool get_vod_req_by_id_and_fname(db_handle_t handle, string video, string vod_id, req_upload_msg_t *req ) {
    std::lock_guard<std::mutex> lock(dbMutex);

    sqlite3_stmt *stmt;
    stringstream select_ss;
    select_ss << "SELECT * FROM UPLOADER_VOD WHERE F_NAME = " << "'" << video << "'"
            << " AND VOD_ID = " << "'" << vod_id << "'" << " AND CANCELLED = 0";
    string sel_stmt = select_ss.str();
    int ret = 0;
    ret = sqlite3_prepare_v2(handle, sel_stmt.c_str(), -1, &stmt, NULL);
    if (ret != SQLITE_OK) {
        LOG_E(TAG,"Failed to prepare statement: %s",sqlite3_errmsg(handle));
        sqlite3_finalize(stmt);
        return false;
    }
    ret = sqlite3_step(stmt);
    if (ret != SQLITE_ROW) {
        LOG_E(TAG,"Failed to get vod request by id and fname: %s",sqlite3_errmsg(handle));
        sqlite3_finalize(stmt);
        return false;
    }

    int id =  sqlite3_column_int (stmt, 0);
    req->msg_type = (msg_type_t)sqlite3_column_int(stmt, 1);
    req->length = sqlite3_column_int(stmt, 2);
    strcpy(req->client_id, reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)));
    req->msg_idx = sqlite3_column_int(stmt, 4);
    req->level = (upload_level_t)sqlite3_column_int(stmt, 5);
    req->payload_size = (payload_size_t)sqlite3_column_int(stmt, 6);
    strcpy(req->fname, reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7)));
    strcpy(req->json_fname, reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8)));
    req->req_id = sqlite3_column_int64(stmt, 9);
    req->retry_count = sqlite3_column_int(stmt, 10);
    istringstream request_time_ss(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 11)));
    request_time_ss >> req->request_time;
    req->trim = (bool)sqlite3_column_int(stmt, 12);
    req->start_sec = (payload_size_t)sqlite3_column_int(stmt, 13);
    req->end_sec = (payload_size_t)sqlite3_column_int(stmt, 14);
    req->part_id = sqlite3_column_int(stmt, 15);
    req->upload_observation = (bool)sqlite3_column_int(stmt, 16);
    req->upload_audio = (bool)sqlite3_column_int(stmt, 17);
    req->quality = sqlite3_column_int(stmt, 18);
    req->alert_id = sqlite3_column_int64(stmt, 19);
    req->ib_alert = (bool)sqlite3_column_int(stmt, 20); //ib_alert is used for is_ext_video
    req->vod_failure = (bool)sqlite3_column_int(stmt, 22);
    req->req_priority = (upload_level_t) sqlite3_column_int(stmt, 24);
    strcpy(req->vod_id, reinterpret_cast<const char*>(sqlite3_column_text(stmt, 25)));

    sqlite3_finalize(stmt);
    return true;
}

void get_pending_vods(db_handle_t handle, vector<req_upload_msg_t*> &vod_list) {
    sqlite3_stmt *stmt;
    const char *select_cmd = "SELECT * FROM UPLOADER_VOD WHERE CANCELLED = 0";
    int ret = 0;
    char *zErrMsg = NULL;

    {
        std::lock_guard<std::mutex> lock(dbMutex);
        ret = sqlite3_prepare_v2(handle, select_cmd, -1, &stmt, NULL);
    }
    if (ret != SQLITE_OK) {
        LOG_E(TAG,"Failed to prepare statement: %s",sqlite3_errmsg(handle));
        sqlite3_finalize(stmt);
        return;
    }

    while ((ret = sqlite3_step(stmt)) == SQLITE_ROW) {
        req_upload_msg_t *req = (req_upload_msg_t*)malloc(sizeof(req_upload_msg_t));
        int id =  sqlite3_column_int (stmt, 0);
        req->msg_type = (msg_type_t)sqlite3_column_int(stmt, 1);
        req->length = sqlite3_column_int(stmt, 2);
        strcpy(req->client_id, reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)));
        req->msg_idx = sqlite3_column_int(stmt, 4);
        req->level = (upload_level_t)sqlite3_column_int(stmt, 5);
        req->payload_size = (payload_size_t)sqlite3_column_int(stmt, 6);
        strcpy(req->fname, reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7)));
        strcpy(req->json_fname, reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8)));
        req->req_id = sqlite3_column_int64(stmt, 9);
        req->retry_count = sqlite3_column_int(stmt, 10);
        istringstream request_time_ss(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 11)));
        request_time_ss >> req->request_time;
        req->trim = (bool)sqlite3_column_int(stmt, 12);
        req->start_sec = (payload_size_t)sqlite3_column_int(stmt, 13);
        req->end_sec = (payload_size_t)sqlite3_column_int(stmt, 14);
        req->part_id = sqlite3_column_int(stmt, 15);
        req->upload_observation = (bool)sqlite3_column_int(stmt, 16);
        req->upload_audio = (bool)sqlite3_column_int(stmt, 17);
        req->quality = sqlite3_column_int(stmt, 18);
        req->alert_id = sqlite3_column_int64(stmt, 19);
        req->ib_alert = (bool)sqlite3_column_int(stmt, 20); //ib_alert is used for is_ext_video
        req->vod_failure = (bool)sqlite3_column_int(stmt, 22);
        req->req_priority = (upload_level_t) sqlite3_column_int(stmt, 24);
        strcpy(req->vod_id, reinterpret_cast<const char*>(sqlite3_column_text(stmt, 25)));
        req->cancelled = (bool)sqlite3_column_int(stmt, 26);

        vod_list.push_back(req);
    }
    sqlite3_finalize(stmt);
    if (ret != SQLITE_DONE)
    {
        LOG_E(TAG,"Error get_pending_vods :%s",sqlite3_errmsg(handle));
    }
}

void set_ext_vod_req_status(db_handle_t handle, req_upload_msg_t *upload_req, int ext_vod_req_status) {
    std::lock_guard<std::mutex> lock(dbMutex);

    stringstream update_str;
    update_str << "UPDATE UPLOADER_VOD SET EXT_VOD_REQ_STATUS = " << ext_vod_req_status
            << " WHERE F_NAME = " << "'" <<  upload_req->fname << "'"
            << " AND VOD_ID = " << "'" <<  upload_req->vod_id << "'"
            << " AND IS_EXT_VIDEO = " << true;
    string update_cmd = update_str.str();
    int ret = 0;
    char *zErrMsg = NULL;
    LOG_I (TAG,"Executing command: %s",update_cmd.c_str());
    ret = sqlite3_exec (handle, update_cmd.c_str(), NULL,0,&zErrMsg);
    if (ret != SQLITE_OK)
    {
        LOG_E(TAG,"Couldn't update DB with ext_vod_stats %d for %s - %s : %s",ext_vod_req_status, upload_req->vod_id, upload_req->fname, zErrMsg);
        sqlite3_free(zErrMsg);
    }
    else
    {
        LOG_I (TAG,"Updated DB with ext_vod_stats %d for %s - %s",ext_vod_req_status, upload_req->vod_id, upload_req->fname);
    }
}


void update_ext_vod_stats(db_handle_t handle, req_upload_msg_t *upload_req, int ext_vod_req_status, int is_fetched,
        int not_available, int is_failed, string failure_reason, string rgb_status) {
    std::lock_guard<std::mutex> lock(dbMutex);

    stringstream update_str;
    update_str << "UPDATE UPLOADER_VOD SET EXT_VOD_REQ_STATUS = " << ext_vod_req_status
            << ", FETCHED_EXT_VIDEO = " << is_fetched
            << ", NOT_AVAILABLE = " << not_available
            << ", VOD_FAILURE = " << is_failed
            << ", FAILURE_REASON = " << "'" << failure_reason << "'"
            << ", RGB_STATUS = " << "'" << rgb_status << "'"
            << " WHERE F_NAME = " << "'" <<  upload_req->fname << "'"
            << " AND VOD_ID = " << "'" <<  upload_req->vod_id << "'"
            << " AND IS_EXT_VIDEO = " << true;
    string update_cmd = update_str.str();
    int ret = 0;
    char *zErrMsg = NULL;
    LOG_I (TAG,"Executing command: %s",update_cmd.c_str());
    ret = sqlite3_exec (handle, update_cmd.c_str(), NULL,0,&zErrMsg);
    if (ret != SQLITE_OK)
    {
        LOG_E(TAG,"Couldn't update DB with ext_vod_stats %d, RGB_STATUS %s for %s - %s : %s",ext_vod_req_status, rgb_status.c_str(), upload_req->vod_id, upload_req->fname, zErrMsg);
        sqlite3_free(zErrMsg);
    }
    else
    {
        LOG_I (TAG,"Updated DB with ext_vod_stats %d, RGB_STATUS %s for %s - %s",ext_vod_req_status, rgb_status.c_str(), upload_req->vod_id, upload_req->fname);
    }
}

int get_ext_vod_req_status(db_handle_t handle,  req_upload_msg_t *upload_req) {
    std::lock_guard<std::mutex> lock(dbMutex);

    sqlite3_stmt *stmt;
    stringstream select_ss;
    select_ss << "SELECT EXT_VOD_REQ_STATUS FROM UPLOADER_VOD WHERE F_NAME = " << "'" << upload_req->fname << "'"
            << " AND VOD_ID = " << "'" << upload_req->vod_id << "'"
            << " AND REQ_PRIORITY = " << upload_req->req_priority
            << " AND IS_EXT_VIDEO = " << true;

    string sel_stmt = select_ss.str();
    LOG_I (TAG,"Executing command: %s",sel_stmt.c_str());

    int ret = 0;
    ret = sqlite3_prepare_v2(handle, sel_stmt.c_str(), -1, &stmt, NULL);
    if (ret != SQLITE_OK) {
        LOG_E(TAG,"Failed to prepare statement: %s",sqlite3_errmsg(handle));
        sqlite3_finalize(stmt);
        return -1;
    }
    ret = sqlite3_step(stmt);
    if (ret != SQLITE_ROW) {
        LOG_E(TAG,"Failed to get status: %s",sqlite3_errmsg(handle));
        sqlite3_finalize(stmt);
        return -1;
    }
    int status = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return status;
}

string get_rgb_status(db_handle_t handle, req_upload_msg_t *upload_req) {
    std::lock_guard<std::mutex> lock(dbMutex);

    sqlite3_stmt *stmt;
    stringstream select_ss;
    select_ss << "SELECT RGB_STATUS FROM UPLOADER_VOD WHERE F_NAME = " << "'" << upload_req->fname << "'"
            << " AND VOD_ID = " << "'" << upload_req->vod_id << "'";

    string sel_stmt = select_ss.str();
    int ret = 0;
    ret = sqlite3_prepare_v2(handle, sel_stmt.c_str(), -1, &stmt, NULL);
    if (ret != SQLITE_OK) {
        LOG_E(TAG,"Failed to prepare statement: %s",sqlite3_errmsg(handle));
        sqlite3_finalize(stmt);
        return "";
    }
    ret = sqlite3_step(stmt);
    if (ret != SQLITE_ROW) {
        LOG_E(TAG,"Failed to get status: %s",sqlite3_errmsg(handle));
        sqlite3_finalize(stmt);
        return "";
    }
    string status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    sqlite3_finalize(stmt);
    return status;
}

void get_failed_uploads(db_handle_t handle, vector<req_upload_msg_t*> &upload_list){
    sqlite3_stmt *stmt;
    const char *select_cmd = "SELECT * FROM UPLOADER WHERE STATUS >= 0 AND STATUS <= 2";
    int ret = 0;

    {
        std::lock_guard<std::mutex> lock(dbMutex);
        ret = sqlite3_prepare_v2(handle, select_cmd, -1, &stmt, NULL);
    }

    if (ret != SQLITE_OK)
    {
        LOG_E(TAG,"Failed to prepare statement: %s",sqlite3_errmsg(handle));
    }
    while ((ret = sqlite3_step(stmt)) == SQLITE_ROW)
    {
        //req_upload_msg_t req;
        req_upload_msg_t *req = (req_upload_msg_t*)malloc(sizeof(req_upload_msg_t));
        int id =  sqlite3_column_int (stmt, 0);
        req->msg_type = (msg_type_t)sqlite3_column_int(stmt, 1);
        req->length = sqlite3_column_int(stmt, 2);
        strcpy(req->client_id, reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)));
        req->msg_idx = sqlite3_column_int(stmt, 4);
        req->res_reqd = sqlite3_column_int(stmt, 5);
        req->level = (upload_level_t)sqlite3_column_int(stmt, 6);
        req->payload_size = (payload_size_t)sqlite3_column_int(stmt, 7);
        strcpy(req->fname, reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8)));
        strcpy(req->json_fname, reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9)));
        req->req_id = sqlite3_column_int(stmt, 10);
        req->retry_count = sqlite3_column_int(stmt, 11);
        LOG_I(TAG,"get_failed_uploads %s", req->fname);

        istringstream request_time_ss(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 12)));
        request_time_ss >> req->request_time;
        string md5sum;
        md5sum = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 13));
        LOG_I(TAG,"get_failed_uploads md5sum %s", md5sum.c_str());

        if(!check_existence_delete_incomplete_file(req->fname, md5sum)){
            delete_upload_request(handle, req);
        }else{
            LOG_I(TAG,"get_failed_uploads: %s %llu", req->fname, req->request_time);
            upload_list.push_back(req);
        }
    }
    sqlite3_finalize(stmt);
    if (ret != SQLITE_DONE)
    {
        LOG_E(TAG,"Error get_failed_uploads :%s",sqlite3_errmsg(handle));
    }
}

void write_to_csv(const char* filename, sqlite3_stmt* stmt) {
    std::ofstream file(filename);

    if(!file.is_open()) {
        LOG_E(TAG,"write_to_csv :: Failed to open file: %s", filename);
        return;
    }

    // Write headers
    for (int i = 0; i < sqlite3_column_count(stmt); ++i) {
        if (i > 0) file << ",";
        file << sqlite3_column_name(stmt, i);
    }
    file << "\n";

    // Write rows
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        for (int i = 0; i < sqlite3_column_count(stmt); ++i) {
            if (i > 0) file << ",";
            const char* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, i));
            if (text) {
                file << text;
            } else {
                file << "NULL";
            }
        }
        file << "\n";
    }

    file.close();

    if (file.fail()) {
        LOG_E(TAG,"write_to_csv :: File write failed!");
    } else {
        LOG_I(TAG,"write_to_csv :: File created successfully.");
    }
}


bool dump_vod_list_csv(db_handle_t handle, const char* csv_file_path,  bool &is_file_empty)
{
    sqlite3_stmt* stmt;
    int ret = 0;

    std::string sql = R"(
        SELECT
            (SELECT COUNT(*) FROM UPLOADER_VOD b WHERE b.LEVEL < a.LEVEL OR (b.LEVEL = a.LEVEL AND b.REQ_TIME <= a.REQ_TIME)) AS RANK,
            VOD_ID, F_NAME, REQ_PRIORITY, LEVEL as EFFECTIVE_PRIORITY, RETRY_COUNT, IS_EXT_VIDEO, FETCHED_EXT_VIDEO,
            VOD_FAILURE, FAILURE_REASON, NOT_AVAILABLE, REQ_TIME, ELAPSED_TIME_MIN
        FROM UPLOADER_VOD a
        WHERE CANCELLED = 0
        ORDER BY LEVEL ASC, REQ_TIME ASC;
    )";
    {
        std::lock_guard<std::mutex> lock(dbMutex);
        ret = sqlite3_prepare_v2(handle, sql.c_str(), -1, &stmt, NULL);
    }
    if (ret != SQLITE_OK)
    {
        LOG_E(TAG,"Failed to prepare statement: %s",sqlite3_errmsg(handle));
        sqlite3_finalize(stmt);
        return false;
    }

    // Check if the query results are empty
    ret = sqlite3_step(stmt);
    LOG_I(TAG,"sqlite3_step() returned: %d",ret);

    if (ret == SQLITE_DONE)
    {
        is_file_empty = true;
    }
    else if (ret == SQLITE_ROW)
    {
        is_file_empty = false;

        // Reset the statement to iterate from the beginning
        sqlite3_reset(stmt);

        // Write the query results to a CSV file
        write_to_csv(csv_file_path, stmt);
    }
    else
    {
        LOG_E(TAG,"Failed to get query results: %s",sqlite3_errmsg(handle));
        sqlite3_finalize(stmt);
        return false;
    }

    // Finalize the statement and close the database
    sqlite3_finalize(stmt);
    return true;

}


bool print_db (db_handle_t handle)
{
    sqlite3_stmt *stmt;
    const char *select_cmd = "SELECT * FROM UPLOADER";
    int ret = 0;
    ret = sqlite3_prepare_v2(handle, select_cmd, -1, &stmt, NULL);
    if (ret != SQLITE_OK)
    {
        LOG_E(TAG,"Failed to prepare statement: %s",sqlite3_errmsg(handle));
        sqlite3_finalize(stmt);
        return false;
    }
    while ((ret = sqlite3_step(stmt)) == SQLITE_ROW)
    {
        int id =  sqlite3_column_int (stmt, 0);
        const unsigned char *fname = sqlite3_column_text(stmt, 8);
        const int status = sqlite3_column_int(stmt, 14);
        LOG_I (TAG,"%d\t %s\t %d",id,fname,status);
    }
    if (ret != SQLITE_DONE)
    {
        LOG_E(TAG,"Error:%s",sqlite3_errmsg(handle));
        sqlite3_finalize(stmt);
        return false;
    }
    sqlite3_finalize(stmt);
    return true;
}

static bool clear_db (db_handle_t handle)
{
    string del_cmd = "DELETE FROM UPLOADER";
    del_cmd += "; DELETE FROM SQLITE_SEQUENCE WHERE NAME='UPLOADER'";
    LOG_I (TAG,"del cmd: %s",del_cmd.c_str());
    int ret = 0;
    char *zErrMsg = 0;

    ret = sqlite3_exec(handle, del_cmd.c_str(), NULL, NULL, &zErrMsg);
   if( ret != SQLITE_OK )
    {
      LOG_E(TAG,"SQL delete error: %s\n", zErrMsg);
      sqlite3_free(zErrMsg);
   }
    else
    {
      LOG_I(TAG,"Cleared DB successfully\n");
   }
    return true;
}

bool delete_db (db_handle_t handle)
{
    int ret = 0;
    string db_fname = get_db_fname();

    ret = sqlite3_close(handle);
    if (ret != SQLITE_OK)
    {
        LOG_I (TAG,"sqlite3_close failed");
        return false;
    }
    if (remove (db_fname.c_str()) != 0)
    {
        LOG_E (TAG,"Couldn't remove db file %s",db_fname);
        return false;
    }
    return true;
}
