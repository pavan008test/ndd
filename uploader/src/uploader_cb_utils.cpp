#include <string>
#include <sstream>
#include <nd_db_utils.h>
#include <log.h>
#include <nd_msg_types.h>
#include <nd_file_utils.h>
#include <system_utils.h>
#include <nd_factory.h>
#include <storage_utils.h>
using namespace std;

extern ND_DeviceFactory *nd_device_obj;
db_handle_t* cb_db_handle = NULL;

extern string CB_DB_PATH;
extern string MIGRATION_DB_PATH;
extern string sdcard_img_path;

static const char *TAG="UPL_CB";

bool init_circ_buff_db(){
    if(nd_open_db(CB_DB_PATH, &cb_db_handle) == false) {
        cb_db_handle = NULL;
        LOG_E(TAG, "failure in open DB");
        return false;
    }
    return true;
}

static int callback_count_rows(void *count, int argc, char **argv, char **azColName){
    int i;
    int64_t *temp_count = (int64_t* )count;

    temp_count[0] = temp_count[0] + argc;
    if (argc != 1){
        temp_count[1] = 0;
    } else {
        int row_count = 0;
        if(string_to_integer(argv[0], row_count) == false) {
            LOG_E(TAG, "failed to get integer from string: %s", argv[0]);
        }
        temp_count[1] += argv[0] ? row_count : 0;
    }
    return 0;
}

bool transcoding_in_progress(string video_path, circular_buffer_transcode_status_t tc_status){
    init_circ_buff_db();
    int rc;
    bool ret = true;
    string folder, video_fname;
    if(get_folder_file_names(video_path, folder, video_fname) == false) {
        LOG_E(TAG, "failed to split folder and file names");
        return false;
    }
    string val_string;
    std::stringstream val_stream;
    val_stream <<
                "SELECT COUNT(*) from VIDFILES WHERE (TRANSCODE_STATUS == "
                << tc_status << " ) AND ( NAME == '"
                << video_fname << "');";
    val_string = val_stream.str();
    int64_t count_rows[2] = {0, 0};
    rc = nd_exec_cmd_db(cb_db_handle, val_string, callback_count_rows, (void*)&count_rows[0]);
    if(rc == false){
        LOG_E(TAG, "failed to execute");
        nd_close_db(cb_db_handle);
        return false;
    }
    int count = count_rows[1];
    nd_close_db(cb_db_handle);
    LOG_I(TAG, "transcoding in progress: %d", count);
    return count == tc_status;
}

