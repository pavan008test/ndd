#include <sqlite3.h>
#include <string>
#include "nd_msg_types.h"
#include <pthread.h>

typedef sqlite3 *uploader_db_handle_t;

extern const char *TAG;
extern string device_id;
extern string EA_IMGS_PATH;
extern NDService *nd_service_obj;
extern pthread_mutex_t qlock_misc;
extern bool is_ea_imgs_req_present;
extern uploader_db_handle_t ea_db_handle;
extern const string EA_IMGS_ZIPS_PATH;
extern const int EA_IMGS_DB_ENTRY_LIMIT; // 1 Lakh

// DB interaction methods
bool insert_db_ea_imgs_in_bulk(uploader_db_handle_t handle, vector<ea_file_data_t>& file_details);
bool insert_db_ea_imgs(uploader_db_handle_t handle, uploader_add_ea_file_db_msg_t *upload_req, ea_img_status& error_code);
bool get_db_ea_imgs_count(uploader_db_handle_t handle, int& count);
bool get_db_ea_imgs_for_cleanup(uploader_db_handle_t handle, vector<ea_db_file_data_t>&all_files_db);
bool get_db_ea_imgs_below_sc(uploader_db_handle_t handle, int64_t target_sc, vector<string>& files_list);
bool get_db_ea_imgs_in_batch(uploader_db_handle_t handle, int batch_count, vector<ea_cache_file_data_t> &ea_imgs);
bool get_oldest_db_ea_imgs(uploader_db_handle_t handle, long& overflow_count, vector<pair<string, int>>&oldest_files);
bool get_db_ea_imgs_for_folder_cleanup(uploader_db_handle_t handle, vector<ea_file_disk_cleanup_data_t>& db_file_details, string folder_path);
bool update_db_ea_img_size(uploader_db_handle_t handle, int64_t file_size, int index_id);
bool update_db_ea_img_error_code(uploader_db_handle_t handle, int index_id, ea_img_status error_code);
bool update_excess_db_ea_imgs(uploader_db_handle_t handle, int64_t target_sc, ea_img_status error_code);
bool update_misc_db_ea_imgs(uploader_db_handle_t handle, vector<int>&index_list, ea_img_status error_code);
bool delete_oldest_db_ea_imgs(uploader_db_handle_t handle, int target_indexid);
bool delete_db_ea_imgs_in_batch(uploader_db_handle_t handle, vector<ea_cache_file_data_t>&ea_imgs_cache);

// Business logic methods
bool is_ea_path_healthy();
bool monitor_ea_folder_size();
void clean_ea_zips(string path);
int get_pending_ea_imgs_count();
void remove_unoperated_ea_imgs();
bool ea_imgs_sync_folder_and_db();
void handle_db_entries_overflow(long db_count);
void cleanup_old_ea_imgs_drp(int64_t target_sc, bool drp_enabled);
int64_t extract_sc_from_zip_name(const string &zip_path);
void send_ea_folder_details_to_hs(long folder_size, long pending_images);
void delete_files_from_disk(vector<ea_cache_file_data_t>& ea_imgs_cache);
ea_img_status get_img_status_from_msg(uploader_add_ea_file_db_msg_t* msg);
char* create_ea_zip_upload_payload(vector<ea_cache_file_data_t>& ea_imgs_cache);
void send_ea_zip_details_to_hs(vector<ea_cache_file_data_t>& files_in_zip, string& ea_imgs_zip_path_to_upload);
void sync_folder_with_db(vector<pair<long int, string>>& all_files_dir, vector<ea_db_file_data_t>& all_files_db);
void get_ea_folder_and_db_details(vector<pair<long int, string>>& all_files_dir, vector<ea_db_file_data_t>& all_files_db);
pair<bool, string> create_ea_imgs_zip(vector<ea_cache_file_data_t> &file_list, string &unoperated_ea_img_dest);

extern uint64_t get_epoch();
extern bool is_path_healthy(string obs_path);
extern bool get_filename_from_path(string filename, string& filename_no_path);
extern void get_files( const char* path, vector< pair<long int, string> > &file_list, const char* extension);
extern void send_obs_gen_message_healthstats(string meta_fname, int64_t upload_add, int64_t upload_del, string status, string reason);

