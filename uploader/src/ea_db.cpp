#include <log.h>
#include <sqlite3.h>
#include <string>
#include "nd_msg_types.h"
#include "nd_file_utils.h"
#include <mutex>
#include <stdio.h>
#include <sstream>
#include <unistd.h>

using namespace std;

#define TAG "EADB"
typedef sqlite3 *db_handle_t;
mutex eaDbMutex; // Mutex for synchronizing access from multiple threads

//If not exists, create table for Event access preview images
void create_ea_imgs_table_if_not_exists(db_handle_t handle){ 
    char *sql_err;
    char *sql_create_EA_IMGS_table = "CREATE TABLE IF NOT EXISTS UPLOADER_EA_IMGS(" \
            "INDEXID INTEGER PRIMARY KEY AUTOINCREMENT," \
            "NAME               TEXT    NOT NULL UNIQUE," \
            "SESSION_COUNT      INT     NOT NULL," \
            "ERROR_CODE         INT     NOT NULL,"\
            "FILE_SIZE_BYTES    INT     NOT NULL," \
            "UDID               INT     NOT NULL," \
            "CAM_TYPE           INT     NOT NULL);";

    int ret = sqlite3_exec(handle, sql_create_EA_IMGS_table, NULL, 0, &sql_err);
    if (ret != SQLITE_OK)
    {
        LOG_E(TAG,"can't create db table UPLOADER_EA_IMGS: %s",sql_err);
        sqlite3_free(sql_err);
        sqlite3_close(handle);
        handle = NULL; 
    }else{
        LOG_I (TAG,"UPLOADER_EA_IMGS table exists/created!");
    }
}

db_handle_t create_ea_db (string filepath)
{
    char *sql = NULL;
    char *zErrMsg = NULL;
    db_handle_t handle = NULL;

    int ret = sqlite3_open (filepath.c_str(), &handle);
    if (ret)
    {
        LOG_I(TAG,"Can't open EA db:%s",sqlite3_errmsg(handle));
        sqlite3_close(handle);
        handle = NULL;
        return NULL;
    }
    else {
        LOG_I(TAG,"opened EA db");
        const char * const command = "PRAGMA integrity_check;";
        ret = sqlite3_exec (handle, command, NULL, 0, &zErrMsg);
        if (ret != SQLITE_OK) {
            LOG_E(TAG,"EA DB integrity check failed: %s",zErrMsg);
            sqlite3_free(zErrMsg);
            sqlite3_close(handle);
            handle = NULL;
            file_delete(filepath);
            ret = sqlite3_open (filepath.c_str(), &handle);

            if (ret != SQLITE_OK) {
                LOG_I(TAG,"Can't recreate EA db:%s",sqlite3_errmsg(handle));
                sqlite3_close(handle);
                handle = NULL;
                return NULL;
            }
            else {
                LOG_C(TAG,"Recreated EA db");
            }
        }
    }

    create_ea_imgs_table_if_not_exists(handle);

    return handle;
}

bool insert_db_ea_imgs(db_handle_t handle, uploader_add_ea_file_db_msg_t *upload_req, ea_img_status& error_code) {
    std::lock_guard<std::mutex> lock(eaDbMutex);
    
    bool insertion_status = false; 
    do{
        // SQL insert statement with placeholders
        string insert_query = "INSERT INTO UPLOADER_EA_IMGS (NAME, SESSION_COUNT, ERROR_CODE, FILE_SIZE_BYTES, UDID, CAM_TYPE) "
                                "VALUES (?, ?, ?, ?, ?, ?)";
        
        sqlite3_stmt *stmt = nullptr;
        int ret = sqlite3_prepare_v2(handle, insert_query.c_str(), -1, &stmt, NULL);
        if (ret != SQLITE_OK) {
            LOG_E(TAG, "Failed to prepare statement: %s", sqlite3_errmsg(handle));
            if(stmt) {
                sqlite3_finalize(stmt);
            }
            break;
        }

        // Bind parameters
        sqlite3_bind_text(stmt, 1, upload_req->base_file_name, -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 2, upload_req->session_count);
        sqlite3_bind_int(stmt, 3, static_cast<int>(error_code));
        sqlite3_bind_int(stmt, 4, upload_req->file_size_bytes);
        sqlite3_bind_int64(stmt, 5, upload_req->udid);
        sqlite3_bind_int(stmt, 6, upload_req->cam_type);

        // Execute the statement
        ret = sqlite3_step(stmt);

        if(stmt){
            sqlite3_finalize(stmt);
        }

        if(ret == SQLITE_DONE){
            LOG_I(TAG, "Inserted EA file %s in table", upload_req->base_file_name);
        }else if(ret == SQLITE_CONSTRAINT){
            LOG_E(TAG, "Entry already exists! Not inserting again into table for %s", upload_req->base_file_name);
        }else{
            LOG_E(TAG, "Insertion failed: %s", sqlite3_errmsg(handle));
            break;
        }

        insertion_status = true; 
    }while(false);

    return insertion_status; 
}

bool insert_db_ea_imgs_in_bulk(db_handle_t handle, vector<ea_file_data_t>& file_details) {
    std::lock_guard<std::mutex> lock(eaDbMutex);
    
    char *errMsg = nullptr;

    if (sqlite3_exec(handle, "BEGIN TRANSACTION;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
        LOG_E(TAG, "Failed to begin transaction: %s", errMsg);
        sqlite3_free(errMsg);
        return false;
    }

    string insert_query = "INSERT INTO UPLOADER_EA_IMGS (NAME, SESSION_COUNT, ERROR_CODE, FILE_SIZE_BYTES, UDID, CAM_TYPE) "
                          "VALUES (?, ?, ?, ?, ?, ?)";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(handle, insert_query.c_str(), -1, &stmt, NULL) != SQLITE_OK) {
        LOG_E(TAG, "Failed to prepare statement: %s", sqlite3_errmsg(handle));
        if(errMsg) {
            sqlite3_free(errMsg); // Free before reuse
            errMsg = nullptr;
        }

        if (sqlite3_exec(handle, "ROLLBACK;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
            LOG_E(TAG, "Rollback failed: %s", errMsg);
            sqlite3_free(errMsg);
        }
        return false;
    }

    for (const auto& file : file_details) {
        sqlite3_bind_text(stmt, 1, file.file_name.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 2, file.session_count);
        sqlite3_bind_int(stmt, 3, static_cast<int>(file.error_code));
        sqlite3_bind_int(stmt, 4, file.file_size_bytes);
        sqlite3_bind_int(stmt, 5, file.udid);
        sqlite3_bind_int(stmt, 6, file.cam_type);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            LOG_E(TAG, "Insertion failed for file %s: %s", file.file_name.c_str(), sqlite3_errmsg(handle));
            sqlite3_finalize(stmt);

            if(errMsg) {
                sqlite3_free(errMsg); // Free before reuse
                errMsg = nullptr;
            }

            if (sqlite3_exec(handle, "ROLLBACK;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
                LOG_E(TAG, "Rollback failed: %s", errMsg);
                sqlite3_free(errMsg);
            }
            return false;
        }

        sqlite3_reset(stmt);
    }

    sqlite3_finalize(stmt);

    if (sqlite3_exec(handle, "COMMIT;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
        LOG_E(TAG, "Final commit failed: %s", errMsg);
        sqlite3_free(errMsg);
        errMsg = nullptr;

        if (sqlite3_exec(handle, "ROLLBACK;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
            LOG_E(TAG, "Rollback failed after commit error: %s", errMsg);
            sqlite3_free(errMsg);
        }

        return false;
    }

    LOG_I(TAG, "Successfully inserted %d EA file records.", file_details.size());
    return true;
}

// Method to get all the db entries for bootup sync between disk and DB
bool get_db_ea_imgs_for_cleanup(db_handle_t handle, vector<ea_db_file_data_t>&all_files_db){
    std::lock_guard<std::mutex> lock(eaDbMutex);
    sqlite3_stmt *stmt = nullptr;

    string query = "SELECT INDEXID, NAME, FILE_SIZE_BYTES, ERROR_CODE FROM UPLOADER_EA_IMGS;";
    
    int ret = sqlite3_prepare_v2(handle, query.c_str(), -1, &stmt, NULL);
    if (ret != SQLITE_OK) {
        LOG_E(TAG, "Failed to prepare statement: %s", sqlite3_errmsg(handle));
        if (stmt) {
            sqlite3_finalize(stmt);
        }        
        return false;
    }

    ea_db_file_data_t file; // Temporary variable to store the data
    while ((ret = sqlite3_step(stmt)) == SQLITE_ROW) {
        file.index_id = sqlite3_column_int(stmt, 0);
        file.file_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        file.file_size = sqlite3_column_int64(stmt, 2);
        file.error_code = static_cast<ea_img_status>(sqlite3_column_int(stmt, 3));
        all_files_db.push_back(file);
    }
    
    sqlite3_finalize(stmt);
    if (ret != SQLITE_DONE) {
        LOG_E(TAG, "Unexpected error after fetching data: %s", sqlite3_errmsg(handle));
        return false;
    }

    return true;
}

// Method to get number of entries in the DB for DB size check
bool get_db_ea_imgs_count(db_handle_t handle, int& count){
    std::lock_guard<std::mutex> lock(eaDbMutex);
    sqlite3_stmt *stmt = nullptr;

    string query = "SELECT COUNT(*) FROM UPLOADER_EA_IMGS;";

    int ret = sqlite3_prepare_v2(handle, query.c_str(), -1, &stmt, NULL);
    if (ret != SQLITE_OK) {
        LOG_E(TAG, "Failed to prepare statement: %s", sqlite3_errmsg(handle));
        if (stmt) {
            sqlite3_finalize(stmt);
        }
        return false;
    }

    ret = sqlite3_step(stmt);
    if (ret != SQLITE_ROW) {
        LOG_E(TAG, "Failed to get count of EA images: %s", sqlite3_errmsg(handle));
        if(stmt){
            sqlite3_finalize(stmt);
        }
        return false;
    }

    count = sqlite3_column_int(stmt, 0);

    sqlite3_finalize(stmt);
    return true;
}

// Method to get the oldest 'overflow_count' number of entries from the DB to handle DB size overflow
bool get_oldest_db_ea_imgs(db_handle_t handle, long& overflow_count, vector<pair<string, int>>&oldest_files){
    std::lock_guard<std::mutex> lock(eaDbMutex);
    sqlite3_stmt *stmt = nullptr;

    // SQL query to get the oldest entries based on the default INDEXID sorting following FIFO
    stringstream query;
    query << "SELECT NAME, INDEXID FROM UPLOADER_EA_IMGS LIMIT " << overflow_count << ";";

    int ret = sqlite3_prepare_v2(handle, query.str().c_str(), -1, &stmt, NULL);
    if (ret != SQLITE_OK) {
        LOG_E(TAG, "Failed to prepare statement: %s", sqlite3_errmsg(handle));
        if(stmt){
            sqlite3_finalize(stmt);
        }
        return false;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        string name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        int indexid = sqlite3_column_int(stmt, 1);
        oldest_files.push_back(make_pair(name, indexid));
    }

    sqlite3_finalize(stmt);
    LOG_I(TAG, "Successfully retrieved the oldest %ld entries.", overflow_count);
    return true;
}

// Method to delete the oldest entries less than target indexid from the DB to handle DB size overflow
bool delete_oldest_db_ea_imgs(db_handle_t handle, int target_indexid) {
    std::lock_guard<std::mutex> lock(eaDbMutex);

    stringstream delete_str;
    delete_str << "DELETE FROM UPLOADER_EA_IMGS WHERE INDEXID <= " << target_indexid << ";";
    string delete_cmd = delete_str.str();

    int ret = sqlite3_exec(handle, delete_cmd.c_str(), NULL, 0, NULL);
    if (ret != SQLITE_OK) {
        LOG_E(TAG, "Failed to delete oldest EA images: %s", sqlite3_errmsg(handle));
        return false;
    }

    LOG_I(TAG, "Successfully deleted oldest DB entries up to INDEXID %d", target_indexid);
    return true;
}

// Method to update error code of "entries that have IMAGE_AVAILABLE but not present in disk" -> (misc)
bool update_misc_db_ea_imgs(db_handle_t handle, vector<int>& index_list, ea_img_status error_code) {
    std::lock_guard<std::mutex> lock(eaDbMutex);

    const int BATCH_SIZE = 500;
    int total_entries = index_list.size();

    for (int list_itr = 0; list_itr < total_entries; list_itr += BATCH_SIZE) {
        stringstream update_str;
        update_str << "UPDATE UPLOADER_EA_IMGS SET ERROR_CODE = " << static_cast<int>(error_code)
                   << " WHERE INDEXID IN (";

        int batch_end = min(list_itr + BATCH_SIZE, total_entries);
        for (int batch_itr = list_itr; batch_itr < batch_end; ++batch_itr) {
            update_str << index_list[batch_itr];
            if (batch_itr < batch_end - 1) {
                update_str << ", ";
            }
        }

        update_str << ");";
        string update_cmd = update_str.str();

        int ret = sqlite3_exec(handle, update_cmd.c_str(), NULL, 0, NULL);
        if (ret != SQLITE_OK) {
            LOG_E(TAG, "Failed to update error code for provided INDEXID batch: %s", sqlite3_errmsg(handle));
            return false;
        }
    }

    LOG_I(TAG, "Successfully updated error code of provided index_list in batches.");
    return true;
}

// Method to get the oldest 'batch_count' number of entries based on session_count for uploading to cloud
bool get_db_ea_imgs_in_batch(db_handle_t handle, int batch_count, vector<ea_cache_file_data_t> &ea_imgs){
    std::lock_guard<std::mutex> lock(eaDbMutex);

    sqlite3_stmt *stmt = nullptr;
    stringstream query_stream;
    
    query_stream << "SELECT INDEXID, NAME, ERROR_CODE FROM UPLOADER_EA_IMGS "
                << "ORDER BY SESSION_COUNT "
                << "LIMIT " << batch_count << ";";

    string query = query_stream.str();

    int ret = sqlite3_prepare_v2(handle, query.c_str(), -1, &stmt, NULL);
    if (ret != SQLITE_OK) {
        LOG_E(TAG, "Failed to prepare statement: %s", sqlite3_errmsg(handle));
        if (stmt) {
            sqlite3_finalize(stmt);
        }
        return false;
    }

    // Fetching the results and populating ea_imgs
    ea_cache_file_data_t file; 
    while ((ret = sqlite3_step(stmt)) == SQLITE_ROW) {
        file.index_id = sqlite3_column_int(stmt, 0);
        file.file_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        file.error_code = static_cast<ea_img_status>(sqlite3_column_int(stmt, 2));
        ea_imgs.push_back(file);
    }
    
    sqlite3_finalize(stmt);
    if (ret != SQLITE_DONE) {
        LOG_E(TAG, "Unexpected error after fetching data: %s", sqlite3_errmsg(handle));
        return false;
    }

    return true;
}

// Method to get db entries for EA images folder size cleanup
bool get_db_ea_imgs_for_folder_cleanup(db_handle_t handle, vector<ea_file_disk_cleanup_data_t>& db_file_details, string folder_path){
    std::lock_guard<std::mutex> lock(eaDbMutex);

    sqlite3_stmt *stmt = nullptr;
    stringstream query_stream;
    query_stream << "SELECT SESSION_COUNT, NAME, FILE_SIZE_BYTES FROM UPLOADER_EA_IMGS "
                 << "WHERE ERROR_CODE = " << static_cast<int>(ea_img_status::IMAGE_AVAILABLE)
                 << " ORDER BY SESSION_COUNT ASC;";

    string query = query_stream.str();

    int ret = sqlite3_prepare_v2(handle, query.c_str(), -1, &stmt, NULL);
    if (ret != SQLITE_OK) {
        LOG_E(TAG, "Failed to prepare statement: %s", sqlite3_errmsg(handle));
        if (stmt) {
            sqlite3_finalize(stmt);
        }
        return false;
    }

    ea_file_disk_cleanup_data_t file_details; 
    while ((ret = sqlite3_step(stmt)) == SQLITE_ROW) {
        file_details.session_count = sqlite3_column_int64(stmt, 0);
        file_details.file_path = folder_path + reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        file_details.file_size = sqlite3_column_int(stmt, 2);
        db_file_details.push_back(file_details);
    }
    
    sqlite3_finalize(stmt);
    if (ret != SQLITE_DONE) {
        LOG_E(TAG, "Unexpected error after fetching data: %s", sqlite3_errmsg(handle));
        return false;
    }

    return true;
}

// Method to delete DB entries after successful upload of EA images to cloud 
bool delete_db_ea_imgs_in_batch(db_handle_t handle, vector<ea_cache_file_data_t>&ea_imgs_cache){
    std::lock_guard<std::mutex> lock(eaDbMutex);

    stringstream delete_str;
    delete_str << "DELETE FROM UPLOADER_EA_IMGS "
               << "WHERE INDEXID IN (";

    // Add the INDEXID values from ea_imgs_cache to the IN clause
    for (int ind = 0; ind < ea_imgs_cache.size(); ++ind) {
        delete_str << ea_imgs_cache[ind].index_id;
        if (ind != ea_imgs_cache.size() - 1) {
            delete_str << ", ";
        }
    }

    delete_str << ");";

    string delete_cmd = delete_str.str();
    printf("delete_str: %s\n", delete_cmd.c_str());

    int ret = 0;
    char *zErrMsg = NULL;

    ret = sqlite3_exec(handle, delete_cmd.c_str(), NULL, 0, &zErrMsg);
    if (ret != SQLITE_OK) {
        LOG_E(TAG, "Failed to delete entries from UPLOADER_EA_IMGS: %s", zErrMsg);
        if(zErrMsg){
            sqlite3_free(zErrMsg);
        }
        return false;
    }

    return true;
}

//Method used to get db entries below a session count received from CB broadcast for DRP
bool get_db_ea_imgs_below_sc(db_handle_t handle, int64_t target_sc, vector<string>& files_list) {
    std::lock_guard<std::mutex> lock(eaDbMutex);

    stringstream select_str;
    select_str << "SELECT NAME FROM UPLOADER_EA_IMGS "
           << "WHERE SESSION_COUNT <= " << target_sc 
           << " AND ERROR_CODE = " << static_cast<int>(ea_img_status::IMAGE_AVAILABLE)
           << " ORDER BY SESSION_COUNT;";
    string select_cmd = select_str.str();
    sqlite3_stmt *stmt = nullptr;

    // Prepare and execute the select query
    int ret = sqlite3_prepare_v2(handle, select_cmd.c_str(), -1, &stmt, NULL);
    if (ret != SQLITE_OK) {
        LOG_E(TAG, "Failed to prepare SELECT statement: %s", sqlite3_errmsg(handle));
        if (stmt) {
            sqlite3_finalize(stmt);
        }
        return false;
    }

    while ((ret = sqlite3_step(stmt)) == SQLITE_ROW) {
        string name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        files_list.push_back(name);
    }

    sqlite3_finalize(stmt);

    if (ret != SQLITE_DONE) {
        LOG_E(TAG, "Unexpected error after fetching data: %s", sqlite3_errmsg(handle));
        return false;
    }

    return true;
}

// Update file size and error_code of entries deleted due to DRP or Folder size quota
bool update_excess_db_ea_imgs(db_handle_t handle, int64_t target_sc, ea_img_status error_code){
    std::lock_guard<std::mutex> lock(eaDbMutex);

    stringstream update_str;
    update_str << "UPDATE UPLOADER_EA_IMGS "
               << "SET ERROR_CODE = " << static_cast<int>(error_code) << ", "
               << "FILE_SIZE_BYTES = " << 0
               << " WHERE SESSION_COUNT <= " << target_sc
               << " AND ERROR_CODE = " << static_cast<int>(ea_img_status::IMAGE_AVAILABLE) << ";";
    string update_cmd = update_str.str();

    int ret = sqlite3_exec(handle, update_cmd.c_str(), NULL, 0, NULL);
    if (ret != SQLITE_OK) {
        LOG_E(TAG, "Failed to update error code for entries up to session count %d: %s", target_sc, sqlite3_errmsg(handle));
        return false;
    }

    LOG_I(TAG, "Marked error code as %d for entries up to session count %d.", error_code, target_sc);
    return true;
}

// Method to update file size of an entry in UPLOADER_EA_IMGS
bool update_db_ea_img_size(db_handle_t handle, int64_t file_size, int index_id){
    std::lock_guard<std::mutex> lock(eaDbMutex);

    stringstream update_str;
    update_str << "UPDATE UPLOADER_EA_IMGS "
               << "SET FILE_SIZE_BYTES = " << file_size
               << " WHERE INDEXID = " << index_id << ";";
    string update_cmd = update_str.str();

    int ret = sqlite3_exec(handle, update_cmd.c_str(), NULL, 0, NULL);
    if (ret != SQLITE_OK) {
        LOG_E(TAG, "Failed to update file size in DB: %s", sqlite3_errmsg(handle));
        return false;
    }

    return true;
}

// Method to update error code of an entry in UPLOADER_EA_IMGS
bool update_db_ea_img_error_code(db_handle_t handle, int index_id, ea_img_status error_code){
    std::lock_guard<std::mutex> lock(eaDbMutex);
    stringstream update_str;
    update_str << "UPDATE UPLOADER_EA_IMGS "
               << "SET ERROR_CODE = " << static_cast<int>(error_code)
               << " WHERE INDEXID = " << index_id << ";";
    string update_cmd = update_str.str();

    int ret = sqlite3_exec(handle, update_cmd.c_str(), NULL, 0, NULL);
    if (ret != SQLITE_OK) {
        LOG_E(TAG, "Failed to update error code in DB: %s", sqlite3_errmsg(handle));
        return false;
    }

    LOG_I(TAG, "Updated EA image error code in DB");
    return true;
}
