#include <unordered_map>
#include "nd_msg_types.h"
#include <jansson.h>
#include <log.h>
#include <nd_file_utils.h>
#include "service_utils.h"
#include <nd_time.h>
#include <system_utils.h>
#include <nd_auth_openssl.h>
#include <nd_cb_utils.h>
#include "ea_images_utils.h"

typedef sqlite3 *uploader_db_handle_t;


// Method to clear unoperated_ea folder which contains decrypted EA images
void remove_unoperated_ea_imgs(){
    string command_del_all = "rm -rf /home/ubuntu/.nddevice/unoperated_ea/*";
    int delete_status = system(command_del_all.c_str());
    if(delete_status != 0){
        LOG_E(TAG, "Error: Could not delete files from /home/ubuntu/.nddevice/unoperated_ea/, Error: %s", strerror(errno));
    }else{
        LOG_I(TAG, "Successfully deleted all files from /home/ubuntu/.nddevice/unoperated_ea/");    
    }
}

// Method to get count of ea image entries in DB to decide on back-to-back uploads to address pile-up
int get_pending_ea_imgs_count(){
    int count = 0;
    if(!get_db_ea_imgs_count(ea_db_handle, count)){
        string str_msg = "EA imgs get count from table failed";
        LOG_E(TAG, str_msg.c_str());
        nd_service_obj->send_err_msg(SM_E_UPLD_EA_IMGS_DB_GET_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg);
    }
    return count; 
}

bool is_ea_path_healthy(){
   if(!is_path_healthy(EA_IMGS_PATH)) {
        string str_msg = "EA images path read error. Possible case of internal bad blocks!!";
        LOG_E(TAG, str_msg.c_str());
        nd_service_obj->send_err_msg(SM_E_UPLD_FILE_READ_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg);
        return false;
    }
    return true; 
}

// ea_zip_name will be like "ea_images_<epoch>_<newest_sc>.7z". This function extracts newest_sc from it. 
int64_t extract_sc_from_zip_name(const string &zip_path) {

    string zip_name; 
    get_filename_from_path(zip_path, zip_name);
    size_t last_underscore = zip_name.find_last_of('_');
    size_t dot_pos = zip_name.find_last_of('.');

    if (last_underscore != string::npos && dot_pos != string::npos && last_underscore < dot_pos) {
        string sc_str = zip_name.substr(last_underscore + 1, dot_pos - last_underscore - 1);
        try {
            int64_t sc_int64; 
            string_to_int64(sc_str, sc_int64);
            return sc_int64;
        } catch (const invalid_argument &e) {
            LOG_E(TAG, "Invalid number format in zip name: %s", sc_str.c_str());
        } catch (const out_of_range &e) {
            LOG_E(TAG, "Number out of range in zip name: %s", sc_str.c_str());
        }
    }

    return -1;
}

void send_ea_zip_details_to_hs(vector<ea_cache_file_data_t>& files_in_zip, string& ea_imgs_zip_path_to_upload) {
    json_t *root = json_object();
    json_t *zip_info = json_object();
    json_t *files_list = json_object();

    if (!root || !zip_info || !files_list) {
        LOG_E(TAG, "Failed to create JSON objects for sending EA zip details to HS");
        json_decref(root); json_decref(zip_info); json_decref(files_list);
        return;
    }

    json_object_set_new(zip_info, "zip_size", json_integer(max(0, get_file_size(ea_imgs_zip_path_to_upload))));
    json_object_set_new(zip_info, "timestamp", json_integer(get_system_time()));

    for (const auto& file : files_in_zip) {
        json_object_set_new(files_list, file.file_name.c_str(), json_pack("{s:i}", "ea_img_status", static_cast<int>(file.error_code)));
    }

    json_object_set_new(zip_info, "files_list", files_list);
    json_object_set_new(root, "isArray", json_string("true"));
    json_object_set_new(root, "health_info:ea_imgs:requests", zip_info);

    char* req_params = json_dumps(root, 0);
    if (!req_params) {
        LOG_E(TAG, "EA zip details JSON dump failed for sending EA zip details to HS");
        json_decref(root);
        return;
    }

    nd_service_obj->send_msg_healthstats(req_params, strlen(req_params));

    json_decref(root);
    free(req_params);
}

void send_ea_folder_details_to_hs(long folder_size, long pending_images) {
    json_t *root = json_object();
    if(root == NULL){
        LOG_E(TAG, "Failed to create root JSON object for sending EA folder details to HS");
        return;
    }
    char *req_params = NULL;

    json_object_set_new(root, "session", json_string("health_info:ea_imgs:folder_details"));
    json_object_set_new(root, "folder_size", json_integer(folder_size));
    json_object_set_new(root, "timestamp", json_integer(get_system_time()));
    json_object_set_new(root, "pending_images", json_integer(pending_images));

    req_params = json_dumps(root, 0);
    if(req_params == NULL){
        LOG_E(TAG, "EA folder details JSON dump failed for sending EA folder details to HS");
        json_decref(root);
        return; 
    }
    int length = strlen(req_params);

    nd_service_obj->send_msg_healthstats(req_params, length);

    json_decref(root); 
    free(req_params);
}

void delete_files_from_disk(vector<ea_cache_file_data_t>& ea_imgs_cache){
    for(int ind = 0; ind < ea_imgs_cache.size(); ind++){
        if(ea_imgs_cache[ind].error_code == ea_img_status::IMAGE_AVAILABLE){
            string file_path = EA_IMGS_PATH + ea_imgs_cache[ind].file_name;
            if(file_delete(file_path)){
                LOG_I(TAG, "Deleted EA image after upload: %s", file_path.c_str());
            }else{
                LOG_E(TAG, "EA imgs disk deletion failed after upload: %s", file_path.c_str());
            }
        }
    }
}

bool compare_ea(ea_file_disk_cleanup_data_t first, ea_file_disk_cleanup_data_t second){
    return first.session_count < second.session_count;
}

/*
    Method to delete old disk EA images files in case of folder size quota overflow and update status in DB
    Invoked: 
    1. Called when sdcard is mounted
    2. Called every 1 hour by handle_req_upload_misc thread
*/
bool monitor_ea_folder_size(){
    LOG_I(TAG,"checking EA images files to recover disk space");

    long total_size = 0;
    vector<ea_file_disk_cleanup_data_t> file_details;

    bool query_status = get_db_ea_imgs_for_folder_cleanup(ea_db_handle, file_details, EA_IMGS_PATH);
    if(query_status){
        for(int ind = 0; ind < file_details.size(); ind++){
            total_size += file_details[ind].file_size;
        }
    }else{
        LOG_E(TAG, "EA imgs folder size cleanup get table failed");
        file_details.clear();

        // fallback in case of query failure or db corruption. Proceeding with file system details
        vector<string> files_list;
        get_files(EA_IMGS_PATH.c_str(), files_list);
        ea_file_disk_cleanup_data_t file;
        for(const string& file_name: files_list){
            string file_path = EA_IMGS_PATH + file_name;
            long cur_size = file_size(file_path);

            file.file_size = cur_size;
            file.session_count = sessionCount_from_file(file_name);
            file.file_path = file_path;
            file_details.push_back(file);

            total_size += cur_size;
        }            
    }

    // if query_status = true, file_details will contain db entries else file system entries
    send_ea_folder_details_to_hs(total_size, file_details.size());

    if(total_size <= EA_IMGS_FOLDER_SIZE){
        LOG_I(TAG,"EA images folder size quota within limit! Folder size: %d", total_size);
        return true; 
    }else if(query_status == false){ //query failed and there is overflow. Proceed with fallback
        sort(file_details.begin(), file_details.end(), compare_ea);
    }
    
    long overflow = total_size - EA_IMGS_FOLDER_SIZE;
    int64_t target_sc = -1;
    long prefix_size = 0;
    
    bool all_files_delete_success = true; // To check if deletion failed for atleast 1 file 
    for(int ind = 0; ind < file_details.size(); ind++){
        long cur_size = file_details[ind].file_size;

        string file_path = file_details[ind].file_path;
        if(file_is_present(file_path)){
            int ret = remove(file_path.c_str());
            if(ret != 0){
                LOG_E(TAG, "Error deleting folder size quota exceeded file: %s", file_path.c_str());
                string str_msg = "Delete file to maintain EA imgs folder size quota failed!";
                nd_service_obj->send_err_msg(SM_E_UPLD_EA_IMGS_FOLDER_SIZE_QUOTA_EXCEED, NDService::UNUSED_ERR_AUX_CODE, str_msg);
                all_files_delete_success = false;
            }else{
                LOG_I(TAG, "Deleted file to maintain EA imgs folder size quota: %s", file_path.c_str());
            }
        }

        if(prefix_size + cur_size >= overflow){
            target_sc = file_details[ind].session_count; 
            break;
        }else{
            prefix_size += cur_size;
        }
    }

    if(!all_files_delete_success){
        string str_msg = "Disk Cleanup of EA imgs due to folder size quota failed for atleast 1 file";
        LOG_E(TAG, str_msg.c_str());
        nd_service_obj->send_err_msg(SM_E_UPLD_EA_IMGS_FOLDER_SIZE_QUOTA_EXCEED, NDService::UNUSED_ERR_AUX_CODE, str_msg);
    }

    query_status = update_excess_db_ea_imgs(ea_db_handle, target_sc, ea_img_status::DELETED_LOW_STORAGE);
    if(!query_status){
        string str_msg = "EA imgs folder size cleanup update table failed";
        LOG_E(TAG, str_msg.c_str());
        nd_service_obj->send_err_msg(SM_E_UPLD_EA_IMGS_DB_UPDATE_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg);
        return false;
    }

    return true;
}

// Method to honor DRP by deleting EA images in disk and updating status in DB
void cleanup_old_ea_imgs_drp(int64_t target_sc, bool drp_enabled) {
    string reason = "";
    if(drp_enabled){
        reason = "DRP";
    }else{
        reason = "CB broadcasted SC";
    }

    LOG_C(TAG, "Cleaning up old EA images based on %s", reason.c_str());
    vector<string> files_list;
    bool query_status = get_db_ea_imgs_below_sc(ea_db_handle, target_sc, files_list);
    if(!query_status){
        string str_msg = "DB GET query failed while" + reason + " cleanup";
        LOG_E(TAG, str_msg.c_str());
        nd_service_obj->send_err_msg(SM_E_UPLD_EA_IMGS_DB_GET_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg);
    }

    if(files_list.size() == 0){
        LOG_I(TAG, "No files to delete while %s cleanup! Exiting...", reason.c_str());
        return;
    }

    bool all_files_delete_success = true; // To check if deletion failed for atleast 1 file 
    for(int ind = 0; ind < files_list.size(); ind++){
        string file_path = EA_IMGS_PATH + files_list[ind];
        bool file_delete_status = false;
        pthread_mutex_lock(&qlock_misc);
        file_delete_status = file_delete(file_path);
        pthread_mutex_unlock(&qlock_misc);

        if(file_delete_status){
            LOG_I(TAG, "Deleted file while %s cleanup: %s", reason.c_str(), file_path.c_str());
        }else {
            all_files_delete_success = false;
            LOG_E(TAG, "Delete file while %s cleanup failed!: %s", reason.c_str(), file_path.c_str());
        }
    }

    query_status = update_excess_db_ea_imgs(ea_db_handle, target_sc, ea_img_status::DELETED_DRP);
    if(!query_status){
        string str_msg = "DB UPDATE query failed while" + reason + " cleanup";
        LOG_E(TAG, str_msg.c_str());
        nd_service_obj->send_err_msg(SM_E_UPLD_EA_IMGS_DB_UPDATE_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg);
    }

    if(!all_files_delete_success){
        string str_msg = "Disk Cleanup of EA imgs based on " + reason + " cleanup failed for atleast 1 file";
        LOG_E(TAG, str_msg.c_str());
        nd_service_obj->send_err_msg(SM_E_UPLD_EA_IMGS_DRP_DELETION, NDService::UNUSED_ERR_AUX_CODE, str_msg);
    }
}

// Method to get all files in EA images folder and UPLOADER_EA_IMGS DB for bootup sync between disk and DB
void get_ea_folder_and_db_details(vector<pair<long int, string>>& all_files_dir, vector<ea_db_file_data_t>& all_files_db){
    get_files(EA_IMGS_PATH.c_str(), all_files_dir, ".jpeg");

    LOG_I(TAG, "Total files found in EA Images Folder %d", all_files_dir.size());

    if(!get_db_ea_imgs_for_cleanup(ea_db_handle, all_files_db)){
        string str_msg = "Get DB details failed for EA bootup sync";
        LOG_E(TAG, str_msg.c_str() );
        nd_service_obj->send_err_msg(SM_E_UPLD_EA_IMGS_DB_SYNC_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
       return;
    }
    
    LOG_I(TAG, "Total entries found in EA Images DB %d", all_files_db.size());
}

/*
Responsibilities:
    1. Inserts entries in DB for files present in folder but not in DB
    2. Updates file size in DB in case of mismatch
Invoked:
    1. On bootup to sync DB and EA images folder
*/
void sync_folder_with_db(vector<pair<long int, string>>& all_files_dir, vector<ea_db_file_data_t>& all_files_db) {
    int dir_ind = 0, db_ind = 0;
    
    // Create a map of db files for quick lookup
    unordered_map<string, pair<int64_t, int>> db_file_unordered_map; // <file_name, <file_size, index_id>>
    for (int ind = 0; ind < all_files_db.size(); ind++) {
        db_file_unordered_map[all_files_db[ind].file_name] = make_pair(all_files_db[ind].file_size, all_files_db[ind].index_id);
    }

    vector<ea_file_data_t> file_details; // Store new files to be inserted in bulk
    
    for (pair<long int, string>& file : all_files_dir) {
        string file_name; 
        if (!get_filename_from_path(file.second, file_name)) {
            LOG_E(TAG, "Failed to get filename from path! Skipping sync for: %s", file.second.c_str());
            continue;
        }
        int64_t file_size = get_file_size(file.second);

        if (db_file_unordered_map.find(file_name) == db_file_unordered_map.end()) {
            // File not found in DB
            LOG_I(TAG, "File not found in EA images DB: %s, Adding!", file_name.c_str());
            int cam_type = get_cam_num_from_filename(file.second);
            ea_img_status error_code = ea_img_status::IMAGE_AVAILABLE;

            ea_file_data_t file_data;
            file_data.file_name = file_name;
            file_data.file_size_bytes = file_size;
            file_data.cam_type = cam_type;
            file_data.udid = udid_from_file(file_name);
            file_data.session_count = sessionCount_from_file(file_name);
            file_data.error_code = error_code;
            
            file_details.push_back(file_data);
        } else if (db_file_unordered_map[file_name].first != file_size) {
            LOG_I(TAG, "Updating file size for EA img: %s", file_name.c_str());
            if (!update_db_ea_img_size(ea_db_handle, file_size, db_file_unordered_map[file_name].second)) {
                LOG_E(TAG, "Update file size in DB failed for EA img");
            }
        }
    }
    
    if (!file_details.empty()) {
        if(!insert_db_ea_imgs_in_bulk(ea_db_handle, file_details)){
            string str_msg = "Insert EA imgs in bulk failed for EA bootup sync";
            LOG_E(TAG, str_msg.c_str());
            nd_service_obj->send_err_msg(SM_E_UPLD_EA_IMGS_DB_INSERT_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg);
        }
    }
}

// Method invoked on bootup to delete oldest entries in DB and corresponding files in disk in case of DB size quote overflow
void handle_db_entries_overflow(long db_count){
    long overflow = db_count - EA_IMGS_DB_ENTRY_LIMIT;
    vector<pair<string,int>> oldest_files; // <file_name, indexid> 
    if(!get_oldest_db_ea_imgs(ea_db_handle, overflow, oldest_files)){
        string str_msg = "Get oldest entries in DB failed for EA bootup sync";
        LOG_E(TAG, str_msg.c_str() );
        nd_service_obj->send_err_msg(SM_E_UPLD_EA_IMGS_DB_SYNC_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
        return;
    }
    int max_indexid = -1; 
    for(auto& file: oldest_files){
        max_indexid = max(max_indexid, file.second);
        string path = EA_IMGS_PATH + file.first;
        if(file_delete(path)){
            LOG_I(TAG, "Deleted file due to DB entries overflow: %s", path.c_str());
        }else{
            LOG_E(TAG, "Delete file due to DB entries overflow Failed: %s", path.c_str());
        }   
    }

    if(!delete_oldest_db_ea_imgs(ea_db_handle, max_indexid)){
        string str_msg = "Delete oldest entries in DB failed for EA bootup sync";
        LOG_E(TAG, str_msg.c_str() );
        nd_service_obj->send_err_msg(SM_E_UPLD_EA_IMGS_DB_SYNC_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
    }
}

/*
    Method called on device bootup or uploader restart
    1. Ensures that number of entries in DB are within limit
    2. Syncs folder and DB
*/
bool ea_imgs_sync_folder_and_db(){
    LOG_I(TAG, "EA Images folder and DB sync started");
    int db_count = 0; 
    if(!get_db_ea_imgs_count(ea_db_handle, db_count)){
        string str_msg = "Get DB count failed for EA bootup sync";
        LOG_E(TAG, str_msg.c_str() );
        nd_service_obj->send_err_msg(SM_E_UPLD_EA_IMGS_DB_SYNC_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
    }

    if(db_count > EA_IMGS_DB_ENTRY_LIMIT){
        LOG_I(TAG, "DB entry count is more than limit. Deleting oldest entries");
        handle_db_entries_overflow(db_count);
    }else{
        LOG_I(TAG, "DB entry count is within limit. Proceeding with sync");
    }

    vector<pair<long int, string>> all_files_dir;
    vector<ea_db_file_data_t>all_files_db;
    get_ea_folder_and_db_details(all_files_dir, all_files_db);
    sync_folder_with_db(all_files_dir, all_files_db);
}

pair<bool, string> create_ea_imgs_zip(vector<ea_cache_file_data_t> &file_list, string &unoperated_ea_img_dest){
    bool zip_created = false;
    int file_count_in_zip = 0;
    
    int last_ind = file_list.size() - 1;
    string newest_file_name = file_list[last_ind].file_name;
    int64_t newest_sc = sessionCount_from_file(newest_file_name);
    
    //zip file name preparation
    stringstream ss; 
    ss << "ea_images_" << get_epoch() << "_" << newest_sc <<".7z";
    string ea_imgs_zip_path_to_upload = EA_IMGS_ZIPS_PATH + ss.str();

    string response_sz = "";
    stringstream sevenz_cmd_stream;
    string sevenz_arg = "7za a ";
    sevenz_cmd_stream <<  sevenz_arg << ea_imgs_zip_path_to_upload;
    remove_unoperated_ea_imgs(); // remove any previous unoperated files

    try{
        for(int ind = 0; ind < file_list.size(); ind++){
            if(file_list[ind].error_code != ea_img_status::IMAGE_AVAILABLE){
                continue; 
            }
            string cur_file_name = file_list[ind].file_name;
            string cur_file_path = EA_IMGS_PATH + cur_file_name;

            string unoperated_ea_img_dest_path = unoperated_ea_img_dest + cur_file_name;

            bool file_present_status = false;
            pthread_mutex_lock(&qlock_misc);
                file_present_status = file_is_present(cur_file_path);
            pthread_mutex_unlock(&qlock_misc);

            if(!file_present_status){
                file_list[ind].error_code = ea_img_status::ERROR_UNKNOWN;
            
                if(!update_db_ea_img_error_code(ea_db_handle, file_list[ind].index_id, ea_img_status::ERROR_UNKNOWN)){
                    string str_msg = "Update EA img error code in DB failed";
                    LOG_E(TAG, str_msg.c_str());
                    nd_service_obj->send_err_msg(SM_E_UPLD_EA_IMGS_DB_UPDATE_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg);
                }

                LOG_W(TAG,"Skipping file! Not present in disk but marked available in DB: %s", cur_file_path.c_str());
                continue;
            }
            result decryption_status = ND_AUTH_ERROR; 

            pthread_mutex_lock(&qlock_misc);
            decryption_status = nd_file_reoperate_to_file(cur_file_path.c_str(), unoperated_ea_img_dest_path.c_str());
            pthread_mutex_unlock(&qlock_misc);

            if (ND_AUTH_SUCCESS != decryption_status){
                file_list[ind].error_code = ea_img_status::DECRYPTION_ERR;
                
                if(!update_db_ea_img_error_code(ea_db_handle, file_list[ind].index_id, ea_img_status::DECRYPTION_ERR)){
                    string str_msg = "Update EA img error code in DB failed";
                    LOG_E(TAG, str_msg.c_str());
                    nd_service_obj->send_err_msg(SM_E_UPLD_EA_IMGS_DB_UPDATE_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg);
                }

                string reason = "skipping ea img because of reoperation failure or zero file size";
                LOG_W(TAG, "%s: %s", reason.c_str(), cur_file_path.c_str());
                send_obs_gen_message_healthstats(cur_file_path, -1, get_system_time(), "fail", reason);

                if(file_delete(cur_file_path)){
                    LOG_I(TAG,"Deleted file due to reoperation failure: %s", cur_file_path.c_str());
                }else{
                    LOG_E(TAG,"Delete file due to reoperation failure Failed: %s", cur_file_path.c_str());
                }

                continue; 
            }

            LOG_I(TAG,"adding ea img to zip cmd: %s", unoperated_ea_img_dest_path.c_str());
            sevenz_cmd_stream << " " << unoperated_ea_img_dest_path;
            file_count_in_zip++;
        }

        if(file_count_in_zip == 0){
            LOG_I(TAG,"No files added to zip. Returning!");
            ea_imgs_zip_path_to_upload = "";
            return make_pair(false, ea_imgs_zip_path_to_upload);
        }

        int resp_code = -1;
        string response_sz = "";
        string sevenz_cmd = sevenz_cmd_stream.str();

        LOG_I(TAG, "Creating EA images zip file at once! cmd: %s", sevenz_cmd.c_str());
        printf("EA images 7z cmd: %s", sevenz_cmd.c_str());

        bool zip_add_status = system_execute_with_resp_and_resp_code("7z ea img", sevenz_cmd, response_sz, resp_code);
        LOG_I(TAG, "7z resp: status:%d, message: %s", zip_add_status, response_sz.c_str());
        if(!zip_add_status){
            string reason = "zip add failure for ea images";
            LOG_E(TAG, "7z command encountered errors: %s", response_sz.c_str());
            nd_service_obj->send_err_msg(SM_E_UPLD_ZIP_READ_FAIL, NDService::UNUSED_ERR_AUX_CODE, reason);
        }

        zip_created = file_is_present(ea_imgs_zip_path_to_upload);
        if(zip_created){
            LOG_I(TAG,"EA images zip file created!");
            LOG_I(TAG,"Archived EA images size:  %0.2f kB, File count: %d",
                    (float)(get_file_size(ea_imgs_zip_path_to_upload) / 1000.0),
                    file_count_in_zip);
            file_fd_sync(ea_imgs_zip_path_to_upload);
        }
        // remove the unoperated(decrypted) ea images files in both the cases: successfully added to zip or or failed to zip
        remove_unoperated_ea_imgs();
    }
    catch (const std::runtime_error& re) {
        LOG_E(TAG, "Zipper exception!! Internal read error: %s", re.what());
        nd_service_obj->send_err_msg(SM_E_UPLD_ZIP_READ_FAIL, NDService::UNUSED_ERR_AUX_CODE, "Zipper exception!! Internal read error" );
    }
    catch(...){
        LOG_E(TAG, "Zipper exception!! error: Unknown. Check syslog!!");
        nd_service_obj->send_err_msg(SM_E_UPLD_ZIP_READ_FAIL, NDService::UNUSED_ERR_AUX_CODE, "Zipper exception!! error: Unknown. Check syslog!!" );
    }

    return make_pair(zip_created, ea_imgs_zip_path_to_upload);
}

char* create_ea_zip_upload_payload(vector<ea_cache_file_data_t>& ea_imgs_cache){
    json_t* root = json_object();
    
    if (root == NULL) {
        LOG_E(TAG, "Failed to create JSON object for EA images upload.");
        return NULL;
    }

    json_t* img_details = json_object();
    if (img_details == NULL) {
        LOG_E(TAG, "Failed to create JSON object for EA images upload.");
        json_decref(root);
        return NULL;
    }

    for (const auto& img : ea_imgs_cache) {
        json_object_set_new(img_details, img.file_name.c_str(), json_integer(static_cast<int>(img.error_code)));
    }

    json_object_set_new(root, "images", img_details);
    json_object_set_new(root, "device_id", json_string(device_id.c_str()));
    
    char* req_params = json_dumps(root, 0);
    if (req_params == NULL) {
        LOG_E(TAG, "EA image upload payload creation failed.");
    }

    /* Create a formatted copy for debugging
    char* formatted_json = json_dumps(root, JSON_INDENT(4));
    FILE* log_file = fopen("/home/ubuntu/.nddevice/ea_payload.txt", "a");
    if (log_file != NULL) {
        fprintf(log_file, "EA images upload payload:\n%s\n\n", formatted_json ? formatted_json : req_params);
        fclose(log_file);
        LOG_I(TAG, "Successfully wrote formatted EA image upload payload into ea_payload.txt");
    } else {
        LOG_E(TAG, "Failed to write to log file.");
    }

    free(formatted_json);
    */

    json_decref(root);
    return req_params;
}

void clean_ea_zips(string path){
    vector<pair<long int,string>>previous_zips;
    get_files(path.c_str(), previous_zips, ".7z");
    for(auto zip: previous_zips){
        if(file_delete(zip.second)){
            LOG_I(TAG, "Deleted old EA zip on bootup: %s", zip.second.c_str());
        }else{
            LOG_E(TAG, "Failed to delete old EA zip on bootup: %s", zip.second.c_str());
        }
    }
}

ea_img_status get_img_status_from_msg(uploader_add_ea_file_db_msg_t* msg){
    using privacy = ea_image_privacy_type;

    switch (msg->privacy_type) {
        case privacy::NO_PRIVACY:
            return ea_img_status::IMAGE_AVAILABLE;
        case privacy::RECORD_PRIVACY:
            return ea_img_status::RECORD_PRIVACY;
        case privacy::UPLOAD_PRIVACY:
            return ea_img_status::UPLOAD_PRIVACY;
        case privacy::PARTIAL_PRIVACY:
            return ea_img_status::PARTIAL_PRIVACY;
        case privacy::LPW_NO_CAPTURE:
            return ea_img_status::LPW_NO_CAPTURE;
        default:
            return ea_img_status::ERROR_UNKNOWN;
    }
}

